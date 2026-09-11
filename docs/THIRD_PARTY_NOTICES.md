# 第三方代码与许可证说明

本项目在编写 RC003 协议相关代码时参考了第三方开源项目。以下是核查结果与处理方式。

---

## 1. RemoteMapper-ESP32

| 项目 | 内容 |
| --- | --- |
| 仓库 | https://github.com/cuicui-V5/RemoteMapper-ESP32 |
| 用途 | RC003 的 HID/ATVV 协议行为、原始键码数值、服务与特征 UUID |
| 许可证声明 | README 与 DEVELOPMENT.md 均写明 "本项目采用 [MIT License](./LICENSE) 协议开源" |
| **实际许可证文件** | **仓库中不存在 `LICENSE` 文件**（`git ls-files` 无匹配，根目录也没有）。见下方"许可证结论" |

### 参考了它的哪些事实

只使用了**协议层事实**与**常量数值**：

| 参考内容 | 位置（参考项目） |
| --- | --- |
| RC003 原始键码数值（`0x80` `0x81` `0xF1` `0x66` `0x24` `0x5D` `0xC0` `0x52` `0x51` `0x50` `0x4F` `0x28` `0x04` 及别名码） | `src/keymap/key_definitions.h`、README 按键表 |
| 标准 HID 键盘 Usage 与 Consumer Usage 数值 | `src/keymap/key_definitions.h` |
| ATVV 服务/特征 UUID（`ab5e0001..ab5e0004-...`） | `include/app_config.h` |
| HOGP 服务 `0x1812` / 报告特征 `0x2A4D` / 协议模式 `0x2A4E` / 控制点 `0x2A4C` | `include/app_config.h` |
| 报告长度启发式（1 / 2 / 3–7 / 8 字节，槽位从第 3 字节开始，全零=释放） | `src/ble/ble_remote_client.cpp` 的 `on_hogp_report_notify()` |
| 扫描识别线索（名称关键字、ATVV UUID、小米 OUI 前缀）、不匹配通用 `0x1812` 的理由 | 同文件 `is_target_remote()` |
| 保持麦克风关闭 / 不发送 `MIC_OPEN` 的结论 | 同文件 ATVV 处理分支的注释 |

这些内容属于**协议事实与数值常量**（接口事实、硬件行为观测），不是可受版权保护的表达。
本项目的所有源码均为自行编写：模块划分、事件队列、状态机、键映射、HID 报告描述符、
错误处理、防卡键机制、自检与测试框架、串口控制台等都不来自该仓库。

### 没有参考的部分

明确**没有**参考或搬运该项目的以下内容（它们属于 ESP32-S3 的 USB 方案，与本项目无关）：

- TinyUSB、USB HID、USB UAC、USB 复合设备描述符；
- 音频流水线（ADPCM 解码、AGC、滤波、环形缓冲、静音/淡入处理）；
- Wi-Fi 与 Web 后台/前端；
- 多层键映射、连发/长按/双击等宏逻辑（本项目按要求只做一对一转发）；
- PC 伴侣程序、刷写器、NVS 配置存储实现。

### 许可证结论与处理

该仓库 README 声明 MIT，但**仓库里没有 LICENSE 文件**，因此严格来说它的授权状态存在
不确定性。据此采取的保守做法：

1. **不复制其源代码**。只引用协议事实与常量数值，并从零实现；
2. 在本文件中保留来源署名与上述核查结论，便于后续追溯；
3. 若上游将来补上 `LICENSE` 文件，本项目对其事实性引用的使用方式与 MIT 兼容；
4. 若上游许可证与 MIT 不符，需要复核的只是"协议事实引用"这一部分——源码本身没有派生关系。

> 这不是法律意见。若要对外分发，建议向上游作者确认许可证，或仅保留协议事实引用并删除本节
> 中与实现细节重合的描述。

---

## 2. Arduino-ESP32 核心与 BLE 库

| 项目 | 内容 |
| --- | --- |
| 组件 | `esp32:esp32` 3.3.11 平台及其内置 `BLE` 库 |
| 路径 | `C:\code\arduino-c3-data\data\packages\esp32\hardware\esp32\3.3.11` |
| 许可证 | Apache License 2.0（见 `libraries/BLE/LICENSE`） |
| 使用方式 | 作为**依赖库**直接链接调用，未修改其源码，未复制其代码进本仓库 |

本仓库不包含该核心的任何源码副本。`arduino-cli.yaml` 只指向外部安装目录，工具链本身
被 `.gitignore` 排除在版本控制之外。

使用的关键 API（均为公开接口）：

- `BLEDevice` / `BLESecurity`：单次 `init()`、Just Works 绑定、功率设置；
- `BLEScan` / `BLEAdvertisedDevice`：上游扫描；
- `BLEClient` / `BLERemoteService` / `BLERemoteCharacteristic`：上游连接、服务发现、通知订阅；
- `BLEServer` / `BLEHIDDevice` / `BLECharacteristic` / `BLEAdvertising`：下游 HID 外设；
- 直接调用 NimBLE host 的两个 API 绕过封装缺口：`ble_store_util_delete_peer()` /
  `ble_store_clear()`（封装层没有暴露删除 Bond 的接口）、`ble_gap_security_initiate()` /
  `ble_gap_conn_find()`（避免调用会永久阻塞的 `secureConnection()`）。
  这两个头文件来自 ESP-IDF 的 NimBLE 组件（同样为 Apache-2.0）。

---

## 3. 本项目的许可证

本仓库源码以 **GPL-3.0-or-later** 许可发布，见根目录 `LICENSE`。
（2026-09-11 起：最初为 MIT；应作者决定改为 GPL-3.0 以保持衍生项目同样开源，
历史提交仍可在 Git 中追溯。）

---

## 4. NimBLE-Arduino（仅作为实现方式的核实依据）

| 项目 | 内容 |
| --- | --- |
| 仓库 | https://github.com/h2zero/NimBLE-Arduino |
| 许可证 | Apache License 2.0 |
| 使用方式 | **未引入、未链接、未复制代码。** 仅在排查 §4.6 时阅读其 `NimBLEService.cpp`，用来核实"底层 NimBLE 是否允许一个服务里有两条同 UUID 特征"这个问题 |

**为什么值得记录**：本项目的 `hid_gatt.cpp` 之所以能给出两条 `0x2A4D` 特征，前提是
"NimBLE 本身没有 UUID 唯一性限制"。这个结论是通过阅读 NimBLE-Arduino 的实现得到的——
它用 `std::vector` 存特征，`addCharacteristic()` 只比较指针而不比较 UUID，`getCharacteristic()`
甚至带 `idx` 参数用于取"同 UUID 的第几条"。

不过它所采用的做法（构建 `ble_gatt_chr_def[]` → `ble_gatts_count_cfg()` →
`ble_gatts_add_svcs()`）**与 Arduino-ESP32 核心自带的 `BLEService::start()` 完全一致**——
本项目也是照这条既有路径写的，并未搬运 NimBLE-Arduino 的代码。

本项目仍然只使用 Arduino-ESP32 核心自带的 NimBLE 主机栈，**没有引入任何第三方 BLE 库**。

---

---

## 5. GetSayAll/remote-mic-app-windows（按键映射 UI 的设计参考）

| 项目 | 内容 |
| --- | --- |
| 仓库 | https://github.com/GetSayAll/remote-mic-app-windows |
| 许可证 | GPL-3.0-only（Logo/Icon 为专有品牌资产，见其 LOGO-LICENSE.md） |
| 使用方式 | **布局与交互设计参考**：本项目 Web UI 的按键映射页（按键卡片环绕遥控器、逐键配置槽、"恢复默认"）在信息架构上参考了它的同名页面 |

**未做的事**：没有复制它的任何源码、图片或图标文件（本项目页面为独立实现，
图标使用 Unicode 字符而非其素材）；没有采用它的单击/双击/长按三段手势模型——
本项目一个按键只对应一个桌面动作，按下与抬起实时转发。

**为什么致谢**：它对 RC003 在 Windows 上的按键码、配对行为和"HOGP 报文里塞私有键码"
这一事实的公开记录，与本项目独立测得的结果互相印证。

---

## 6. cuicui-V5/RemoteMapper-ESP32（最早参考的同类项目）

| 项目 | 内容 |
| --- | --- |
| 仓库 | https://github.com/cuicui-V5/RemoteMapper-ESP32 |
| 许可证 | 仓库声明 MIT，但未随附 LICENSE 文件（见 §1 的谨慎处理） |
| 使用方式 | 只引用其公开记录的**协议事实与常量数值**（RC003 原始键码、UUID、报文长度启发式），未复制源码 |

其记录的键码帮助本项目起步；后续真机核对纠正了其中 4 个键码（主页/菜单/电视/语音），
纠正后的实测值已作为主码、原记录值降为同义词，见 [`KEYMAP.md`](KEYMAP.md) §1。

---

## 7. 许可证变更记录

- 2026-09-09：项目以 MIT 起步（当时仅参考了无 LICENSE 文件的仓库，故按宽松许可处理）。
- 2026-09-11：应作者决定改为 **GPL-3.0-or-later**：Web UI 的布局参考了 GPL-3.0 的
  remote-mic-app-windows，作者希望整个项目与其参考来源保持同一种 copyleft 精神。
  变更前后的历史提交均可在 Git 中追溯。
