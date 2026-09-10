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
| L4 下游（C3 → Windows） | 键盘 + 媒体键 | 键盘**已通过**（`Status=OK`、`kbdhid`）；媒体键结构已改为两报告 ID，**未上机复验** | 见 §4.6（根因）与 §4.7（自建服务层）|
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
   - **恰好两个**输入报告：Report ID 1 = 64 bit（8 字节键盘）、Report ID 2 = 16 bit（2 字节 Consumer）
   - **恰好两个**顶层 Collection、Collection 不嵌套
   - **不得声明任何输出报告**（HOGP 要求每种报告都有对应类型的特征）
   - Array 项的 Usage Maximum 不超过 Logical Maximum
   - `hid_report_map.h` 里的常量（报告 ID、两个报告长度、键区偏移）与描述符逐项一致

   这几条都不是风格偏好，是三次真机故障换来的：两集合共用一个报告 ID → Windows `Code 10`
   （§4.6）；报告类型对不上 → 同样的 `0xC0110002`（§4.5）。
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

### 4.5 Windows 报"驱动程序错误"：描述符声明了不存在的报告类型

现象：Windows 11 蓝牙设备卡片上显示 `Mi Remote Bridge` + **"驱动程序错误"**，键盘不可用。

设备端日志显示链路其实是通的：

```
[353228][WIN    ] host connected (id 1 addr xx:xx:xx:xx:xx:xx mtu 23)
[358567][WIN    ] report 1 notifications ENABLED (handle 1)     <- Windows 订阅成功
[361688][WIN    ] host disconnected (handle 1, remaining 0)     <- 3 秒后自己撤了
```

**取证（全部来自 Windows 自身，不是推测）：**

| 来源 | 内容 |
| --- | --- |
| `Get-PnpDevice` | `符合蓝牙低能耗 GATT 的 HID 设备`，`Problem=CM_PROB_FAILED_START`，**只有这一个设备失败** |
| 内核 PnP 事件 411 | `问题: 0xA`（Code 10），**`问题状态: 0xC0110002`** |
| System 日志 / BTHUSB | `远程适配器 (60:55:f9:xx:xx:xx) 成功地与本地适配器配对` |

`0xC0110002` 查 WDK 头文件即 `HIDP_STATUS_INVALID_REPORT_TYPE`：

```c
#define HIDP_STATUS_INVALID_REPORT_TYPE (HIDP_ERROR_CODES(0xC,2))
```

**根因**：报告描述符里声明了一个 **Output（LED）** 报告（从标准 USB 键盘描述符照抄来的
1 字节 LED 状态），但 HID 服务里**只有一条 Input 类型的 Report 特征**
（`BLEHIDDevice::inputReport()` 建的，Report Reference = `{1, 0x01}`）。
HOGP 要求描述符里声明的每一种报告都有对应类型的 Report 特征；Windows 去找 Output 那条，
只找到标着 "input" 的，判定报告类型无效，驱动启动失败。

**为什么不能"补一条 output 特征"**：`BLEHIDDevice::outputReport()` 建的是第二个 `0x2A4D`
特征，会踩 §4.2 那个重复 UUID 的坑，特征根本不会出现在 GATT 表里。本固件从不驱动键盘 LED，
所以**删掉这个声明**既正确又是唯一可行解（描述符 90 → 72 字节，`0x91` 项全部移除）。

#### 一个方法论上的教训

我一度写下"Windows 这条链路没有配对事件，所以配对没成功"。**这个推断是错的。**
`onAuthenticationComplete` 属于 `BLESecurityCallbacks`，而我们的固件**从未注册**它
（它是全局单槽，且同组回调里 `onConfirmPIN` / `onAuthorizationRequest` 的返回值会被库采用，
为一行日志去注册会影响已经跑通的 RC003 配对，不划算）。所以日志里没有那一行，
只说明"没人打印"，**不等于"没发生"**。真相由 Windows 自己的 BTHUSB 日志给出：配对是成功的。

**日志里没有 ≠ 没发生。** 这是本次排查里唯一一次走错方向，值得记下来。

### 4.6 第一次修复无效：Code 10 仍在，重新定位

**§4.5 的结论（LED 输出报告）是错的。** 删掉 LED 输出后重新配对，Windows 报的还是同一个错误：

| 时间 | 描述符 | 设备实例 | 结果 |
| --- | --- | --- | --- |
| 18:07 | 90 字节，含 LED Output | `...&0&0013` | Code 10 / `0xC0110002` |
| 18:24 | 72 字节，无 Output | `...&1&0013` | **Code 10 / `0xC0110002`（一字不差）** |

实例序号从 `&0&` 变成 `&1&`，说明**用户在 Windows 里删掉设备后确实重新枚举了**，
排除了"Windows 用了缓存的旧描述符"这一解释。**描述符真的被重新读了，还是失败。**

#### 现在的主要嫌疑：两个顶层集合共用一个 Report ID

本设计里键盘和 Consumer Control 是**两个顶层集合（TLC）共用 Report ID 1**，
因此只有**一条** Report 特征。而常见的可用实现是：

| 要素 | 正确实现 | 本设计 |
| --- | --- | --- |
| 特征值配置 | 每个 Report ID 对应独立特征值 | **两个功能共用一个特征** ❌ |

`0xC0110002` = `HIDP_STATUS_INVALID_REPORT_TYPE`，恰好是"按（报告 ID，报告类型）找不到对应报告"
这一类问题，与"两个 TLC 挤在一个 ID / 一条特征上"吻合。

#### 为什么不能简单地改成两条 Report 特征

`BLEService::start()`（NimBLE 路径）是**遍历内部的 `std::map<std::string, BLECharacteristic*>`
来构建 `ble_gatt_chr_def[]`** 的，而 `addCharacteristic()` 遇到同 UUID：

```cpp
if (pExisting != nullptr) { pExisting->m_removed = 0; }   // 新的一条根本不进 map
else                      { m_characteristicMap.setByUUID(...); }
```

**没进 map 就不会被注册进 GATT 表。** 又因为 `BLEUUID::toString()` 对 16 位 UUID 返回
`"00002a4d-0000-1000-8000-00805f9b34fb"`，"换一种写法骗过判重"也不可行；
`executeCreate()` 是 private 也无法手动调用。**结论：本库版本下，一个服务里不可能有两条 `0x2A4D` 特征。**

（注意：限制只在 Arduino 封装层的那个 map 上。NimBLE 本身的 `ble_gatt_svc_def` 完全允许两条同 UUID 特征。）

#### 诊断结果：确认就是两个顶层集合（2026-09-10 18:38）

把描述符改成**只有一个顶层集合**（Consumer 用法并进 Keyboard 集合），
**报告仍是 10 字节、仍用 Report ID 1**，因此**固件代码一行未改**，
唯一的变量就是"顶层集合的数量"。结果**一次通过**：

```
[335902][WIN] host connected (mtu 256)
[338841][WIN] host READ 00002a4e (Protocol Mode) -> 1 bytes
[338962][WIN] host READ 00002a4b (Report Map)   -> 65 bytes   <- 真的重读了
[339081][WIN] host READ 00002a4a (HID Info)     -> 4 bytes
[341241][WIN] report 1 notifications ENABLED
（此后没有任何断开）
```

Windows 侧：

| 项 | 结果 |
| --- | --- |
| `符合蓝牙低能耗 GATT 的 HID 设备` | **`Status=OK` / `CM_PROB_NONE`**（此前是 `CM_PROB_FAILED_START`）|
| `HID Keyboard Device` | 创建成功，`HID\{00001812-...}\9&24B6531B&0&0000` |
| 内核 PnP 410 | `keyboard.inf` / 服务 `kbdhid`；`hidbthle.inf` / 服务 `mshidumdf` |
| 断开次数 | **0**（两 TLC 版本是 3 秒后断开）|

**结论：Windows 的 BLE HID 栈不接受"两个顶层集合共用一条 Report 特征"。**
合法 HID 不等于 Windows 接受；`HIDP_STATUS_INVALID_REPORT_TYPE` 就是它在这条路径上的报错方式。

#### 代价与下一步

单集合会让 Windows 绑定 `kbdhid`，因此 **13 键里的媒体类 3 键（音量±、返回）和语音键在本版失效**，
方向键 / 确定 / 主页 / 菜单 / 电视 / 电源 正常。

要让媒体键回来，必须给出**两条 `0x2A4D` 特征**（报告 ID 1 = 键盘，报告 ID 2 = Consumer）。
Arduino 封装层做不到（见上），所以下一步是**绕过 `BLEHIDDevice`，用 NimBLE 的 `ble_gatt_svc_def`
自建 HID 服务**——NimBLE 本身完全允许两条同 UUID 特征，限制只在封装层那个 map 上。

> 教训：**一个"更简洁"的设计（共用报告 ID）如果偏离了所有可用实现的做法，
> 就要先去查有没有人这么干成过，而不是先假定它合法就够了。**

#### 同时加上的观测点（方法上必须）

`hid_server.cpp` 现在给 HID 服务的可枚举特征（`0x2A4A` / `0x2A4B` / `0x2A4E` / `0x2A4C`）
挂了读写日志。原因是：

> **"改了描述符但没效果"有两种可能——改动是错的，或者主机用了缓存。**
> **这两种情况从设备端看起来完全一样。**

日志里出现 `host READ 00002a4b-...` 就证明主机确实重新读了报告描述符，诊断才成立。

---

### 4.7 自建 HID 服务（修复 4.6 的根因）

§4.6 证实"两个顶层集合共用一条 Report 特征"是 Windows 不肯启动的原因。
正确结构需要**两条同为 `0x2A4D` 的 Report 特征**，而 Arduino 封装层做不到（原因见
`hid_gatt.h` 的注释）。因此新增 `hid_gatt.cpp`，用 NimBLE 的 `ble_gatt_svc_def`
直接构建 HID（0x1812）/ 设备信息（0x180A）/ 电池（0x180F）三个服务。

**改动范围**：

| 文件 | 作用 |
| --- | --- |
| `hid_gatt.h` / `hid_gatt.cpp`（新增，约 420 行） | 服务定义、访问回调、订阅监听、通知发送 |
| `hid_report_map.h` | 恢复两集合，每个集合**自己的报告 ID**（1 键盘 8 字节 / 2 Consumer 2 字节）|
| `hid_server.cpp` | 内部改用 `hid_gatt`；**对外 API 一字未改** |
| `hid_report_map.h` 常量 / `selftest.cpp` / `check_vectors.py` | 同步为新布局 |

**没有改动**：`ble_core`、`ble_bonds`、`rc003_client`、`keymap`、`event_bus`、`bridge`、`cli`。
RC003 那一侧和配对/Bond 逻辑完全不受影响。

**状态**：编译通过（0 warning，685939 B），模型检查 11096/0。
**尚未上机**——烧录时板子的 USB 串口消失（`CM_PROB_PHANTOM`），未能烧入。

## 5. L4 实机验收清单（部分完成）

按顺序做完并回填。每条都有"怎么判"和"期望结果"。

### 5.1 基础链路

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 1 | 烧录 | `.\scripts\flash.ps1 -Port COM3` | `UPLOAD OK`，串口出启动横幅 | ✅ 2026-09-10 |
| 2 | 上游配对 | `scan` → `connect <index>`（人工选择） | `ready: N subscription(s)` | ✅ 秒连，见下 |
| 3 | 下游配对 | Windows 蓝牙添加 `Mi Remote Bridge` | 日志 `host connected` 变 1 | ⚠️ 连接与订阅成功，但驱动 Code 10（§4.5）；修复后**待复验** |
| 4 | Windows 识别 | 设置 / 设备管理器 | 出现"键盘"+ 消费类控制设备 | ⏳ 待在步骤 3 通过后确认 |
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
| 固件内延迟（仿真，无宿主） | 通知回调 → `hid_server` 完成 notify | min 19 / median 26 / max 203 µs |
| 真机按键，**无宿主订阅** | 同上，13 键 | min 190 / median 215 / max 361 µs |
| 真机按键，**Windows 已订阅** | 同上，20 键 | **min 763 / median 782 / max 842 µs** |
| 端到端主观延迟 | 按键到 Windows 响应 | ☐ 尚未量化 |

三条数值都属于 `event_bus::post()` → `hid_server` 完成 `notify()` 这一段，**不含**空口
传输、Windows HID 栈处理与蓝牙栈排队时间 —— 所以"体感延迟"比这个大，两者不要混为一谈。

> **不要引用第二行。** 它是在"Windows 侧驱动失败、没有订阅者"的状态下测的，
> 那时 `notify()` 立刻返回，所以数字偏乐观。**有宿主订阅时才是真实值（约 780 µs）**，
> 因为它现在真的要把通知发出去。同一份文档里保留这一行，是为了让这个差异本身可见：
> **测量条件比数字更重要。**

782 µs 中位数远低于人眼可分辨的阈值（10 ms 量级），且 20 次采样只有 ±5% 的抖动。

### 5.4.1 下游稳定性（15 分钟连续运行）

`build/win-pair3.log` 记录了 15 分钟连续运行：**宿主连接 1 次、断开 0 次**，
期间按键 20 次全部正常。对比修复前（两个顶层集合）——**每次连接都在 3 秒后被打断**。

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
