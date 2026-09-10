# 小米蓝牙遥控器 ESP32-C3 桥接器

本项目计划使用 ESP32-C3 同时作为：

- BLE Central：连接小米蓝牙遥控器 2 Pro（RC003）；
- BLE HID Peripheral：向 Windows 提供标准蓝牙键盘和媒体键。

当前阶段：建立可复现的 Arduino CLI + Arduino-ESP32（ESP32-C3）本地开发环境。


## 本地工具链

- Arduino CLI：`1.5.1`，存放于 `.tools/arduino-cli-1.5.1`（不进入 Git）；
- Arduino-ESP32：`3.3.11`，存放于 `C:\code\arduino-c3-data`（不进入 Git）；
- 命令入口：`./scripts/arduino.ps1`；
- ESP32-C3 FQBN：`esp32:esp32:esp32c3`。

由于乐鑫 Windows RISC-V 链接器不能可靠处理中文路径，本机将工具链缓存实际存放在
`C:\code\arduino-c3-data`。编译时必须通过上述命令入口读取 `arduino-cli.yaml`。

验证编译：

```powershell
./scripts/arduino.ps1 compile --fqbn esp32:esp32:esp32c3 `
  --output-dir ./build/environment-check ./firmware/EnvironmentCheck
```