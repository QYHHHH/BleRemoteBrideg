# 测试与验收

本文档把"验证"分成四层，并逐层说明**当前真实状态**。请务必区分"编译通过"和"实机通过"。

---

## 0. 当前状态总览

| 层级 | 内容 | 状态 | 证据 |
| --- | --- | --- | --- |
| L1 编译验证 | `esp32:esp32:esp32c3:FlashMode=dio,PartitionScheme=huge_app` | **通过**（2026-09-11 新版 UI 编译，非本次真机验证） | flash 1392931 B (44%)，全局 RAM 41220 B (12%)；CLI exit 0，无 stderr |
| L2 宿主端模型验证 | 解析器/状态机/键表/HID 描述符/随机不变量 | **通过** | `python tests/model/check_vectors.py` → `checks passed: 11098, failed: 0` |
| L3 设备端自检 | 在真机 MCU 上跑同一套向量 + 分发仿真 | **通过** | `selftest` → `143 passed, 0 failed` + `44 passed, 0 failed`，`RESULT: PASS` |
| L4 上游（RC003 → C3） | 扫描、直连、配对加密、服务发现、订阅通知、逐键解析 | **通过** | 13/13 键识别，0 未知码；延迟 min 190 / median 215 / max 361 µs（§5.1、§5.2）|
| L4 下游（C3 → Windows） | 键盘 + 媒体键 | **通过**（用户实测 13 键全部可用） | 两集合两报告 ID + 自建服务层（§4.6、§4.7）；蓝牙关→开自动重连 |
| L4 下游（C3 → iPhone） | iOS BLE HID | **通过**（音量键、方向键实测；iOS 订阅了键盘/Consumer/电池全部三条通知） | §4.9 |
| L4 多主机 | Windows ↔ iPhone 轮换 | **通过**（切换间隔 <0.1 s；bond 保护策略就位，第 4 台设备触发的腾位待实测） | §4.9 |
| L4 重连速度 | 重启到按键生效 | **优化至 3.3 s**（原始 9.5 s；根因与修复见 §4.8） | `build/reconnect-timing*.log` |
| L4 Web 配置界面 | Web UI + NVS 绑定持久化 | **部分**：AP/HTTP/绑定 API 在旧版页面下实测通过；页面重做后浏览器逻辑 85 项通过（§4.12），但**真机回归未通过**（§4.13：页面体积超出内存余量、AP 的 DHCP 低水位下发不出租约） | §4.11、§4.12、§4.13 |
| L4 边界与恢复 | 长按、连按、休眠唤醒、两侧重启、卡键 | **部分**：蓝牙关→开重连、长静置首按即时已验证；其余 §5.3 |
| L4 Improv 串口配网 | 公开网页（ESP Web Tools）走 Web Serial 下发 Wi-Fi 凭据 | **L1 通过，L4 未上机**：`improv_serial.{h,cpp}` + `wifi_ui::rejoin()` 已落地并编译通过（2026-09-16，零警告），**真机一项未验**；验收清单见 §5.3.2 |

已确证的硬件：ESP32-C3 rev v0.3 / 4MB Macronix flash / COM3(CH343) / MAC `60:55:f9:xx:xx:xx`；
RC003 `c0:5d:39:xx:xx:xx`（公开地址），广播名「小米蓝牙语音遥控器」。

**四个只在真机上才会暴露的问题已经定位并修复**，取证与根因写在 §4：

1. 板子无法启动（默认 QIO 下 flash 读回全 0xFF）；
2. 按媒体键崩溃（BLE 封装层注册不了两个同 UUID 的特征）；
3. `BLEClient::connect()` 的 timeout 参数被库忽略，导致连接失败要等 60 s（§4.4）；
4. 重启后按键 9.5 秒才生效（订阅顺序 + 连接参数时机 + 监督超时，§4.8）。

另有两项**机制调研结论**（§4.10）：NFC 卡是 ISO 14443-4 智能卡、碰触蓝牙零交互；
BLE 侧无任何"写 NFC 卡"的服务。

## 1. L1 编译验证（已通过）

```powershell
.\scripts\build.ps1
```

或直接：

```
arduino-cli --config-file arduino-cli.yaml compile `
  --fqbn "esp32:esp32:esp32c3:FlashMode=dio,PartitionScheme=huge_app" `
  --output-dir ./build/MiRemoteBridge ./firmware/MiRemoteBridge
```

2026-09-11 新版 Web UI 的编译输出（仅编译验证；历史真机记录见 §4）：

```
Sketch uses 1392931 bytes (44%) of program storage space. Maximum is 3145728 bytes.
Global variables use 41220 bytes (12%) of dynamic memory, leaving 286460 bytes for local variables. Maximum is 327680 bytes.
```

2026-09-16 加上 Improv 串口配网（`improv_serial.{h,cpp}`）之后：

```
Sketch uses 1383947 bytes (43%) of program storage space. Maximum is 3145728 bytes.
Global variables use 62108 bytes (18%) of dynamic memory, leaving 265572 bytes for local variables. Maximum is 327680 bytes.
```

新增模块的静态占用约 **2.2 KB RAM**（发送帧缓冲 522 B、结果编码缓冲 512 B、
扫描结果字符串 24×40 B、接收帧 128 B），flash 约 +5.9 KB。

全局 RAM 占用不代表 Wi-Fi 启用后的空闲堆；新版页面/API 在低堆水位下仍需真机回归。

无 warning、无 error。（体积会随每次改动小幅变化，以实际输出为准。）

### 编译相关排查

| 现象 | 原因与处理 |
| --- | --- |
| `.\scripts\build.ps1` 跑完"什么都没有发生"、没有输出、也没有生成 bin | PowerShell 5.1 在启动子进程时会用环境变量构造一个**大小写不敏感**的字典；若环境里同时存在 `http_proxy` 和 `HTTP_PROXY`，构造会抛异常，表现就是子进程静默不执行。`scripts/_common.ps1` 启动时会自动删除重复项并打印一行提示。若仍无效，用系统级命令 `set HTTP_PROXY=` 后重开终端 |
| `arduino-cli compile` **卡住不动**（十几分钟零输出、`tasklist` 里只有 `arduino-cli.exe` 而没有 `cc1plus`、构建目录无改动） | 代理变量把它的网络请求挂死了。**清空代理再跑**：`unset HTTP_PROXY HTTPS_PROXY http_proxy https_proxy ALL_PROXY all_proxy; export NO_PROXY='*' no_proxy='*'`。判断依据：清掉后 `arduino-cli core list` 会秒回 |
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
5. **运行时模式向量**（10 条）：返回/电源/语音三组可选模式的每一种取值。
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
[TEST   ] vector suite: 143 passed, 0 failed
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

**状态**：编译通过（0 warning，686595 B），模型检查 11098/0。**已上机，Windows 枚举通过。**

上机结果（2026-09-10 19:33，见 `build/win-pair4.log` 与 `build/win-diag4.txt`）：

```
设备端：
  [40696][HIDG] report 1 (keyboard) notifications ENABLED
  [40936][HIDG] report 2 (consumer) notifications ENABLED     ← 两条特征都被订阅

Windows 内核 PnP（hidbthle.inf 正常启动，无 Code 10）：
  ...&Col01  → keyboard.inf  (kbdhid)     键盘集合
  ...&Col02  → hidserv.inf                 Consumer 集合
  HIDClass 里非 OK 的设备：0 个
```

**`Col02` 的存在是 4.5/4.6 那个故障的反证**：旧结构下 Windows 连子设备都建不出来
（`CM_PROB_FAILED_START` / `0xC0110002`），现在两个集合各自绑定了正确的驱动。

对照本次与上一次（诊断版）的差异 —— 注意这里有**两个**变量同时变了，所以下表不能
单独用来归因：

| | 诊断版（单集合） | 本版（两集合两报告 ID） |
| --- | --- | --- |
| Windows HID 驱动 | `Status=OK` | `Status=OK` |
| 建出的集合 | 1 个（`Col01`）| **2 个（`Col01` + `Col02`）** |
| 连续运行断开次数 | 0 / 15 分钟 | 见 §5.4.2（受配对状态影响，见下） |

**过程中踩到的一个坑**：为了让服务结构变更后从干净状态重新配对，只在 C3 侧执行了
`forget win`，Windows 侧仍保留配对记录 → 两侧密钥不一致 → Windows **每 6～7 秒断开重连一次**
（MTU 也从 256 掉到 23），期间按下的键落入断开窗口而丢失。Windows 后来自行完成重新配对
（`bonds` 由 1 变回 2）后才稳定。**结论：清配对必须两边同时清**，已写进 `docs/RECOVERY.md`。

### 4.8 重连慢：9.5 s → 3.3 s（订阅顺序 + 连接参数 + 监督超时）

用户报告重启后十几秒按键才生效。实测时间线（`build/reconnect-timing*.log`）：

```
优化前                                    优化后
2.9 s  Windows 重连+订阅                  3.7 s  Windows 重连+订阅（并行）
3.0 s  RC003 建链                          4.0 s  RC003 建链
4.0 s  服务发现完成                        4.9 s  服务发现完成
5.9 s  电池读+订阅      <-- 按键仍死       6.0 s  HID 订阅完成
9.3 s  HID 订阅完成（3.4 s）               3.2 s  READY（两订阅齐）
9.5 s  READY
```

三个根因与修复：

1. **服务 map 按 UUID 字符串排序**，`0x180F`（电池）排在 `0x1812`（HID）前 →
   先花 ~2 s 读电池+订阅。改两遍扫描：pass 0 只处理 HID（按键路径），pass 1 其余。
2. **第一版修复的 filter 放错位置**：`getCharacteristics()` 是惰性调用（首次访问才做
   ATT 特征发现），filter 在它之后时 pass 0 仍为每个跳过的服务付 ~3.2 s 发现成本。
   filter 必须在惰性调用**之前**。
3. **`updateConnParams(12,12,0,400)` 在函数末尾**才发——发现与订阅全部跑在旧连接
   间隔上。提前到加密完成后立即请求。

修复后仍余 ~3 s 建链等待。深挖发现真正原因是**链路监督超时**：硬复位无法发 BLE
断开包，遥控器要等 `BLE_GAP_INITIAL_SUPERVISION_TIMEOUT = 400×10ms = 4 s` 才发现
主机消失、开始广播——与实测 3.5–4 s 建链精确吻合。库里扫描参数本已是 10ms/10ms
（100% 占空比），无优化空间。修复：`updateConnParams(12,12,0,100)`（1 s），加密后
立即发 + 周期刷新重申。副作用全为正：真实断链 1 s 内检测（清按键更快）；1 s 超时
在 15 ms 间隔下容忍连续丢 60+ 个连接事件，不会误断。

最终实测（第二次重启起，新参数已生效）：建链 **1.03 s**、HID 订阅 **2.9 s**、
**READY 3.2 s**（两订阅齐）——9.5 s → 7.0 s → **3.3 s**，达到普通蓝牙键盘
断电重连水平（1–3 s）。

### 4.9 iOS 主机与多主机轮换（已验证）

iPhone 配对后（bond `xx:xx:xx:xx:xx:xx`），BLE HID 在 iOS 上完整工作。实测：

```
[847809] host connected (id 1 addr xx:xx:xx:xx:xx:xx mtu 23)
[848439] report 1 (keyboard) notifications ENABLED
[848439] report 2 (consumer) notifications ENABLED
[848451] battery level notifications ENABLED          ← iOS 连电池也订阅
[850209] host READ battery -> 97%                     ← 并主动读电量
```

- 音量键（Consumer）与方向键（键盘）在 iPhone 上实测可用；
- 组合键在 iOS 上按 Apple 语义解释（Win=⌘、Alt=Option），属正常差异；
- 多主机轮换：iPhone 断开后 **0.1 s** Windows 自动接管，双向成立；
- 两集合两报告 ID 的结构在 Windows 与 iOS 上均被接受。

### 4.10 NFC 的机制结论（与桥接器无关，已实测排除）

用户实测（iPhone NFC Tools 读卡 + 碰触抓取）：

- 标签是 **ISO 14443-4 智能卡**（Type A / IsoDep，复旦微芯片），UID
  `1D:3D:BA:xx:xx:xx:xx`，**无任何可读 NDEF 内容**；
- 碰触期间蓝牙侧**零数据**（除用户物理按键外无断链、无重连、无报告）；
- 小米手机的投屏走 **IsoDep APDU 私有协议 + 小米账号云**（这就是只有小米手机
  能投屏、iPhone 读不出内容的原因）；iPhone 快捷指令触发绑定的是**卡片 UID**；
- BLE 侧 9 个服务 25 个特征中没有任何"写 NFC 卡"的通道（可写点均为 HID/ATVV
  功能接口）。

结论：NFC 与蓝牙是两条独立通道，桥接器无 NFC 硬件、也不参与。"碰一碰 →
Windows 动作"如需实现，唯一现实路径是给 C3 加 NFC 读卡模块（如 PN532），
未排期。

---

### 4.11 Wi-Fi 配置栈与分区方案（Web UI 的代价与对策）

加入 `wifi_ui`（Wi-Fi AP + HTTP 服务器 + 页面）后：

1. **flash 溢出**：默认分区只有 1.2 MB app，Wi-Fi/lwip/WebServer 使 text 段超出。
   FQBN 增加 `PartitionScheme=huge_app`（3 MB app，无 OTA）；实测 flash
   1 360 719 B (43%)。`scripts/_common.ps1` 已固化该选项。
2. **堆水位**：AP 开启时 free ~20.8 KB（紧张但稳定，min 20.5 KB）；`wifi off` 后
   回落到 ~59 KB（**不会**回到初始 106 KB——Wi-Fi 库保留内部缓冲，属预期；
   完全恢复需重启）。两轮 on/off 循环后 free 稳定在 58.9–59.0 KB，无泄漏。
3. **共存**：AP 启停期间 RC003 保持 READY 不断链。

---

### 4.12 Web UI 浏览器端回归（2026-09-11，**非真机**）

页面重做后需要在不接板子的前提下把浏览器逻辑跑一遍。做法是从 `web_page.h` 抽出页面，
注入一个**只存在于预览文件**的 `fetch` 模拟器（`tests/tools/web_ui_preview.py`），
再用本机装的 Chrome 执行断言 —— 驱动方式是与真机检查同一套的 `cdp.py`（手写 CDP 客户端），
**除了 Chrome 和 Python 之外不需要装任何东西**。

```bash
python tests/tools/check_web_ui.py                  # 闪存预算 + 125 项页面断言（约 1 秒）
python tests/tools/check_web_ui.py --check-size     # 只看闪存预算
python tests/tools/check_web_ui.py --emit-js out.js # 只导出断言源码，便于调试
```

`scripts/test.ps1` 的第 3/4 步跑的就是第一条命令。

**闪存预算口径（2026-09-12 修正）**：盯的是三个 **gzip 资产的实际字节数**（当前
`html 3902→1751 / css 9132→3037 / js 14856→6400`，合计 **11188 B**，上限 14000 B），
而不是源页面字节。旧口径盯源页面（已长到 26956 B）→ 上线即红，且**这个数字设备根本看不到**
（页面是 gzip 传输、只有压缩包进 flash），于是一红到底、无人理会。

**热点的按下/选中状态逐键断言（2026-09-12 新增，39+2 项）**：每个遥控器热点都同时带一个位置类
（`p1..p8`、`du/dd/dl/dr`、`do`），其中几个自己设了 background / border / box-shadow / color，
且**排在 `.rb.live`/`.rb.sel` 之后、权重相同** → 源序取胜。后果：上下左右四个键完全不高亮，
电源/语音/确定/音量只亮一半，而页面在真实按键时会加的 `.pulse` **一条 CSS 规则都没有**。
现在断言「每个热点的 live / sel / pulse 三态都必须改变计算样式」，**逐键断言**——因为这个缺陷
的本质就是"键与键之间不一致"，整体断言抓不到。注意探针必须先把 `transition` 置 `none`：
`.k` 有 `transition:.15s`，改完 class 同 tick 读计算样式拿到的是过渡前的值，会把正常状态判成死态。

断言失败时的排查工具：`python tests/tools/spot_audit.py`（`--raw 0x52` 可指定键）—— 逐键打印
三态各自改了哪些计算样式，并把遥控器截图写到 `outputs/`；有死状态就以非零码退出。

**已验证（150 项断言，全部通过）**：

> 修正记录：这层断言一度**跑不到也跑不对**，两个独立缺陷 ——
> ① 执行入口要求传入外部 `agent-browser` 可执行文件，而它不在本仓库工具链里，
> `scripts/test.ps1` 只接了 `--check-size`，所以断言实际从未被自动执行；
> ② `web_ui_preview.demo_html()` 注入模拟器的锚点是 `<script>\n'use strict';`，
> 页面在 `<script>` 后多了一个空行后 `str.replace` **静默变成空操作**，预览页根本没有
> fetch 模拟器 → 所有断言都会因 "initial API load" 失败。
> 现改为按标签定位注入（找不到就报错退出），且执行改走 `cdp.py`。

| 类别 | 覆盖 |
| --- | --- |
| 数据加载 | 13 张卡片 + 13 个遥控器热点；`effective` 生效值优先于静态默认 |
| 逐键保存 | 13 个键逐个保存，请求 `raw` 为**十六进制**，回读值与提交值逐项一致 |
| 编辑回填 | 修饰键位、主键、Consumer 十进制回填；「不改直接保存」不丢修饰键 |
| 组合与边界 | 仅修饰键和弦（Ctrl+Win）、右 Alt+`,`、空键盘动作被拒 |
| 清除语义 | 「恢复此键默认」先改草稿、保存后才生效；恢复后回落串口基础模式 |
| 失败处理 | HTTP 错误保留编辑器；回读不一致判定失败；离线显示旧数据并禁用写入 |
| 注入防护 | 广播名含 HTML 时按文本渲染，不进入 DOM |
| 布局 | 1440 / 1024 / 768 / 390 / 320 五档 × 三页，均无横向溢出；Esc 关闭弹窗 |

**这一层没有验证**：ESP32 上的真实 HTTP 栈、NVS 持久化、Wi-Fi 与 BLE 共存下的堆水位、
真实手机浏览器。§0 表格里 Web UI 一行的"浏览器回归"指本节，**仍不等于真机验收**。

---

### 4.13 新版 Web UI 的真机回归：**未通过**（2026-09-11，COM3 实测）

§4.12 的浏览器回归只证明页面逻辑对；**真机上这份页面发不出去**。以下是取证。

**堆账（串口 `status` 实测）**

| 状态 | free heap | largest block |
| --- | --- | --- |
| 刚启动（BLE 未连） | 147868 B | 114676 B |
| AP 关闭（BLE 已连） | ~50068 B | — |
| AP 开启（BLE 主机未连） | ~20744 B | — |
| **AP 开启 + BLE 主机已连** | **~13568 B** | 7668 B |

AP 本身约吃 38 KB；BLE 两条链路再把堆压到 13 KB 量级。

**三个故障，按发现顺序**

1. **单次 `send_P()` 发 47.8 KB 必失败。** 整块交给 lwIP 需要大块连续内存，
   而最大连续块只有 7.6 KB。表现：首页返回 200 但 0 字节，之后 `/api/bindings`
   静默，**HTTP 服务从此不再应答**。
2. **只做分块（chunked）仍然不够。** 改为 1 KB 分块后 `curl` 只收到
   29696 / 47845 字节就超时，服务再次卡死。根因是没有做 **TCP 流控**：
   `NetworkClient::write()` 在 `WIFI_CLIENT_MAX_WRITE_RETRY` 之后**返回部分写入量**，
   而 `sendContent()` 不检查返回值 —— 丢一个字节，分块框架就错位，客户端一直等。
   正确做法是 `availableForWrite()` 循环并校验写入量。
3. **AP 的 DHCP 在低水位下不发租约（最严重）。** 在 ~13.5 KB 水位下，客户端能关联
   802.11 但拿不到 IP，退化成 `169.254.x.x`；**电脑与手机都复现**。
   堆回到 ~20.7 KB 时立刻正常（电脑拿到 `192.168.4.2`）。
   → 这是**内存不足导致 AP 的 DHCP 服务起不来**，不是客户端问题。

**遥控器侧是好的**：串口抓到 `parsed raw=0x4A (HOME) DOWN` / `UP`。
失效的是下游 —— 日志里 `host connected: no (0)`，按键没有接收方。
`wifi off` 后堆回到 ~50 KB，桥接器恢复正常。

**页面体积红线（实测）**

| 页面大小 | 发送方式 | 结果 |
| --- | --- | --- |
| 21482 B | `send_P` | ❌ 200 但 0 字节 |
| 21482 B | 1 KB 分块 + 无流控 | ❌ 只到 29696/47845 字节后卡死 |
| 47845 B | `send_P` | ❌ 同上 |
| 8257 B（gzip） | `send_P` | ⚠️ 响应头正确，**正文被截断**（0–5612 / 8257） |

> **更正**：本文档早先版本写过"13087 B 可用（原版页面）"，那是**从旧页面体积推断的，不是实测**。
> 回查 `HANDOFF.md` 与 §4.11，此前只验证过 AP/HTTP/**API**，**页面本身从未在真机上确认能加载**。
> 现在有证据表明它很可能一直不可用 —— 见 §4.13.3。
根因：`NetworkClient::write_P()` 只是 `write()`（flash 内存映射，不需要大缓冲），
而 `write()` 在 `WIFI_CLIENT_MAX_WRITE_RETRY` 次重试后**返回部分写入量**，
`send_P` 不看返回值 → 剩余字节被静默丢弃，HTTP 服务随后不再应答。

**试过但失败的方案**：`availableForWrite()` 式流控循环（1 KB 缓冲 + 按返回值重试 + 缓冲满时 `delay(2)`）
在真机上**仍然失败**（21.5 KB 依旧 0 字节），因此**已回退**，不作为未验证代码留在树里。

**下一步（未做）**：把页面压到 ≤ 13 KB（旧版大小），再上机复测。
**在真机复测通过之前，不得声称 Web UI 真机可用。**

**已验证可用的诊断（保留）**：`wifi status` 打印 `stations` / `AP IP` / `heap free`；
AP 事件打印 station joined / left(reason) / **got an IP**。
真机确认过 `station left (aid 1, reason 2)` 与 `AP stopped, heap back to 49484 B`。
`STAIPASSIGNED` 只在 DHCP 真的发出地址时触发，是区分"连不上"与"连上但无租约"的关键。

#### 4.13.1 真正的根因之一：WebServer 默认不收集请求头

用 `curl -v` 抓响应头，看到服务器回的是 **`Content-Length: 21578`** —— 也就是
gzip 分支根本没进，走的是明文回退路径。原因在库层：

```cpp
// WebServer.h
int _headerKeysCount = 0;          // 默认 0，只有前两个标准头会被记录
void collectHeaders(const char *headerKeys[], const size_t headerKeysCount);
```

**必须显式声明要收集的请求头**，否则 `header("Accept-Encoding")` 永远返回空字符串。
修复：`s_server->collectHeaders(kCollectedHeaders, 1)`（`wifi_ui.cpp` 的 `startServer()`）。

这条同样是"改了没效果"却看不出原因的典型：**分支静默走错，日志里什么都不会出现。**
诊断手段是看响应头里的 `Content-Length`，而不是只看状态码。

#### 4.13.2 真正的阻塞点：低堆水位下 AP 的 DHCP 服务起不来

页面体积解决后，暴露出更前置的问题。多次实测：

| AP 启动时的 free heap | DHCP |
| --- | --- |
| ~20744 B | ✅ 客户端立刻拿到 `192.168.4.2` |
| ~12944–13568 B | ❌ 客户端关联成功但退化成 `169.254.x.x` |

堆水位取决于 `wifi on` 那一刻的 BLE 状态，因此**同一份固件时而可用、时而不可用**。
`wifi off` 后堆回到 ~49640 B，桥接器恢复正常。

**这意味着：只要 BLE 链路把堆压到 13 KB 量级，配置界面就无法访问 —— 与页面大小无关。**
在解决这个之前，Web UI 的可用性取决于运气，不能对外声称可用。

**gzip 方案的现状（2026-09-12 更新）**：`tests/tools/gen_web_page.py` 把页面拆成
三份资产并压缩进 `web_page_gz.h`，**只存 gzip、无条件回 `Content-Encoding: gzip`**
（不按 `Accept-Encoding` 分流）。期间做过一版"明文 + gzip 双形态"（+27 KB flash），
**用户明确否决："不影响功能，不要增加复杂度"**，已回退，留档在
`build/gzip-dual-form-backup/`。改用 STA 后**真机验证通过**（见 §4.13.4）。
代价：任何直接读 `/`、`/app.css`、`/app.js` 的工具**必须自己 gunzip**，
否则会得到"200 但内容不对"的假失败。

#### 4.13.4 改为「连路由器」后：页面**首次在真机上完整加载**

AP 模式的根本问题是板子自己还要跑 DHCP 服务，而 BLE 已经把堆压到临界。
改成 **STA 模式**（板子加入现有 Wi-Fi，由路由器发 IP）后：

```
wifi join MyHomeWiFi <password>     # 凭据存 NVS
wifi on
  → joining "MyHomeWiFi" - the config page will be served on the LAN
  → config page: http://192.168.1.100/ (heap 19632 B, Wi-Fi cost 57568 B)

curl --compressed http://192.168.1.100/
  第2次: HTTP 200  gz 8257B  1.20s  解压后 21578   ← 完整
  第3次: HTTP 200  gz 8257B  1.26s  解压后 21578   ← 完整
```

**这是该页面第一次在真机上完整送达**（结尾 `</html>`，浏览器无报错）。
对比 AP 模式：同样的页面被截断到 5612 B、耗时 10.4 s。
两个原因：路由器负责 DHCP（板子不再需要 DHCP 服务），链路质量也由路由器保证。
传输从 10.4 s 降到 1.2 s。

**但还没完 —— 堆水位波动仍未解决。** 多次实测 `wifi on` 之后的堆：

| 场景 | Wi-Fi 开启后 free heap | 页面 |
| --- | --- | --- |
| STA，`before Wi-Fi` 77200 | 19632 B | ✅ 完整 |
| STA，`before Wi-Fi` 67960/68124 | 10512–10540 B | ❌ 0 字节 |
| STA，`before Wi-Fi` 67964 | 10528 B | ❌ 0 字节 |

Wi-Fi 开销稳定（57436–57584 B），**差异全部来自开 Wi-Fi 之前的堆**：
67960–77200 B（约 9 KB 波动），来源未查明。**堆 ≥ ~19 KB 时页面可用，~10.5 KB 时不可用。**

另外试过 `WiFi.setSleep(false)` 想降低首包延迟，**已回退**：它没解决波动，且无法证明有收益。

**当前状态**：STA 模式已实现并验证可用（`wifi join` / `wifi on`），
AP 保留为兜底（`wifi ap on`）。**页面能否加载仍取决于那 9 KB 波动，未达到可对外声称的稳定度。**
下一步应查清 `before Wi-Fi` 堆的 9 KB 波动来源（怀疑与 BLE 链路状态或 Wi-Fi 库重入有关）。

#### 4.13.5 把页面拆成三个资源，并排除了两个错误假设

生成器改为把页面拆成三个独立资源，全部 gzip 传输（浏览器一律支持，顺带去掉了
`Accept-Encoding` 握手与 `collectHeaders()` 依赖）：

| 资源 | 原始 | gzip |
| --- | --- | --- |
| `/`（HTML 外壳） | 3844 B | **1748 B** |
| `/app.css` | 7555 B | **2433 B** |
| `/app.js` | 10213 B | **4358 B** |

**两个假设被真机数据推翻**：

1. ❌「负载必须小于最大连续块」——1748 B 的资源在 ~10 KB 空闲时同样 0 字节，
   而 1252 B 的 `/api/bindings` 在同一水位下是成功的。
2. ❌「从 PROGMEM 直接发有问题」——把资源拷进 RAM 缓冲区、走与 JSON 接口
   完全相同的发送路径，结果一样失败。

**真正成立的相关性**：`/api/*` 这类小响应能成功，页面类响应失败 ——
但**页面是测试里的第一个请求，它失败后服务被卡死**，之后所有请求都失败。
所以"哪些接口能用"的对比是被污染的；正确的说法是：

> **空闲堆 ~9.7–10.5 KB 时 WebServer 无法完成任何页面级响应；
> 空闲堆 19632 B 时三个资源全部正常。**

**决定性的变量是开 Wi-Fi 之前的堆**：

| 场景 | 开 Wi-Fi 前 | 开之后 | 结果 |
| --- | --- | --- | --- |
| 唯一成功的一次 | 77200 B（largest 65524） | 19632 B | ✅ |
| 其余多次 | 67940–68124 B（largest 57332） | 9668–10540 B | ❌ |

注意 `largest` 相差 **8192 B —— 恰好 8 KB**，很像一个 FreeRTOS 任务栈。
开 Wi-Fi 前的堆在 48.6–77.2 KB 之间，Wi-Fi 开销 38.4–57.6 KB，两者叠加后落在 9.7–19.6 KB。

**下一步（明确）**：查清那 8 KB 连续块归属哪个任务/缓冲，以及为什么它有时不存在。
把开 Wi-Fi 前的堆稳定在 77 KB 以上，配置页就可用。**在此之前不要声称 Web UI 可用。**

---

### 4.14 状态指示灯、Wi-Fi 常开、实时响应与遥控器示意图（2026-09-11 晚）

本轮四项改动：

1. **双状态指示灯**（`status_led.cpp`）——**D5 = GPIO13 指示电脑链路，D4 = GPIO12
   指示遥控器链路**；每个灯三种节奏：呼吸（PWM 三角波，2 秒一个来回）= 等待连接、
   快闪（90 ms 半周期）= 正在连接、常亮 = 已连接。**按住遥控器任意键时 D4 熄灭**，
   松开恢复（用 `bridge::activeRawKey()`，与串口 `status`、Web UI 同一份状态）。
   引脚依据合宙官方 wiki（D4=IO12、D5=IO13，高电平有效）；这两个脚在 QIO flash
   模式下是 SPIHD/SPIWP，本板强制 DIO 模式所以可用——与"板子必须 DIO"的既有约束
   正好互补。旧的单 LED 实现（GPIO8、低电平有效、位于 bridge.cpp 内）已整体移除。
2. **Wi-Fi 常开**：开机 8 秒后自动 `enable()`（先让 BLE 建链），`disable()` 改为
   明确拒绝，加入失败不再放弃而是每 15 秒用已存凭据重试。
3. **页面实时响应**：关闭 Wi-Fi modem sleep（`WiFi.setSleep(false)`）——默认省电
   模式下每个 HTTP 请求要等下一个信标，滞后 100–300 ms；同时前端轮询 300 → 150 ms。
4. **遥控器示意图按实物比例重绘**：方向盘中心在机身 22% 高度、直径占机身宽 80%；
   顶部两键 9% 高度；左右两列 37% / 47% / 57% 且严格水平对齐。根因是 `.dp`
   容器的 CSS 仍然存在、而 `rmMark()` 早已不再生成该容器，于是方向键坐标相对
   `.body` 生效——上键跑到机身顶部、下键跑到机身底部。

**验证状态**：编译通过（flash 42% / RAM 13%，0 warning）；HTML 预览经用户确认。
**真机验证待做**：LED 三态与按键盘熄灭、开机自动连 Wi-Fi、页面实时高亮延迟。

> 后续：§4.17 又加了第四种节奏（双闪 = 配对信息失效），并在十分钟无操作后熄灭
> 两个灯；本节记录的是当时的三种节奏。

---

### 4.15 事件通道（长轮询）取代按键轮询；烧录速度实测（2026-09-11 深夜）

**问题**：页面 150 ms 轮询按键状态，短按会在两次轮询之间整个消失；用户要求
"收到事件立即显示"。

**方案取舍**：SSE / WebSocket 在本服务器上不可行——它一次只服务一个 exchange，
长期流式连接会阻塞页面自己的保存请求。改用**长轮询**：页面 `GET
/api/events?since=<计数>` 挂在设备上，设备在按键转发时立即应答；无事件则
5 秒窗口到期后应答，页面立刻续上。请求带事件计数（`bridge::keyPresses()`），
因此**计数不丢 = 按压不丢**。写入前页面先 abort 该请求，写完恢复。

**实测**（串口注入按键后测量应答时刻）：

| 场景 | 结果 |
| --- | --- |
| 无按键，挂起到期 | 5096 ms（窗口 5 s） |
| 注入 `key 52 press` 后应答 | **23 ms** |
| 注入 `key 52 release` 后应答 | 25 ms（activeKey 归 0） |
| 期间堆余量 | 30.9 KB（min 16.1 KB） |

顺带修掉两个用户可见问题：状态轮询失败一次就锁死"灰色不可编辑"→ 现在
`/api/status` 成功即自动恢复并补拉映射；5 秒全量重读 → 改为 30 秒兜底
（保存本身已经回读确认）。

**烧录速度实测**：arduino-cli @921600 写 1.33 MB 用 17.3 s（618.6 kbit/s）；
esptool 直接以 **1 500 000** 波特率烧录为 649.8 kbit/s / 16.4 s——**几乎没有区别**，
说明瓶颈在 flash 写入/CH343 传输路径，**不在串口速率**。C3 的 boards.txt 也只
提供到 921600。结论：不需要改烧录参数。

---

### 4.16 WebSocket 事件通道 + 访问密码（2026-09-11 深夜）

**架构变化**：按键反馈从轮询/长轮询改为**一条 WebSocket 长连接**，双向承载
按键推送与配置命令。实测（串口注入按键，测应答时刻）：

| 项目 | 结果 |
| --- | --- |
| 握手 | `101 Switching Protocols`，`Sec-WebSocket-Accept` 校验通过 |
| 带错误 token 的握手 | `401` |
| 落键 → 帧到达 | 数毫秒级（长轮询版实测 23 ms，WS 更短） |
| 保活 | 服务端每 10 s ping；对端静默 30 s 即断开释放服务槽 |

**认证**（HTTP Basic + WebSocket 令牌）：

```
无凭据             → 401 + realm "…setup - choose a console password"
首次带凭据         → 200，密码写入 NVS（SHA1）
同一凭据           → 200
错误密码           → 401 + realm "MiRemoteBridge web console"
GET /api/token     → {"token":"…"}（受 Basic 保护）
/ws?token=<正确>   → 101
/ws?token=<错误>   → 401
pass clear（串口） → 清除密码，回到 setup 模式
```

**忘记密码的出路**：BOOT 长按 5 秒 → `settings::clearAll()` 用
`s_prefs.clear()` 清空整个命名空间（Wi-Fi 凭据、网页密码、按键映射、
状态缓存），配合 `ble_bonds::removeAll()` 清蓝牙配对 → 完整出厂状态。

**三个真机才暴露的坑**（本轮）：

1. **握手响应被自己的 WS 分支吞掉**：`s_ws` 一置位，`pollHttp()` 就在开头
   进入帧模式并 `return`，握手写的 `101` 留在 HTTP 发送缓冲里永远发不出去 →
   客户端等 4 s 超时看到连接被关。修法是加 `s_pendingUpgrade`：`101` 走完
   正常 HTTP 发送路径后才把同一个 socket 翻成帧模式。
2. **`Authorization` 头解析**：`%s` 会在冒号后的空格处停下（只拿到
   `"Basic"`），`%[^\r]` 又会把空格带进去。正解是先跳过空白再读到行尾。
3. **残留在服务槽上的连接**：长轮询/WS 占用唯一服务槽，若对端消失而不发 FIN，
   服务会永久卡死（lwip 自己的 keepalive 要数小时）。长轮询已整体移除；
   WS 加了 ping + 30 s 静默断开。

---

### 4.17 两条链路的"配对信息失效"闭环（2026-09-16，编译 + 宿主端 + 主机侧上机实测）

本轮把"配对信息失效"从"用户自己猜"变成"固件自己认出来并锁存，网页给出唯一
正确的下一步"。四组改动：

**1. 主机（下游）侧：广播永不暂停，删掉网页的"重新配对"按钮**

- 原实现有一个 `pauseHostPairing()` / `hostPairingPaused` 暂停广播的开关，配合
  网页的"重新配对"按钮和 `POST /api/host-pairing`。**这一整套已删除**：恢复出厂
  清掉设备侧 Bond 后，广播本来就该自己一直开着，不需要用户再点任何东西。
- 旧密钥认证失败（`BLE_GAP_EVENT_ENC_CHANGE` 且 `role == SLAVE`、`status != 0`）
  → 新事件 `BR_EV_WIN_AUTH_FAIL` → `hid_server::setHostRepairRequired(true)`，
  网页锁存"请在 Windows 删除 Mi Remote Bridge 后重新添加"。新配对成功
  （`role == SLAVE`、`status == 0`）→ `BR_EV_WIN_AUTH_OK` → 清除锁存。
- **不宣称"完全消除 Windows 上短暂的连接/断开"**：Windows 在拿到 `AUTH_FAIL`
  后自己会重试，那一小段连上又断开是它的行为，固件只能保证**固件自己不再重试**
  （拒绝 + 锁存提示）。
- **⚠️ 实测更正：这个循环不会自己停。** 见本节末尾"上机实测"——Windows 会以
  约 2 次/秒的频率持续重连，直到用户在 Windows 里删掉设备。固件侧唯一能真正
  打断它的手段是暂停广播，而暂停广播会让用户无法从 Windows 重新添加，两个要求
  直接冲突。**经用户确认，选择保持"一直广播 + 拒绝 + 锁存"**，并把这一事实写进
  `RECOVERY.md` / `PAIRING.md`，不假装循环已经停止。

**2. 遥控器（上游）侧：`REMOTE_REPAIR_REQUIRED`**

- 普通断开（超时、休眠、远端主动断）**不进**这个状态：继续用已有 Bond 自动重连。
- **只有**确认的认证/密钥失败才进入——判据是 `ENC_CHANGE` 且 `role == MASTER`
  且 `status` 属于 `BLE_ERR_AUTH_FAIL (0x05)` / `BLE_ERR_PINKEY_MISSING (0x06)`。
  用角色区分上下游，所以两条链路的失败不会互相误伤。
- 进入后：**停止自动重连**（不再调用 `connect()`）、**不删 Bond**、**不自动配对**、
  **不清空槽位映射**。网页锁存"遥控器配对信息已失效，已停止自动连接。如果要继续
  使用，请让遥控器进入配对模式，然后从附近设备中选择。"
- **必须由用户在网页上明确选择**附近设备后才动手：`POST /api/connect` 带
  `replacingDeadPairing = true`，先删当前**活动槽**的旧遥控器 Bond（学习到的
  按键与槽位绑定不动），再连/重配所选设备，成功后清除警告。
- 扫描期间 `onResult` 在 `s_repairRequired` 时直接返回：附近列表照常刷新，但
  **不会**自动匹配、不会自动连接——保证"用户选择之前不自动重配"。

**3. 检查了 Arduino-ESP32 3.3.11 的 `BLEClient` 自动重试行为**

`BLEClient::secureConnection()` 在 `PINKEY_MISSING` 时会重试一次
（`libraries/BLE/src/BLEClient.cpp` 约 234–243 行），但本固件走的是
`ble_gap_security_initiate()`，不经过那条路径。真正需要注意的库行为是
`BLEClient::handleGAPEvent()` 的 `ENC_CHANGE` 分支（约 1296–1306 行）：它在
`PINKEY_MISSING` 时会自己调 `ble_store_util_delete_peer()`——这是库内行为，
固件无法拦截，所以"不删 Bond"指的是**固件自己**不删；`BLE_GAP_EVENT_DISCONNECT`
不会自动重连，且 `BLESecurity::m_forceSecurity = true` 只在连接成功后才发起加密，
因此"用户选择之前不自动重配"成立的前提是**修复态下永不调用 `connect()`**。

**4. 指示灯：新增双闪，原三态语义不变**

- D5 / D4 新增第四种节奏 **双闪**（亮 120 ms / 灭 120 ms / 亮 120 ms / 灭约
  1.24 s，周期 1.6 s），分别对应上面的两种锁存态。呼吸 / 快闪 / 常亮三种原有
  语义、以及"按住遥控器按键时 D4 熄灭"的反馈**完全不变**。
- 双闪的占空比**完全由时钟取模算出**（`now % 1600`），没有相位状态、也不做
  减法比较，`millis()` 回绕安全。
- **十分钟休眠**对双闪同样生效（提示仍留在网页上）；休眠期间按遥控器按键会让
  D4 亮 140 ms 作为确认，且**不重置**那十分钟的计时。

**验证状态**：

| 检查 | 命令 | 结果 |
| --- | --- | --- |
| 固件编译 | `arduino-cli compile`（FQBN 见 §4.11） | ✅ 1378063 B（43%）flash / 59764 B（18%）RAM，0 warning |
| 宿主端模型 | `python tests/model/check_vectors.py` | ✅ 11098 断言通过 |
| 网页断言 | `python tests/tools/check_web_ui.py` | ✅ 161 条通过（新增 8 条覆盖两个锁存提示） |
| 生成物一致性 | `gen_web_page.py --check` / `gen_vectors.py` | ✅ `web_page_gz.h` 与 `selftest_vectors.h` 均可复现 |
| 烧录 + 串口冒烟 | `flash.ps1 -Port COM3`（COM3 = 0x1A86:0x55D3，探到 ESP32-C3） | ✅ `UPLOAD OK`、哈希校验通过、启动横幅与控制台正常 |

**上机实测（2026-09-16，COM3；本轮唯一一次真机观测）**

设备侧的 Bond 恰好处于"Windows 记着旧密钥、C3 这边没有"的状态，于是**主机侧
那条路径在真机上跑到了**：

```
[     470][BRIDGE ] host re-pair    : no                      <- 开机时未锁存
[    2037][WIN    ] host connected (id 1 addr xx:xx:xx:xx:xx:xx mtu 23 total 1)
[    2097][BLE    ] host authentication failed (status=7)
[    2109][WIN    ] host disconnected (handle 1, remaining 0)  <- reason=531，Windows 主动断
[    2110][BRIDGE ] host authentication failed -> re-add the device in Windows
[    2122][WIN    ] stored host key rejected; re-add "Mi Remote Bridge" in Windows to pair again
[    2274][BRIDGE ] host re-pair    : required                  <- 锁存生效
```

**已实测**：失败被识别（`status=7`）、锁存生效（`status` 里 `host re-pair : required`）、
日志给出正确指引、设备**继续广播**（期间有 `advertising re-armed`）。

**同时实测到的问题（用户已确认按现状处理）**：这个重连**不会自己停**——
10 秒窗口内 `host connected` 与 `authentication failed` 各 19 次（约 2 次/秒），
持续数分钟不减。重连由 Windows 发起（`reason=531` = 远端主动断开），固件只能
拒绝，无法让对端停止重试。要真正打断它，固件侧唯一的办法是暂停广播，而那样
用户就没法从 Windows 重新添加设备——两个要求互斥。**决定：保持一直广播 +
拒绝 + 锁存**，用户按提示在 Windows 删除设备后循环立刻结束。这一点已写进
`RECOVERY.md` §2.1 ① 与 `PAIRING.md` §3。

**尚未实测**：D5 双闪的外观（无法在远端目视确认，代码路径只到
`hostRepairRequired() -> Show::DoubleFlash`）、清除锁存（需要真的在 Windows 里
删除设备再添加）、遥控器侧整条 `REMOTE_REPAIR_REQUIRED` 路径（本次没有绑定
遥控器）、双闪的十分钟休眠与按键唤醒。

**真机验证待做**：见 §5.3.1 第 25–33 项。

---

## 5. L4 实机验收清单（部分完成）

按顺序做完并回填。每条都有"怎么判"和"期望结果"。

### 5.1 基础链路

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 1 | 烧录 | `.\scripts\flash.ps1 -Port COM3` | `UPLOAD OK`，串口出启动横幅 | ✅ 2026-09-10；2026-09-12 复烧 v0.0.1 再验 |
| 2 | 上游配对 | `scan` → `connect <index>`（人工选择） | `ready: N subscription(s)` | ✅ 秒连，见下 |
| 3 | 下游配对 | Windows 蓝牙添加 `Mi Remote Bridge` | 日志 `host connected` 变 1 | ✅ 修好（§4.5：两个顶层集合 + 各自报告 ID + 两条 `0x2A4D` 特征）后 `Status=OK` |
| 4 | Windows 识别 | 设置 / 设备管理器 | 出现"键盘"+ 消费类控制设备 | ✅ 键盘 → `kbdhid`、消费类 → `hidserv.inf`；iOS 也实测可用（音量 / 方向键） |
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
| 3 | 返回 | `0xF1` | 键盘 Backspace `0x2A` | | ✅ |
| 4 | 上 | `0x52` | 键盘 `0x52` | | ✅ |
| 5 | 下 | `0x51` | 键盘 `0x51` | | ✅ |
| 6 | 左 | `0x50` | 键盘 `0x50` | | ✅ |
| 7 | 右 | `0x4F` | 键盘 `0x4F` | | ✅ |
| 8 | 确定 | `0x28` | 键盘 `0x28` | | ✅ |
| 9 | 主页 | **`0x4A`** | LGUI + `0x2B`（Win+Tab） | | ✅ |
| 10 | 菜单 | **`0x65`** | 键盘 `0x2C`（Space） | | ✅ |
| 11 | 电视 | **`0x35`** | 键盘 `0x09`（F） | | ✅ |
| 12 | 电源 | `0x66` | 键盘 `0x29`（Esc） | | ✅ |
| 13 | 语音 | **`0x3E`** | LCTRL + LGUI（Ctrl+Win） | | ✅ |

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

### 5.3.1 配对信息失效的闭环（§4.17，本轮新增）

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 25 | 主机侧锁存 | Windows 删除设备后**只在 C3 侧**保留旧 Bond（即不 `forget win`），再让 Windows 尝试连 | 日志出现 `WIN_AUTH_FAIL`；D5 双闪；网页出现"请在 Windows 删除 Mi Remote Bridge 后重新添加"，且**没有**任何按钮；C3 **继续广播** | ☐ |
| 26 | 主机侧解锁 | 在 Windows 里删除 `Mi Remote Bridge` 后重新添加 | 添加成功后 `WIN_AUTH_OK`；D5 回到常亮；网页提示自动消失 | ☐ |
| 27 | 主机侧不循环 | 处于第 25 项状态时观察 1 分钟 | 不会反复"连上→加密失败→断开"；失败只出现一次并锁存 | ☐ |
| 28 | 遥控器侧进入 | 让 RC003 的 Bond 失效（换电池/在原主机上解绑）后静置 | 日志出现 `RC_AUTH_FAIL`；D4 双闪；`status` 显示 `rc003 repair : yes`、`rc003 state : REMOTE_REPAIR_REQUIRED`；网页出现"遥控器配对信息已失效…" | ☐ |
| 29 | 遥控器侧不自动重配 | 处于第 28 项状态时静置 5 分钟 | **没有**任何自动连接尝试；扫描列表照常刷新；已学按键与槽位映射**都还在** | ☐ |
| 30 | 遥控器侧选择后重配 | 让遥控器进入配对模式 → 网页"附近设备"里点选它 | 旧遥控器 Bond 被删、所选设备连上并订阅；槽位映射不变；警告清除；D4 回到常亮 | ☐ |
| 31 | 指示灯休眠 | 静置 10 分钟不按键 | 两个灯都熄灭（含双闪态）；网页提示仍在 | ☐ |
| 32 | 休眠后唤醒 | 第 31 项之后按一下遥控器键 | D4 短暂亮一下（约 140 ms）后重新熄灭，**不**重新点亮十分钟 | ☐ |
| 33 | 重启/恢复出厂唤醒 | 第 31 项之后 `reboot`，再重复一次用 BOOT 长按恢复出厂 | 两种情况下两灯都重新按当前状态指示 | ☐ |

### 5.3.2 Improv 串口配网（免装软件配网，本轮新增）

用公开网页配网，需要桌面版 Chrome / Edge。页面用 https://web.esphome.io/ 的
Connect 即可（ESP Web Tools 那一套）。

**当前状态：编译通过（2026-09-16，零警告），下列各项一项未做。**

| # | 项目 | 操作 | 期望 | 结果 |
| --- | --- | --- | --- | --- |
| 34 | 端口被识别 | 打开网页点 Connect，看端口列表 | 列出 CH343 那个口；取消选择会弹出组件自带的驱动提示框 | ☐ |
| 35 | 设备信息 | 选口后页面读到的设备信息 | 名称 `Mi Remote Bridge`、固件 `MiRemoteBridge` / `v0.0.4`、芯片 ESP32-C3 | ☐ |
| 36 | **DTR 冲突（最要紧）** | 保持端口连接 30 秒以上，什么都不做 | 板子**不重启、不进下载模式、不恢复出厂**。若出现其中任一情况，就是 DTR→GPIO9 这条接线的问题 | ☐ |
| 37 | 配网成功 | 选 Wi-Fi、填密码、提交 | 串口出现 `[IMPROV] credentials for "..." received; joining`；随后网页自动跳到 `http://<ip>/`；`wifi status` 显示 `READY (sta)` | ☐ |
| 38 | 配错密码 | 故意填错密码 | 约 20 秒后网页提示连接失败，且**可以再试一次**（不会卡在"连接中"） | ☐ |
| 39 | 换网络重配 | 对已联网的板子再配一次另一个网络 | 切到新网络，配置页仍可访问（HTTP 监听器跨重连存活） | ☐ |
| 40 | 与控制台共存 | 配网客户端空闲时，在同一个口敲 `status` | 命令正常执行；`IMPROV` 之类的半截输入不会把命令吞掉 | ☐ |
| 41 | 扫描列表 | 在页面上请求扫描 | 列出附近网络；超过 6 个时会拆成多条结果，最后一条为空 | ☐ |

> 第 36 项是这一轮唯一**无法在代码里证明**的风险点。本板 USB 串口的 DTR 接在
> GPIO9（BOOT）上，而 Windows 打开串口时默认置位 DTR。固件已做防护（收到过
> Improv 帧后本次按住不再触发恢复出厂），但**如果浏览器在打开端口的瞬间就把
> 板子拉进下载模式，那是接线问题，需要改硬件**。

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
