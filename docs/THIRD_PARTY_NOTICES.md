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

本仓库源码以 **MIT** 许可发布，见根目录 `LICENSE`。
