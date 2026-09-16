# 架构

本文件覆盖代码结构、关键设计决定与模块边界。配套总览图见
[`README.md`](../README.md) 顶部的架构图。

## 代码结构

```
firmware/MiRemoteBridge/
├── MiRemoteBridge.ino   入口：setup / loop
├── config.h             编译期开关与时机参数
├── key_definitions.h    RC003 原始键码 + 标准 HID Usage（纯 C，无依赖）
├── keymap.{h,cpp}       键码 → HID 动作映射 + 运行时可选模式（纯 C，无依赖）
├── rc003_report.{h,cpp} 报文解析 + 按下/松开状态机（纯 C，无依赖）
├── event_queue.h        固定容量无锁 SPSC 环形队列
├── bridge_events.h      事件类型定义
├── event_bus.{h,cpp}    全局事件队列实例
├── ble_core.{h,cpp}     唯一的 BLE 栈初始化点
├── ble_bonds.{h,cpp}    Bond 查询/删除/腾位保护
├── hid_gatt.{h,cpp}     下游 HID 服务：直接用 NimBLE ble_gatt_svc_def 构建
│                        （两条 0x2A4D 特征——封装层做不到，见 docs/KEYMAP.md §5）
├── hid_report_map.h     HID 报告描述符 + 报告布局常量
├── hid_server.{h,cpp}   下游 HID 外设封装（防卡键、订阅、电量）
├── rc003_client.{h,cpp} 上游：扫描/连接/服务发现/订阅，独立 FreeRTOS 任务
├── bridge.{h,cpp}       事件分发、按键状态、延迟测量、status
├── log.{h,cpp}          分级日志 + 令牌桶限速 + 不限流通道
├── cli.{h,cpp}          串口控制台
├── improv_serial.{h,cpp} Improv Wi-Fi 串口配网（与控制台共用同一个 USB 口）
└── selftest.{h,cpp}     设备端向量测试 + 分发仿真
```

## 关键设计决定

- **单一 NimBLE 主机栈，双角色并存。** Arduino-ESP32 3.3.11 的 C3 构建里
  `CONFIG_BT_NIMBLE_ENABLED=y`，核心自带 `BLE` 封装底层就是 NimBLE；
  `ble_core::begin()` 是唯一调用 `BLEDevice::init()` 的地方。
- **BLE 回调只入队。** 上游通知回调运行在 NimBLE host 任务，只做"解析 → 归一化 →
  入 SPSC 队列"；HID 报告全部由 `loop()` 发送，两角色不重入。
- **阻塞操作放独立任务。** 扫描、连接、发现、订阅由 `rc003` FreeRTOS 任务承担。
- **直连优先。** 保存的身份地址 + Bond 直连，失败才回退扫描；扫描占空比运行
  （20 秒扫描 / 3 秒停顿），给并存的下游链路留空口时间。
- **HID 服务绕过 `BLEHIDDevice` 直接建在 NimBLE 上。** HOGP 要求"每个报告 ID 一条
  Report 特征"，两条 `0x2A4D` 是必须的；Arduino 封装层按 UUID 去重注册不了第二条
  （详见 [`KEYMAP.md`](KEYMAP.md) §5），NimBLE 原生 `ble_gatt_svc_def` 没有这个限制。
- **未知键码宁可漏发。** 不在表内的码丢并记 WARN，绝不盲发。

## 上下游数据流

上游（`rc003_client` 任务）：通知回调解析 → 归一化成 `bridge_events.h` 中的事件 →
入 SPSC 队列。

下游（`loop()`）：出队 → `bridge::dispatch()` 维护按键状态 → `hid_server` 生成
HID 报告 → NimBLE notify。

整条链路**单线程出队**（loop），**入队在 NimBLE host 任务**，两角色不重入，
是"防卡键"三件套（幂等按下/松开 + 报告全量重建 + 任一侧断线立即清零）的实现前提。

## 槽位（多遥控器）

固件内部维护 3 个遥控器槽位（`settings.cpp` 的 `s_slot`）：

- 槽位 01 = 小米 RC003 预设（固定 13 键示意图、固定键表）
- 槽位 02 / 03 = 学习型（按一下键记录一个原始码，最多 128 个；标准 HID usage
  通过 `keymap_standard_name()` 自动显示键名）

切换命令：`slot <0-2>`。和 **bond 槽位**（预编译库上限 3 个）是两个独立概念：
bond 槽位是底层 BLE 配对记录数（1 给遥控器 + 2 给主机），遥控器槽位是上层
"哪个遥控器现在是主"的逻辑槽。