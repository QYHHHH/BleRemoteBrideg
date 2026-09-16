# 串口控制台参考

115200 波特率，UTF-8 编码。**这个 USB 口同时跑 Improv 配网协议**（见
[`README.md §快速开始`](../README.md#快速开始)），两者互不干扰：二进制帧被解析器
认领，文本命令照常执行。

## 命令一览

| 命令 | 作用 |
| --- | --- |
| `status` | 全量状态：两侧连接、bond、电量、队列、当前按键、内存 |
| `scan` / `scan now` | 查看扫描器缓存 / 开始新一轮扫描 |
| `connect <序号\|mac> [public\|random]` | 手动连接 |
| `reconnect` | 断开并重连遥控器 |
| `forget rc` / `forget win` | 清除遥控器侧 / 主机侧配对（见 `RECOVERY.md` 的双侧清规则） |
| `bond list` | 列出所有配对记录 |
| `key <hex> press\|release` | 注入合成按键（不接遥控器即可验证主机侧） |
| `raw on\|off` / `lat on\|off` / `log <0-4>` | 原始报文 / 延迟 / 日志级别 |
| `map back\|power\|voice <模式>` | 运行时键位，NVS 持久化 |
| `selftest` / `sim` | 设备端自检（向量 + 分发仿真） |
| `slot <0-2>` / `slot` | 切换遥控器槽位 / 列出三个槽位与当前活动槽位 |
| `wifi on` / `wifi status` | 开启 / 查看 Web 配置页（STA，加入已保存网络；`status` 打印地址与堆余量） |
| `wifi join <ssid> <pass>` | 保存配置页要加入的网络（开放网络用 `""`） |
| `wifi ap on` | 退回 AP 热点模式（无可用 Wi-Fi 时的兜底，堆占用更高） |
| `wifi off` / `wifi ap off` | **已废弃**：配置页常开，这两个命令只回一句拒绝提示 |
| `factory` | 清除全部配对与设置并重启 |

## 与上位工具的边界

- **PowerShell 的 `System.IO.Ports.SerialPort`** 可以读日志（用脚本 `scripts/monitor.ps1`），
  但 PowerShell 5.1 无法启动外部子进程，不要用 PowerShell 调 `arduino-cli` 等外部 exe——
  用 Bash 工具直接调。
- **绝对不要设 `DtrEnable=true`**。本板 DTR 接的是 BOOT（GPIO9），置位相当于按住 BOOT，
  固件 5 秒后会执行恢复出厂——实测踩过。监控脚本显式 `DtrEnable = $false`。
- 抓启动横幅前先 `RtsEnable=true → 150ms → false`，让板子干净复位。
- 见 [`PITFALLS.md`](PITFALLS.md) 里 DTR / RTS 释放顺序那条。