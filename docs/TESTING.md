# 测试与验收

本文档把"验证"分成三层，并逐层说明**当前真实状态**。请务必区分"编译通过"和"实机通过"。

---

## 0. 当前状态总览

| 层级 | 内容 | 状态 | 证据 |
| --- | --- | --- | --- |
| L1 编译验证 | 对 `esp32:esp32:esp32c3` 干净编译 | **通过** | 0 error / 0 warning，flash 692269 B (52%)，RAM 19564 B (5%) |
| L2 宿主端模型验证 | 解析器/状态机/键表/HID 描述符/随机不变量 | **通过** | `python tests/model/check_vectors.py` → `checks passed: 11093, failed: 0` |
| L3 设备端自检 | 在真机 MCU 上跑同一套向量 + 分发仿真 | **未运行（阻塞）** | 串口打不开，见 §4 |
| L4 实机端到端 | RC003 ↔ C3 ↔ Windows 全链路 | **未运行（阻塞）** | 同上 |

> **没有实机验证的内容一律标记为"编译验证"或"代码级/模型级验证"，不声称已通过硬件测试。**

---

## 1. L1 编译验证（已通过）

```powershell
.\scripts\build.ps1
```

或直接：

```
arduino-cli compile --fqbn esp32:esp32:esp32c3 `
  --output-dir ./build/MiRemoteBridge ./firmware/MiRemoteBridge
```

实测结果：

```
Sketch uses 692269 bytes (52%) of program storage space. Maximum is 1310720 bytes.
Global variables use 19564 bytes (5%) of dynamic memory, leaving 308116 bytes for local variables.
```

无 warning、无 error。

### 编译相关排查

| 现象 | 原因与处理 |
| --- | --- |
| `.\scripts\build.ps1` 跑完"什么都没有发生"、没有输出、也没有生成 bin | PowerShell 5.1 在启动子进程时会用环境变量构造一个**大小写不敏感**的字典；若环境里同时存在 `http_proxy` 和 `HTTP_PROXY`，构造会抛异常，表现就是子进程静默不执行。`scripts/_common.ps1` 启动时会自动删除重复项并打印一行提示。若仍无效，用系统级命令 `set HTTP_PROXY=` 后重开终端 |
| 中文路径编译报错 | 工具链必须留在 `C:\code\arduino-c3-data`。见 `arduino-cli.yaml` 的注释 |
| 提示找不到 arduino-cli | 不要重装。工具链由提交 `16b3766` 固定，检查 `.tools\arduino-cli-1.5.1\arduino-cli.exe` 是否存在 |

编译产物（`build/MiRemoteBridge/`，已被 `.gitignore` 排除）：

| 文件 | 用途 |
| --- | --- |
| `MiRemoteBridge.ino.bin` | 应用固件 |
| `MiRemoteBridge.ino.bootloader.bin` | 二级引导 |
| `MiRemoteBridge.ino.partitions.bin` | 分区表 |
| `MiRemoteBridge.ino.merged.bin` | 0x0 起始的整片镜像 |
| `MiRemoteBridge.ino.elf` / `.map` | 调试与尺寸分析 |

---

## 2. L2 宿主端模型验证（已通过，可今天复现）

```powershell
.\scripts\test.ps1
```

或：

```bash
python tests/tools/gen_vectors.py
python tests/model/check_vectors.py
```

覆盖内容：

1. **报文解析向量**（16 条）：1/2/3/5/8 字节报告、全零=释放、音频长度（>8）不算按键、
   修饰键字节不会被误判成按键。
2. **ATVV 控制报文向量**（4 条）：`AUDIO_START(reason=0x03)` = 语音按下，`AUDIO_STOP` / `MIC_CLOSED` = 松开，
   `CAPS` 响应不算按键。
3. **按下/松开状态机向量**（8 条）：重复按下被吞掉、重复松开被吞掉、未松开就按新键会先自动释放旧键。
4. **键表向量**（20 条）：13 个物理键 + 5 个别名码 + 2 个未知码必须映射为"无"。
5. **运行时模式向量**（8 条）：返回/电源/语音三组可选模式的每一种取值。
6. **未知码全覆盖检查**：遍历 0x00–0xFF，任何非已知键码都必须映射为"无"——保证不会把杂散字节
   当按键发出去。
7. **HID 报告描述符解析**：真实解析 `hid_report_map.h` 的字节流，断言
   - Report ID 1 输入 = 64 bit（8 字节），输出 = 8 bit（1 字节）
   - Report ID 2 输入 = 16 bit（2 字节）
   - 只有 2 个顶层 Collection、Collection 不嵌套
   - Array 项的 Usage Maximum 不超过 Logical Maximum
   - `HID_KEYBOARD_REPORT_LEN` / `HID_CONSUMER_REPORT_LEN` 与描述符一致
8. **随机不变量测试**（4000 步，固定种子 20260910）：随机产生按下/松开/重复/强制全松开序列，
   每一步都断言
   - 主机侧同时按下的键**至多 1 个**；
   - 按下的键一定有非空映射；
   - 释放的键一定处于按下状态；
   - 序列结束后主机状态**完全为空**。

**这一层验证的是规则和表格，不是 C 代码本身。** C 代码由 L3 验证。

---

## 3. L3 设备端自检（未运行 —— 阻塞）

固件内置 `selftest` 命令，在真机上跑**同一套向量**（由 `tests/tools/gen_vectors.py` 从同一份
JSON 生成 `selftest_vectors.h`），因此设备与宿主不会对"期望值"产生分歧。

连上串口后执行：

```
selftest
```

它做两件事：

- **向量套件**：解析器、ATVV、状态机、键表、运行时模式、事件队列（FIFO 顺序、满队列拒绝、
  环形回绕）、报告描述符常量。
- **分发仿真**：用合成报文真正走一遍
  `parse → tracker → 事件队列 → bridge::loop() → HID 分发`，
  并断言：三连重复按下只发一次报告、重复松开只发一次报告、未松开就按新键会先释放旧键、
  13 个键逐个按下/松开都干净结束、长按后松开干净、快速交替后干净、**任一侧断线后当前按键被清零**、
  ATVV 语音按下/松开正确。

预期输出结尾：

```
[TEST   ] vector suite: N passed, 0 failed
[TEST   ] dispatch simulation: M passed, 0 failed
[TEST   ] selftest total: 0 failure(s)
[TEST   ] RESULT: PASS
```

**当前未运行，原因见 §4。** 一旦串口可用，这是"无需另一个对端设备"能拿到的最强证据。

---

## 4. 当前阻塞点（必须如实记录）

用户已说明串口是 **COM3**。实测结果：

```
$ arduino-cli board list
Port Protocol Type              Board Name FQBN Core
COM3 serial   Serial Port (USB) Unknown

$ esptool --port COM3 chip-id
A fatal error occurred: Could not open COM3, the port is busy or doesn't exist.
(Cannot configure port, something went wrong. Original message:
 PermissionError(13, '连到系统上的设备没有发挥作用。', None, 31))

$ [System.IO.Ports.SerialPort]::new('COM3',115200).Open()
OPEN FAILED: 连到系统上的设备没有发挥作用。
```

设备管理器状态：

```
USB-Enhanced-SERIAL CH343 (COM3) | Status=OK | ConfigManagerErrorCode=0
USB\VID_1A86&PID_55D3\5458003768
```

结论：

- COM3 是 **CH343 USB 转串口芯片**（VID `0x1A86` / PID `0x55D3`），PnP 报告"工作正常"；
- 但端口**无法打开**（`ERROR_GEN_FAILURE`），且用 .NET 直接打开也同样失败 —— 不是"被别的程序占用"
  （那会是 access denied）；
- 因此**无法确认 ESP32-C3 是否真的连在这条串口上**，也无法确认板子是否上电。

**需要你确认/处理：**

1. 板子是否已上电、USB 线是否是**数据线**（很多线只能充电）；
2. 是否有其他程序占着 COM3（Arduino IDE 串口监视器、PuTTY、其他终端）—— 有就先关掉；
3. 换一个 USB 口（直连主板，别经过 Hub）再试；
4. 处理后用只读命令复验，**这一步不写任何东西**：
   ```powershell
   .\scripts\flash.ps1 -Port COM3 -ProbeOnly
   ```
   期望看到 `Chip is ESP32-C3`。若仍失败，`Get-PnpDevice -Class Ports | Format-List *` 的输出能帮助定位。

在 `-ProbeOnly` 返回 `Chip is ESP32-C3` 之前，**不会**执行任何烧录。

---

## 5. L4 实机验收清单（待执行）

按顺序做完并回填。每条都有"怎么判"和"期望结果"。

### 5.1 基础链路

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 1 | 烧录 | `.\scripts\flash.ps1 -Port COM3` | `UPLOAD OK`，串口出启动横幅 | ☐ |
| 2 | 上游配对 | 让 RC003 广播，等待自动识别 | 日志出现 `target found` → `ready: N subscription(s)` | ☐ |
| 3 | 下游配对 | Windows 蓝牙添加 `Mi Remote Bridge` | 出现 `report 1 notifications ENABLED` | ☐ |
| 4 | Windows 识别 | 设置 / 设备管理器 | 出现"键盘"+ 消费类控制设备，无"未知设备" | ☐ |
| 5 | 设备端自检 | `selftest` | `RESULT: PASS` | ☐ |

### 5.2 13 个按键逐个按下与松开

`raw on` 打开后逐个按。**每个键都要记录原始码、转换结果、Windows 是否响应。**

| # | 物理键 | 原始码（实测） | 期望输出 | Windows 实测 | 结果 |
| --- | --- | --- | --- | --- | --- |
| 1 | 音量 + | | Consumer Vol Up | | ☐ |
| 2 | 音量 − | | Consumer Vol Down | | ☐ |
| 3 | 返回 | | Consumer AC Back | | ☐ |
| 4 | 上 | | ↑ | | ☐ |
| 5 | 下 | | ↓ | | ☐ |
| 6 | 左 | | ← | | ☐ |
| 7 | 右 | | → | | ☐ |
| 8 | 确定 | | Enter | | ☐ |
| 9 | 主页 | | Win+D | | ☐ |
| 10 | 菜单 | | Space | | ☐ |
| 11 | 电视 | | F8 | | ☐ |
| 12 | 电源 | | Alt+F4 | | ☐ |
| 13 | 语音 | | RAlt+, | | ☐ |

**若某个键的"原始码（实测）"与 `KEYMAP.md` 不同**，把实测码补进
`key_definitions.h` + `keymap.cpp` 后重新编译烧录。

### 5.3 边界与恢复

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 14 | 音量加/减/返回 | 确认这三个非标准码真的送到了 Windows | 系统音量变化 / 浏览器返回 | ☐ |
| 15 | 长时间按住 | 按住音量键 10 秒以上 | 只发一次按下（不连发），松开后立即停止 | ☐ |
| 16 | 快速连续按键 | 1 秒内连按 10 次 | 每次都正确，无丢键、无卡键 | ☐ |
| 17 | RC003 休眠后唤醒 | 静置到遥控器休眠，再按键唤醒 | 自动重连并**重新订阅**（日志有 `ready: ...`），按键恢复 | ☐ |
| 18 | C3 断电重启 | 拔插 C3 | 两侧都自动恢复；优先直连已保存地址 | ☐ |
| 19 | Windows 重启 | 重启 PC | 自动重连，事件键正常 | ☐ |
| 20 | Windows 蓝牙关闭再开启 | 关蓝牙 → 开蓝牙 | 自动重连；若需手动点一下也能连上 | ☐ |
| 21 | 删除 Windows 配对后重新配对 | `forget win` + Windows 删除设备 + 重配 | 重新配对成功 | ☐ |
| 22 | RC003 Bond 失效恢复 | 原设备删配对 → `forget rc` → 遥控器重新广播 | 重新绑定成功 | ☐ |
| 23 | 两侧同时断开 | 同时断电/关机两侧 | 无卡键；两侧恢复后仍正常 | ☐ |
| 24 | 卡键检查 | 上述各项做完后，在 Windows 记事本/浏览器里检查 | **从未出现**任何键卡住 | ☐ |

### 5.4 延迟测量

固件内置了从**收到 BLE 通知**到**发出 HID 通知**的微秒级测量，默认开启：

```
lat on
```

每按一次键会打印：

```
[  xxxxx][KEY    ] press 0x80 (VOL_UP) -> CONSUMER 0x00E9
[  xxxxx][KEY    ] latency notify->hid 1180 us
```

采集方法：连按某个键 30 次，把日志里的 `latency notify->hid` 数值抄出来统计。

| 指标 | 定义 | 实测 |
| --- | --- | --- |
| 固件内延迟（中位数） | 通知回调 → `hid_server` 完成 notify | ☐ 待测 |
| 固件内延迟（最大值） | 同上，取 30 次最大值 | ☐ 待测 |
| 端到端主观延迟 | 按键到 Windows 响应（可用在线键盘测试页面观察） | ☐ 待测 |

注意：固件内延迟**不包含**空口传输、Windows HID 栈处理和应用响应时间。
测量值只代表"固件没有引入额外延迟"。

---

## 6. 回归

每次改动后：

```powershell
.\scripts\test.ps1     # L2 + L1 一起跑
```

实机可用时再加 `selftest`。若改动涉及 BLE 参数或时机，必须重跑 §5.3 的全部项目。
