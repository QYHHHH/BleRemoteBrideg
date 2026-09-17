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
| `bind <raw> kb <mod> <key>` / `bind <raw> cons <usage>` / `bind <raw> none` / `bind list` | 按原始码直接绑定 HID 输出，与页面的按键映射同步 |
| `channel <report\|consumer> <hex>` | 见 [`PAIRING.md` §4](PAIRING.md) |
| `selftest` / `sim` | 设备端自检（向量 + 分发仿真） |
| `slot <0-2>` / `slot` | 切换遥控器槽位 / 列出三个槽位与当前活动槽位 |
| `wifi on` / `wifi off` | 开启 / 关闭 Web 配置页（STA，加入已保存网络；关闭立即断 Wi-Fi、停 HTTP） |
| `wifi status` | 查看配置页状态：地址、剩余窗口时间、堆余量 |
| `wifi join <ssid> <pass>` | 保存配置页要加入的网络（开放网络用 `""`） |
| `wifi ap on` / `wifi ap off` | 退回 / 关闭 AP 热点模式（无可用 Wi-Fi 时的兜底，堆占用更高） |
| `heap` | 打印堆余量与最大连续块 |
| `reboot` | 软重启 |
| `factory` | 清除全部配对与设置并重启 |
| `help` / `?` | 打印命令列表 |

## 与上位工具的边界

- **PowerShell 的 `System.IO.Ports.SerialPort`** 可以读日志（用脚本 `scripts/monitor.ps1`），
  但 PowerShell 5.1 无法启动外部子进程，不要用 PowerShell 调 `arduino-cli` 等外部 exe——
  用 Bash 工具直接调。
- **控制线 DTR / RTS 是成对的，别单独看 DTR。** 监控脚本把**两根都设 `$false`**
  （`scripts/monitor.ps1` 已如此）就既不复位也不按住 BOOT；抓启动横幅前先
  `RtsEnable=true → 150ms → false` 故意复位一次。完整真值表、复位发生的确切
  时刻、`SER_NORESET=1` 用法见 [`PITFALLS.md`](PITFALLS.md#串口监控陷阱与-serial-consolemd-联动)。