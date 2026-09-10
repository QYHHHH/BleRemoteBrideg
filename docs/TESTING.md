# 测试与验收

本文档把"验证"分成三层，并逐层说明**当前真实状态**。请务必区分"编译通过"和"实机通过"。

---

## 0. 当前状态总览

| 层级 | 内容 | 状态 | 证据 |
| --- | --- | --- | --- |
| L1 编译验证 | 对 `esp32:esp32:esp32c3:FlashMode=dio` 干净编译 | **通过** | 0 error / 0 warning，flash 691895 B (52%)，RAM 19780 B (6%) |
| L2 宿主端模型验证 | 解析器/状态机/键表/HID 描述符/随机不变量 | **通过** | `python tests/model/check_vectors.py` → `checks passed: 11095, failed: 0` |
| L3 设备端自检 | 在真机 MCU 上跑同一套向量 + 分发仿真 | **通过** | `selftest` → `137 passed, 0 failed` + `44 passed, 0 failed`，`RESULT: PASS` |
| L4 上游（RC003 → C3） | 扫描、直连、配对加密、服务发现、订阅通知、逐键解析 | **通过** | 13/13 键识别，0 未知码；延迟 min 190 / median 215 / max 361 µs（§5.1、§5.2）|
| L4 下游（C3 → Windows） | Windows 识别为蓝牙键盘 + 媒体控制设备 | **未验证** | 等 Windows 侧配对（§5.1 第 3 步）|
| L4 边界与恢复 | 长按、连按、休眠唤醒、两侧重启、卡键 | **未验证** | §5.3 |

已确证的硬件：ESP32-C3 rev v0.3 / 4MB Macronix flash / COM3(CH343) / MAC `60:55:f9:xx:xx:xx`；
RC003 `c0:5d:39:xx:xx:xx`（公开地址），广播名「小米蓝牙语音遥控器」。

**三个只在真机上才会暴露的问题已经定位并修复**，取证与根因写在 §4：

1. 板子无法启动（默认 QIO 下 flash 读回全 0xFF）；
2. 按媒体键崩溃（BLE 封装层注册不了两个同 UUID 的特征）；
3. `BLEClient::connect()` 的 timeout 参数被库忽略，导致连接失败要等 60 s（§4.4）。

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
Sketch uses 691805 bytes (52%) of program storage space. Maximum is 1310720 bytes.
Global variables use 19788 bytes (6%) of dynamic memory, leaving 307892 bytes for local variables.
```

无 warning、无 error。（体积会随每次改动小幅变化，以实际输出为准。）

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
   - 恰好 **一个** 输入报告：Report ID 1 = 80 bit（10 字节 = 8 键盘 + 2 Consumer）
   - 输出（LED）报告 = 8 bit（1 字节）
   - **不得声明 Report ID 2**（那会需要第二个同 UUID 的特征，见 §4.2）
   - 只有 2 个顶层 Collection、Collection 不嵌套
   - Array 项的 Usage Maximum 不超过 Logical Maximum
   - 字段偏移常量（`HID_INPUT_OFFSET_*`）与描述符逐项一致
8. **随机不变量测试**（4000 步，固定种子 20260910）：随机产生按下/松开/重复/强制全松开序列，
   每一步都断言
   - 主机侧同时按下的键**至多 1 个**；
   - 按下的键一定有非空映射；
   - 释放的键一定处于按下状态；
   - 序列结束后主机状态**完全为空**。

**这一层验证的是规则和表格，不是 C 代码本身。** C 代码由 L3 验证。

---

## 3. L3 设备端自检（已在真机通过）

连上串口后执行 `selftest`，它消费的是由 `tests/tools/gen_vectors.py` 从同一份 JSON 生成的
`firmware/MiRemoteBridge/selftest_vectors.h`，因此设备与宿主不会对"期望值"产生分歧。

实机输出（COM3，2026-09-10）：

```
[TEST   ] --- vector suite ---
[TEST   ] vector suite: 137 passed, 0 failed
[TEST   ] --- dispatch simulation ---
[TEST   ] dispatch simulation: 44 passed, 0 failed
[TEST   ] selftest total: 0 failure(s)
[TEST   ] RESULT: PASS
```

**向量套件**覆盖：报文解析、ATVV 控制报文、按下/松开状态机、键表（含别名码）、
运行时模式三组取值、事件队列（FIFO 顺序 / 满队列拒绝并计数 / 环形回绕）、报告布局常量。

**分发仿真**覆盖：三连重复按下只发一次报告、重复松开只发一次、未松开就按新键会先释放旧键、
13 个键逐个按下/松开都干净结束、长按后松开干净、快速交替后干净、
**任一侧断线后当前按键被清零**、两侧同时断线仍干净、ATVV 语音按下/松开正确。

自检的价值已经被验证过两次——它抓出了两个编译和宿主模型都发现不了的问题：

| 问题 | 为什么宿主模型没抓到 |
| --- | --- |
| 生成器把向量的填充位放在了真实期望值**前面**，导致所有解析/状态机断言错位 | 宿主模型读的是 JSON，不读生成的 C 头文件 |
| `keymap_lookup_ex()` 在 `resolve_dynamic()` 返回 NONE 时**错误地回落到静态表**，使 `map voice disabled` 失效 | Python 模型直接返回，实现正确，因此"向量正确"不等于"C 代码正确" |

这正是把 L2 与 L3 分开的理由：L2 验证**规则和表格**，L3 验证**编译进去的那份 C 代码**。

## 4. 实机联调记录：三个只在真机上暴露的问题

### 4.1 板子无法启动（bootloader 读 flash 返回全 0xFF）

首次烧录后芯片复位循环：

```
E (35) flash_parts: partition 0 invalid magic number 0xfecd
E (35) boot: Failed to verify partition table
E (35) boot: load partition table error!
```

**失败发生在应用被加载之前**，所以与固件本身无关。排查与证据：

| 检查 | 结果 |
| --- | --- |
| flash 里的 bootloader 是否为我们编译的那份 | **逐字节相同**（`flash[0:19536] == 本地 bootloader`）|
| ROM 加载的段地址 | 三个 `load:` + `entry 0x403cbf10` 与我们 bootloader 的段表**逐项一致**（ESP32-C3 段表从文件偏移 24 开始）|
| 分区表内容 | 0x8000 = `aa 50 01 02 00 90 00 00`，magic `0x50AA` **正确** |
| Secure Boot / Flash Encryption | 均为 **Disabled**（排除加密导致读回乱码）|
| bootloader 读 0x8000 | 全片擦除后读到 **`0xFFFF`**，而 esptool 回读同一地址是正确的 |

**根因**：`boards.txt` 里

```
esp32c3.menu.FlashMode.qio.build.flash_mode=dio   ← 镜像头始终 DIO（ROM 用它加载）
esp32c3.menu.FlashMode.qio.build.boot=qio         ← 但默认把 flash 驱动配成 QIO
esp32c3.menu.FlashMode.dio.build.boot=dio
```

默认 FQBN 下镜像头写 DIO、驱动却是 **QIO**。本板的 Macronix flash 在 QIO 下不回应，
于是 bootloader 的每一次 flash 读都拿到 `0xFF`。单变量对比确认：

| 配置 | 结果 |
| --- | --- |
| 80MHz + QIO（FQBN 默认） | ❌ 复位循环 |
| 40MHz + QIO | ❌ 复位循环（读回 `0xFFFF`）|
| 80MHz + **DIO** | ✅ 正常启动 |

**处理**：`scripts/_common.ps1` 把 FQBN 固定为 `esp32:esp32:esp32c3:FlashMode=dio`。
手工编译也必须带上这个选项。

### 4.2 按媒体键会让固件崩溃（库层缺陷）

启动正常后，`selftest` 的分发仿真在第一个音量键上报 panic：

```
Guru Meditation Error: Core  0 panic'ed (Load access fault).
```

用 `riscv32-esp-elf-addr2line` 解析 ELF 得到调用链：

```
bridge::loop() -> handleEvent() -> hid_server::sendConsumerReport()
  -> BLECharacteristic::notify()            (BLECharacteristic.cpp:1065)
  -> BLEService::getServer()                (BLEService.cpp:336)   <-- fault
```

原因是 Arduino-ESP32 3.3.11 的 BLE 封装层**无法注册两个同 UUID 的特征**：

1. HOGP 把每个 Report ID 映射到一个 Report 特征，两个 ID ⇒ 两个同为 `0x2A4D` 的特征；
2. `BLEService::addCharacteristic()` 检测到重复 UUID 后**不把第二个写进服务映射表**；
3. 于是它既进不了 GATT 表，也永远不会被调用 `executeCreate()`；
4. `BLECharacteristic` 构造函数**不初始化 `m_pService`**，只有 `executeCreate()` 会赋值；
5. `notify()` 里 `getService()->getServer()` 读到野指针 → 崩溃。

库自带的 `Server_Gamepad` 示例只创建了**一个** input report，所以上游从未走过这条路径。

**处理**：改成**单个输入报告（Report ID 1，10 字节）承载两个顶层集合**
（8 字节键盘 + 2 字节 Consumer）。共用 Report ID 是合法 HID，并且只用公开 API，
不需要修改仓库外的核心库。同时把"恰好一个输入报告、不得声明 Report ID 2"
写进 `tests/model/check_vectors.py` 作为回归防护。详见 `docs/KEYMAP.md` §5。

> 注意：这个缺陷本来会影响**正常使用**（按音量键就崩），不只是自检。

### 4.3 曾经的串口阻塞（已解决）

早期 COM3 是 CH343（VID `0x1A86` / PID `0x55D3`），PnP 报"工作正常"但端口打不开
（`ERROR_GEN_FAILURE`），`.NET SerialPort.Open()` 同样失败。后经用户处理后恢复，
现已能正常探测、烧录、读日志。

### 4.4 连接一个不在广播的设备要等 60 秒（库层陷阱）

现象：`connect c0:5d:39:xx:xx:xx` 之后，日志停在

```
[151248][RC     ] state -> CONNECTING (console connect)
[151260][RC     ] connecting to c0:5d:39:xx:xx:xx (type 0)...
```

然后**十几秒没有任何输出**，`status` 仍显示 `CONNECTING`。看起来像死锁。

根因在库里，不在本固件：

```cpp
// BLEClient.cpp
rc = ble_gap_connect(BLEDevice::m_ownAddrType, &peerAddr_t, m_connectTimeout, ...);
//                                                          ^^^^^^^^^^^^^^^^
```

`BLEClient::connect(addr, type, timeoutMs)` 的**第三个参数被完全忽略** —— 传给 `ble_gap_connect()`
的是私有成员 `m_connectTimeout`，本库版本默认 **30000 ms**，而且**全库没有 `setConnectTimeout()`**
（`grep -rn setConnectTimeout` 无匹配，因此也没有公开途径改它）。

叠加我在 `connectToAddress()` 里写的"失败后换地址类型再试一次"兜底，最坏就是 **30 s × 2 = 60 s**。

**处理**：

- 删掉那个不起作用的 `BRIDGE_DIRECT_CONNECT_MS` 参数，改用 `BRIDGE_CONNECT_LIB_TIMEOUT_MS`
  把库的真实超时记录下来，并在日志里明写 `may take up to 30 s`——否则这段静默和死锁无法区分；
- 换地址类型重试**只在目标是 random 地址时**才做（公开地址不会换类型，重试纯属浪费 30 s）；
- `connect <mac>` 时若该地址不在上次扫描结果里，先打印警告。

另外把"人工选择"做成了一等路径：`scan` 输出带序号，`connect <index>` 直接按序号连，
并把扫描到的地址类型一并带上。

## 5. L4 实机验收清单（部分完成）

按顺序做完并回填。每条都有"怎么判"和"期望结果"。

### 5.1 基础链路

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 1 | 烧录 | `.\scripts\flash.ps1 -Port COM3` | `UPLOAD OK`，串口出启动横幅 | ✅ 2026-09-10 |
| 2 | 上游配对 | `scan` → `connect <index>`（人工选择） | `ready: N subscription(s)` | ✅ 秒连，见下 |
| 3 | 下游配对 | Windows 蓝牙添加 `Mi Remote Bridge` | 日志 `host connected` 变 1 | ☐ **待做** |
| 4 | Windows 识别 | 设置 / 设备管理器 | 出现"键盘"+ 消费类控制设备 | ☐ **待做** |
| 5 | 设备端自检 | `selftest` | `RESULT: PASS` | ✅ 见 §3 |

上游配对的实测日志（`connect` 发出后约 270 ms 建链）：

```
[151526][RC     ] gatt link established
[151529][NVS    ] saved remote Xiaomi BT Remote (c0:5d:39:xx:xx:xx) type=0
[153107][SEC    ] link encrypted (bonded=1, authenticated=0)
[153675][GATT   ] discovered 9 service(s)
[155226][GATT   ] service 00001812-0000-1000-8000-00805f9b34fb      <- HOGP
[157313][GATT   ] subscribed to HID report 00002a4d-0000-1000-8000-00805f9b34fb
[157313][GATT   ] protocol mode set to Report (0x01)
[157365][GATT   ] ready: 1 subscription(s), notifications live
```

设备：地址 `c0:5d:39:xx:xx:xx`（**公开地址**），广播名「小米蓝牙语音遥控器」，RSSI −35…−48 dBm。

### 5.2 13 个按键逐个按下与松开

`raw on` 打开后逐个按。**每个键都记录原始码、转换结果、Windows 是否响应。**

§1–§4 两列已于 2026-09-10 在真机上跑完（13/13 识别，**0 未知码 / 0 WARN**）；
"Windows 实测"一列要等 §5.1 第 3 步（下游配对）完成后才能填。

| # | 物理键 | 原始码（实测） | 固件转换结果（实测） | Windows 实测 | 结果 |
| --- | --- | --- | --- | --- | --- |
| 1 | 音量 + | `0x80` | Consumer `0x00E9` | | ✅ |
| 2 | 音量 − | `0x81` | Consumer `0x00EA` | | ✅ |
| 3 | 返回 | `0xF1` | Consumer `0x0224` | | ✅ |
| 4 | 上 | `0x52` | 键盘 `0x52` | | ✅ |
| 5 | 下 | `0x51` | 键盘 `0x51` | | ✅ |
| 6 | 左 | `0x50` | 键盘 `0x50` | | ✅ |
| 7 | 右 | `0x4F` | 键盘 `0x4F` | | ✅ |
| 8 | 确定 | `0x28` | 键盘 `0x28` | | ✅ |
| 9 | 主页 | **`0x4A`** | LSUI + `0x07`（Win+D） | | ✅ |
| 10 | 菜单 | **`0x65`** | 键盘 `0x2C`（Space） | | ✅ |
| 11 | 电视 | **`0x35`** | 键盘 `0x41`（F8） | | ✅ |
| 12 | 电源 | `0x66` | LALT + `0x3D`（Alt+F4） | | ✅ |
| 13 | 语音 | **`0x3E`** | RALT + `0x36`（RAlt+,） | | ✅ |

**加粗的 4 个是"第三方记录里写错、实测纠正"的**：`0x4A`/`0x65`/`0x35`/`0x3E` 原本被列为备用码。
已按实测值改主码，并把第三方那套留作同义词。详见 `KEYMAP.md` §1。

**若某个键的"原始码（实测）"与 `KEYMAP.md` 不同**，把实测码补进
`key_definitions.h` + `keymap.cpp` 后重新编译烧录。

### 5.3 边界与恢复

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 14 | 音量加/减/返回 | 确认这三个非标准码真的送到了 Windows | 系统音量变化 / 浏览器返回 | ☐ |
| 15 | 长时间按住 | 按住音量键 10 秒以上 | 只发一次按下（不连发），松开后立即停止 | ☐ |
| 16 | 快速连续按键 | 1 秒内连按 10 次 | 每次都正确，无丢键、无卡键 | ☐ |
| 17 | RC003 休眠后唤醒 | 静置到遥控器休眠，再按键唤醒 | 自动重连并**重新订阅**（日志有 `ready: ...`），按键恢复 | ☐ |
| 18 | C3 断电重启 | 拔插 C3 | 两侧都自动恢复；优先直连已保存地址 | ✅ 上游侧已验证，见下 |
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
| 固件内延迟（仿真） | 通知回调 → `hid_server` 完成 notify | **min 19 / median 26 / max 203 µs** |
| 固件内延迟（真机 13 键） | 同上，每键一次实测 | **min 190 / median 215 / max 361 µs** |
| 端到端主观延迟 | 按键到 Windows 响应（可用在线键盘测试页面观察） | ☐ 待下游配对后实测 |

**真机 13 次的全部采样**（µs）：`190 198 198 203 212 213 215 216 219 219 222 229 361`。

两条数值都属于 `event_bus::post()` → `hid_server` 完成 `notify()` 这一段，**不含**空口
传输、Windows HID 栈处理与 USB/蓝牙栈的排队时间 —— 所以"体感延迟"会比这个大，两者不要混为一谈。

真机的 190–361 µs 比仿真大一个量级，是正常的：仿真跑在 `loop()` 里空转的情境，真机采样
覆盖了 `loop()` 正在处理其它事件、以及 BLE 主机任务抢占的情况。**最大值 361 µs 也没有达到
让人感知的程度**（人眼可分辨的按键延迟阈值在 10 ms 量级）。

注意：固件内延迟**不包含**空口传输、Windows HID 栈处理和应用响应时间。
测量值只代表"固件没有引入额外延迟"。

---

### 5.5 上游通道的实测细节（含未解决项）

#### 5.5.1 断电重启后的自动恢复 ✅

拔插 C3（烧录后复位）后的完整时序，**没有走扫描**，直接用 NVS 里保存的地址直连：

```
[  436][RC     ] state -> DIRECT (boot)
[  437][RC     ] bound remote: c0:5d:39:xx:xx:xx ("Xiaomi BT Remote")
[  457][RC     ] state -> CONNECTING (direct connect to bound address)
[ 3927][RC     ] gatt link established
[ 4776][GATT   ] discovered 9 service(s)
[ 9339][GATT   ] subscribed to HID report 00002a4d-0000-1000-8000-00805f9b34fb
[ 9392][GATT   ] ready: 1 subscription(s), notifications live
[ 9392][RC     ] state -> READY (subscribed)
```

**从加电到 READY 共 9.4 秒**（其中建链 3.5 s，服务发现与订阅 5.5 s）。

#### 5.5.2 ATVV 控制通道在本机没有订阅成功 ⚠️

需求里希望顺带订阅 ATVV 控制特征（`ab5e0004`）来观察语音键，但实测**只建立了 1 个订阅**，
即 HOGP 报告 `0x2A4D`；日志既没有 `subscribed to ATVV control`，也没有 `subscribe failed on
ATVV control` —— 说明该特征在服务发现阶段就**没有出现**在 `ab5e0001` 服务的特征表里。

伴随两条 NimBLE 报错：

```
E (10011) NimBLE: ble_att_clt_tx_read_type rc=3     <- 探测 fe59 服务时
E (10023) NimBLE: ble_att_clt_tx_read_type rc=3     <- 探测 ab5e0001 服务时
```

**未解决项**：这两条错误的根因没有追到底。它们是**非致命**的 —— 建链、发现、订阅、READY 全部照常完成，
13 个键也全部工作。判断为"对远端不提供的属性发起读取、被设备以 ATT 错误拒绝"，属于噪音而非故障。

**为什么这不影响语音键**：语音键在本机走的是 HOGP 报告路径，实测原始码 `0x3E`，映射到 `RAlt+,`。
ATVV 通道只是**另一条**可能上报语音键的路径（对应同义词 `0x04`），两条路互不依赖。

#### 5.5.3 一个待观察的现象：`bonded` 标志

首次配对时日志为 `link encrypted (bonded=1)`，重启重连后为 `link encrypted (bonded=0)`，
但 `status` 里 `bonds: 1` 且链路加密正常、按键正常。

可能是时序问题（该行在绑定的标志位落定之前打印），也可能确实发生了一次重新配对。
**目前没有证据表明它造成任何功能问题**，因此按"待观察"记录，不写成已解决。
重启 N 次后若 `bonds` 始终为 1 且无需遥控器重新进入配对模式，即可判定为时序假象。

---

## 6. 回归

每次改动后：

```powershell
.\scripts\test.ps1     # L2 + L1 一起跑
```

实机可用时再加 `selftest`。若改动涉及 BLE 参数或时机，必须重跑 §5.3 的全部项目。
