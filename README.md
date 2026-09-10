# MiRemoteBridge

ESP32-C3 双角色 BLE 桥接固件：把**小米蓝牙遥控器 2 Pro（RC003）**的按键，转成 Windows 能直接
使用的**标准蓝牙键盘 + 媒体控制设备**。

> 只处理按键。不涉及遥控器麦克风与音频，不实现 USB HID / USB UAC、Windows 伴侣程序、
> 虚拟声卡、内核驱动或注入。

```
┌──────────────────────────┐   BLE 5.0 (HOGP + ATVV 控制通道)   ┌──────────────────────────┐
│  小米蓝牙遥控器 2 Pro    │ ─────────────────────────────────► │   ESP32-C3               │
│  (RC003)                 │ ◄───────────────────────────────── │   Central + Peripheral   │
└──────────────────────────┘                                    └────────────┬─────────────┘
                                                                             │ BLE 5.0
                                                      固定容量事件队列        │ (HID Keyboard +
                                                      + 键码转换             │  Consumer Control)
                                                                             ▼
                                                                ┌──────────────────────────┐
                                                                │   Windows                │
                                                                │   · 标准蓝牙键盘          │
                                                                │   · 标准媒体控制设备      │
                                                                │   免驱，无需任何软件      │
                                                                └──────────────────────────┘
```

## 为什么需要它

RC003 把私有键码塞在 HID 报告的按键槽里：音量是 `0x80` / `0x81`，返回是 `0xF1`。
这些不是合法 HID Usage，Windows 的 `kbdhid.sys` 会在内核层直接丢弃，应用层
（全局 Hook、RawInput、AutoHotkey）都拿不到。本固件在空口完整收到这些码，转换成标准
HID 报告后再以蓝牙键盘的身份发出去，因此**免驱、不改系统、不影响反作弊**。

---

## 当前状态（重要）

| 层级 | 状态 |
| --- | --- |
| L1 编译验证 | ✅ 通过（0 warning，flash 52% / RAM 6%） |
| L2 宿主端模型验证 | ✅ 通过（11095 项断言，含 4000 步随机不变量测试） |
| L3 设备端自检（`selftest`） | ✅ **真机通过**：`137 passed / 0 failed` + 分发仿真 `44 passed / 0 failed` |
| L4 上游 RC003 → C3 | ✅ **真机通过**：13/13 键识别，**0 未知码 / 0 WARN**；固件内延迟 min 190 / median 215 / max 361 µs |
| L4 下游 C3 → Windows | ⏳ **未验证**：等 Windows 蓝牙配对 `Mi Remote Bridge` |
| L4 边界与恢复（长按/连按/休眠/重启/卡键） | ⏳ 未验证 |

**13 个按键的原始码已在本机 C3 + 真实 RC003 上逐键实测确认**，并纠正了 4 个第三方记录里写错的码
（详见 [`docs/KEYMAP.md`](docs/KEYMAP.md) §1）。完整的 24 项验收清单在
[`docs/TESTING.md`](docs/TESTING.md) §5。

**固件已在真实 ESP32-C3 上烧录并运行**（MAC `60:55:f9:xx:xx:xx`，bootloader → app 正常启动，
串口控制台可用）。端到端（RC003 ↔ C3 ↔ Windows）的按键验收还没做，清单在
[`docs/TESTING.md`](docs/TESTING.md) §5。

### 这块板子必须用 `FlashMode=dio`

默认 FQBN 会构建成"镜像头写 DIO、驱动用 QIO"，**本板卡在这种配置下无法启动**：
bootloader 读分区表返回全 `0xFF`，报 `partition 0 invalid magic number` 并复位循环。
实机对比确认：

| 配置 | 结果 |
| --- | --- |
| 80MHz + QIO（FQBN 默认） | ❌ 复位循环 |
| 40MHz + QIO | ❌ 复位循环（读回 `0xFFFF`）|
| 80MHz + **DIO** | ✅ 正常启动 |

所以 `scripts/_common.ps1` 里的 FQBN 固定为 `esp32:esp32:esp32c3:FlashMode=dio`。
手工编译时也要带上这个选项，细节与取证过程见 [`docs/TESTING.md`](docs/TESTING.md) §4。

---

## 硬件与工具链

| 项 | 值 |
| --- | --- |
| 目标芯片 | ESP32-C3 rev v0.3，4MB Macronix flash（`esp32:esp32:esp32c3:FlashMode=dio`）|
| 已验证硬件 | COM3 / CH343 USB 转串口，MAC `60:55:f9:xx:xx:xx` |
| Arduino CLI | 1.5.1，`C:\code\MiRemoteBridge\.tools\arduino-cli-1.5.1`（不入 Git） |
| Arduino-ESP32 | 3.3.11，数据目录 `C:\code\arduino-c3-data`（不入 Git） |
| BLE 主机栈 | **NimBLE**（该核心为 C3 默认且唯一构建的栈：`CONFIG_BT_NIMBLE_ENABLED=y`，Bluedroid 未编译） |

> 工具链刻意放在**无中文路径** `C:\code\arduino-c3-data`：乐鑫的 Windows RISC-V
> 链接器不能可靠处理中文路径。不要把工具链移回本仓库。仓库已改名为
> `C:\code\MiRemoteBridge`，配置里的外部路径保持不变。

---

## 快速开始

```powershell
# 1. 编译
.\scripts\build.ps1

# 2. 看看接了哪些串口（不给 -Port 时只检测，绝不会烧录）
.\scripts\flash.ps1

# 3. 用显式端口确认并烧录（会先只读探测芯片，不是 ESP32-C3 就中止）
.\scripts\flash.ps1 -Port COM3

# 4. 打开串口控制台
.\scripts\monitor.ps1 -Port COM3
```

板子如果只有原生 USB 口、没有 USB 转串口芯片：加 `-CdcOnBoot`。

不需要硬件也能跑的全部检查：

```powershell
.\scripts\test.ps1
```

如果脚本在你的机器上"什么都不做"，先看 `docs/TESTING.md` 的排查小节：
PowerShell 5.1 在环境里同时存在 `http_proxy` 与 `HTTP_PROXY` 时无法启动子进程。
`scripts/_common.ps1` 已经自动处理这种情况。

---

## 控制台命令

在串口里输入 `help`。常用：

| 命令 | 作用 |
| --- | --- |
| `status` | 全量状态：两侧连接、绑定、通知计数、队列、当前按键、延迟开关 |
| `scan` / `scan now` | 查看扫描器缓存 / 开始新一轮扫描 |
| `connect <mac> [public\|random]` | 手动连接指定设备 |
| `reconnect` | 断开并重连 RC003 |
| `forget rc` / `forget win` | 清除遥控器侧 / Windows 侧的配对 |
| `bond list` | 列出所有配对记录 |
| `key <hex> press\|release` | 注入合成按键（验证下游 HID 通道） |
| `channel consumer <hex>` | 直接发一个 Consumer Usage |
| `raw on\|off` / `lat on\|off` / `log <0-4>` | 日志控制 |
| `map back\|power\|voice <模式>` | 改运行时键位，保存在 NVS |
| `selftest` / `sim` | 设备端自检（含分发仿真） |
| `factory` | 清除全部配对与设置并重启 |

---

## 架构

```
firmware/MiRemoteBridge/
├── MiRemoteBridge.ino   入口：setup / loop
├── config.h             所有编译期开关与时机参数
├── key_definitions.h    RC003 原始键码 + 标准 HID Usage（纯 C，无依赖）
├── keymap.{h,cpp}       键码 → HID 动作映射 + 运行时可选模式（纯 C，无依赖）
├── rc003_report.{h,cpp} 报文解析 + 按下/松开状态机（纯 C，无依赖）
├── event_queue.h        固定容量无锁 SPSC 环形队列
├── bridge_events.h      事件类型定义
├── event_bus.{h,cpp}    全局事件队列实例
├── ble_core.{h,cpp}     唯一的 BLE 栈初始化点
├── ble_bonds.{h,cpp}    Bond 查询/删除（补齐封装层缺口）
├── hid_server.{h,cpp}   下游：HID 外设（键盘 + Consumer），防卡键
├── rc003_client.{h,cpp} 上游：扫描/连接/服务发现/订阅，独立 FreeRTOS 任务
├── bridge.{h,cpp}       事件分发、按键状态、延迟测量
├── log.{h,cpp}          分级日志 + 令牌桶限速
├── cli.{h,cpp}          串口控制台
└── selftest.{h,cpp}     设备端向量测试 + 分发仿真
```

### 关键设计决定

**单一 NimBLE 主机栈，双角色并存。** Arduino-ESP32 3.3.11 的 ESP32-C3 构建里
`CONFIG_BT_NIMBLE_ENABLED=y` 且 Bluedroid 根本没编译，所以核心自带的 `BLE` 封装库
天然只走 NimBLE。`ble_core::begin()` 是**唯一**调用 `BLEDevice::init()` 的地方，
防止重复初始化并把两套栈混在一起。

**BLE 回调只入队，不发 HID 报告。** 上游通知回调运行在 NimBLE host 任务里；
它只做「解析 → 归一化 → 推入 SPSC 队列」。HID 报告全部由 Arduino `loop()` 发送，
两条角色不会在 host 任务里互相重入。

**阻塞操作放在独立任务里。** 扫描、连接（最长 8 秒）、服务发现、订阅都会阻塞，
因此全部由 `rc003` FreeRTOS 任务承担，`loop()` 保持随时能处理按键。

**连接策略。** 优先用保存的身份地址 + Bond 直连（试 2 次），失败才回退扫描；
扫描是占空比的（连续 20 秒 → 停顿 3 秒），给同时存在的 Windows 链路留出空口时间，
也让串口日志保持可读。**每次重连后都重新做服务发现并重新订阅。**

**防卡键。** 三个层面：`pressAction`/`releaseAction` 幂等；HID 报告每次从"当前按下的集合"
重建；任一侧断线（含配对失败、主机刚连上、遥控器刚就绪）都立即 `releaseAll()` 把
键盘与 Consumer 报告同时清零。详见 [`docs/RECOVERY.md`](docs/RECOVERY.md) §3。

**未知键码宁可漏发。** 不在表内的码直接丢并记 WARN，不会当成 HID Usage 发出去。

---

## 按键表（13 键，摘要）

| 物理键 | 原始码 | 默认输出 |
| --- | --- | --- |
| 音量 + / − | `0x80` / `0x81` | Consumer Vol Up / Down |
| 返回 | `0xF1` | Consumer AC Back（可改 Esc / Alt+←） |
| 上 / 下 / 左 / 右 | `0x52` / `0x51` / `0x50` / `0x4F` | 方向键 |
| 确定 | `0x28` | Enter |
| 主页 | `0x24` | Win + D |
| 菜单 | `0x5D` | 空格 |
| 电视 | `0xC0` | F8 |
| 电源 | `0x66` | Alt + F4（可改 Sleep / Power / Esc） |
| 语音 | `0x04` / `0x3E` | 右 Alt + 逗号（可关） |

完整说明、别名码与未知码处理见 [`docs/KEYMAP.md`](docs/KEYMAP.md)。
**注意：这些码来自第三方记录，尚未在本机实机逐个核对** —— 核对步骤见
[`docs/TESTING.md`](docs/TESTING.md) §5.2。

---

## 文档

| 文件 | 内容 |
| --- | --- |
| [`docs/PAIRING.md`](docs/PAIRING.md) | 上游/下游配对步骤、日志样例、排查表、命令速查 |
| [`docs/KEYMAP.md`](docs/KEYMAP.md) | 13 键完整表、可配置模式、HID 描述符、重复报文处理 |
| [`docs/RECOVERY.md`](docs/RECOVERY.md) | Bond 清除、故障恢复矩阵、防卡键设计 |
| [`docs/TESTING.md`](docs/TESTING.md) | 四层验证的真实状态、实机联调记录（3 个只在真机暴露的问题）、24 项验收清单、延迟测量 |
| [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md) | 第三方引用核查与许可证结论 |

---

## 测试目录

```
tests/
├── vectors/key_vectors.json    解析/状态机/键表/模式 的唯一权威向量
├── tools/gen_vectors.py        生成 firmware/MiRemoteBridge/selftest_vectors.h
└── model/check_vectors.py      宿主端模型检查 + HID 描述符解析 + 随机不变量测试
```

设备端 `selftest` 用的是从同一份 JSON 生成的 C 头文件，因此**设备与宿主不会对期望值产生分歧**。

---

## 许可证

本项目源码：MIT，见 [`LICENSE`](LICENSE)。
第三方引用核查结论见 [`docs/THIRD_PARTY_NOTICES.md`](docs/THIRD_PARTY_NOTICES.md)
（参考项目声明 MIT 但仓库内没有 `LICENSE` 文件，因此本项目只引用协议事实与常量数值，
未复制其源码）。
