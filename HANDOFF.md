# HANDOFF.md — 交接给下一位 AI 维护者

> 本文件是 AI 会话之间的交接清单。同一工作区的新会话还会自动加载
> `.workbuddy/memory/MEMORY.md`（长期约定）与每日日志 `2026-09-10.md`（完整过程），
> 三份材料配合使用。动手前请先读完本文件和 README.md。

---

## 1. 这是什么项目

**MiRemoteBridge**：ESP32-C3 双角色 BLE 桥接固件。小米蓝牙遥控器 2 Pro（RC003）
把私有键码塞在 HOGP 报文里，Windows/iOS 无法直接使用；本固件以 Central 身份连接
遥控器，把键码翻译成标准 HID 报告，再以"蓝牙键盘 + 媒体设备"的外设身份发给主机。
免驱、免伴侣程序。

**边界**（用户明确的红线）：只做按键转发；不做音频/USB/驱动/注入/宏。Web 配置界面
**默认不在网络上**——开机不自动连 Wi-Fi，**短按 BOOT 键**或串口 `wifi on` 打开
30 分钟窗口（计时从拿到 IP 起），到点主动关 Wi-Fi（`WiFi.disconnect(true)` +
`WIFI_OFF`，BLE 不受影响）。续期就再按一次 BOOT 或再 `wifi on`；页面**没有**
续期按钮（用户明确不要），`/api/window/extend` 只留给脚本用。射频与内存是
被 BLE 与 Wi-Fi 分摊的，但配置可达性优先这条不变。

**许可**：GPL-3.0-or-later（2026-09-11 应作者决定由 MIT 改为 GPL-3.0，全树 SPDX
已更新）。要开源发布，README 已按对外标准写好。

## 2. 当前状态

下表的能力条目为 **2026-09-12 真机实测**；此后（2026-09-13 起）又合入了三项，
列在表后。**全部核心功能真机可用**。

> **版本现状**：`config.h` 与 tag 都是 `v0.0.6`（2026-09-17 发布点）。板子上
> **实际跑的是调试版 `v0.0.5-nosession`** —— 它与 v0.0.6 是**同一份源码**，只差版本
> 字符串；按 `AGENTS.md` 的约定调试版不带 tag。所以"串口横幅报
> `v0.0.5-nosession`、仓库报 `v0.0.6`"是**预期**的，不是版本漂移。要让板上也报
> v0.0.6，跑一次 `flash.ps1 -Port COM3`（**不带** `-Version`）即可。
>
> **BOOT 键两个分支都已真机验收**（2026-09-17，用户实测）：短按打开/续期 30 分钟
> 窗口、长按 5 秒恢复出厂、恢复出厂后重新加键、连电脑按键有效 —— 全部通过。
> 自动化回归见 §7 的 `tests/tools/press_probe.py`。

| 功能 | 验证 |
| --- | --- |
| 13 键转发（含媒体键），原始码实测 | ✅ 用户实测 |
| 电量透传（97–98%，NVS 持久化） | ✅ Windows 显示确认 |
| 重启到按键生效 3.3 秒 | ✅ 实测（原始 9.5 秒） |
| Windows 键盘/媒体、蓝牙关开重连 | ✅ 用户实测 |
| iPhone 连接（音量/方向键） | ✅ 用户实测 |
| 多主机轮换（Windows↔iPhone <0.1s 接管） | ✅ 实测 |
| ~~BOOT 键：短按无操作~~ **已变更** / 长按 5s 恢复出厂 | 短按现在打开 30 分钟配置窗口（2026-09-16 起，见下表 Web 配置 UI 改动），长按 5s 恢复出厂不变 |
| **Web 配置 UI（页面注册/改键/实时按键）** | ✅ **真 Chrome 端到端 8/8 通过**（`tests/tools/browser_check.py`，读 live DOM，含按键实时推送） |
| `bind` 串口命令（设置/列表/解除） | ✅ 实测 |

**2026-09-13 之后合入的**（不在上表的旧实测范围内，验证程度见各自提交）：

| 改动 | 验证 |
| --- | --- |
| Improv 串口配网（`fd7d14a`，兼容 esp-web-tools / web.esphome.io） | 与串口控制台共用 USB 口；控制线真值表见 `docs/WEB-UI.md` |
| 版本号构建期覆盖 `v0.0.4` + 调试后缀（`a5dd8ad`） | 见 `AGENTS.md` 的版本约定 |
| ~~DTR 防护（`71a72f6`）~~ **已撤销** | 前提"浏览器置位 DTR = 按住 BOOT"2026-09-17 实测证伪，`sessionActive()` 整套删除 |

**待办**：`docs/TESTING.md` §5.3 的边界清单（长按、连按、休眠唤醒、两侧同时重启、
卡键）尚未逐项验收。（旧版这里还写着"README 提到的英文版/CI 未做"——
README 早已不含英文版与 CI 的计划，那条已删除。）

**Web UI 的修复过程与必须守住的不变量**见 `docs/HANDOFF-websocket-debug.md`
（该文件已从"排查任务书"改写为结案记录 —— 它原来的三个猜测**全是错的**，别再照着排查）。


## 3. 硬环境

- 板子：**合宙 CORE-ESP32**（CH343 USB 串口，BOOT 键=GPIO9，RESET=EN）
- 串口：**COM3**（板子 USB 连接偶发掉线 `CM_PROB_PHANTOM`，重插/换线解决——不是固件问题）
- 遥控器：RC003 `c0:5d:39:xx:xx:xx`（公开地址，广播名中文「小米蓝牙语音遥控器」）
- bond 槽 3/3：RC003 + Windows(`xx:xx:xx:xx:xx:xx`) + iPhone(`xx:xx:xx:xx:xx:xx`)
- **FQBN：`esp32:esp32:esp32c3:FlashMode=dio,PartitionScheme=huge_app`**（缺一不可，
  原因见 §4）
- 工具链：`.tools/arduino-cli-1.5.1` + `C:\code\arduino-c3-data`（均在仓库外，
  **不要重装、不要移入仓库**，后者必须纯 ASCII 路径）

## 4. 硬性规则（违反任何一条都会踩已知的坑）

1. **FQBN 必须带 `FlashMode=dio`**：默认 QIO 下本板 Macronix flash 读回全 0xFF，
   bootloader 复位循环。40MHz+QIO 也失败，只有 DIO 能启动。
2. **必须 huge_app 分区**：Wi-Fi 配置栈超出默认 1.2MB app 分区，text 溢出。
3. **HID 服务结构 = 两个顶层集合、各自报告 ID、两条 `0x2A4D` 特征**，由
   `hid_gatt.cpp` 直接用 NimBLE `ble_gatt_svc_def` 构建。**不要**改回单集合或
   共用报告 ID（Windows Code 10）、**不要**用 `BLEHIDDevice`（同 UUID 第二条特征
   会被封装层丢弃 → 野指针崩溃）。细节 `docs/KEYMAP.md` §5、`docs/TESTING.md` §4.6-4.7。
4. **BLE 回调只入队**（`event_bus::post`），HID 报告由 `loop()` 发送。
5. **清配对必须两侧同时清**（桥接器 + 主机蓝牙里删设备）。单侧清 → 密钥不一致 →
   每 6–7 秒断开重连循环。
6. **地址一律用 `BLEAddress::toString()` 的规范文本**；`getNative()` 是逆序字节，
   拼回 `BLEAddress` 会得到反序地址。
7. **未知键码宁可漏发**（记 WARN），绝不盲发。
8. **`getCharacteristics()` 是惰性调用**——过滤条件要放在它**之前**（§4.8 教训）。
9. **验证措辞**：没上机验证的必须写"编译验证/代码级验证"，不得声称通过硬件测试。
10. **Bond 保护**：任何腾位逻辑不得删除 `settings::rc003Address()` 对应的 bond
    （`ble_bonds::makeRoomForPeer` 已实现，别破坏）。

## 5. 工作方式

- **编译/烧录**：`./scripts/build.ps1` / `flash.ps1 -Port COM3`（bash 环境可直接调
  `./.tools/arduino-cli-1.5.1/arduino-cli.exe --config-file ./arduino-cli.yaml compile
  --fqbn "esp32:esp32:esp32c3:FlashMode=dio,PartitionScheme=huge_app" --output-dir
  ./build/MiRemoteBridge ./firmware/MiRemoteBridge`）。**修改后必须实际编译。**
- **抓串口**：Bash 调 `.exe` 可用；PowerShell 工具**无法启动子进程**（沙箱限制），
  但 .NET `SerialPort`（PowerShell 内）可用且要设 `Encoding=UTF8`（否则中文广播名
  变 `?????`）。抓启动横幅先 `RtsEnable=true → 150ms → false`。
  **控制线 DTR / RTS 是成对的**（经典双三极管自动下载电路），别单独看 DTR——
  完整真值表、复位发生的确切时刻、`SER_NORESET=1` 用法、取证脚本清单，
  见 [`docs/PITFALLS.md`](docs/PITFALLS.md#串口监控陷阱与-serial-consolemd-联动)
  与 `docs/TESTING.md` §5.3.2。判据：日志里有没有 `[BUTTON ] key pressed`——
  有说明 RTS 是低的（等于按住 BOOT），不是"DTR 被置位"。
- **测试**：`python tests/model/check_vectors.py`（宿主端 11098 断言）；设备端
  `selftest` 串口命令。改解析/键表/HID 描述符后两者都要跑。
  页面断言 `python tests/tools/check_web_ui.py`（**161 项**，见
  `EXPECTED_CHECKS`；README/TESTING 里出现过 125/150 都是旧值）。
- **Git**：关键节点自动提交，**中文提交信息**（见 `AGENTS.md`）并注明验证程度
  （编译验证 vs 真机验证）。历史在 Git 里可追溯（`git log`），不要重建仓库。
- **文档**：用户要求项目开源，README 面向对外发布；一切"实测/未实测"严格区分。
- **Web 配置是按需 30 分钟窗口，**不是**常开**：`wifi on` 打开窗口（计时从拿到
  IP 起），`wifi off` 现在**真的能关**。**短按 BOOT 键**也是打开入口，且会重置
  倒计时。实现见 `cli.cpp` 的 wifi 分支、`wifi_ui.cpp` 的 `kWindowMs` 常量、
  `wifi_ui.h` 的头注释。`AUTH.md` 写了"为什么这么做"（替代旧密码机制）。
  Wi-Fi 与 BLE 共存吃 ~50KB 堆 + 射频时间，这是已知代价。

## 6. 已知问题 / 注意

- 板子 USB 偶发掉线（`CM_PROB_PHANTOM`）——硬件/线材问题，重插即可。
- `bind` 的 keycode 语义约定**全 hex**（`bind 3e ...` = 0x3E 语音键）。
- Web UI 在窗口期内走 **STA 模式**（加入家里 Wi-Fi 拿 DHCP 地址，地址记在
  `build/.boardip`）。`wifi ap on` 仍然是兜底热点。原因：AP 模式下 DHCP 在 BLE
  共存时的 ~13.5 KB 堆水位起不来（客户端关联成功却拿不到 IP → `169.254.x.x`），见
  `docs/TESTING.md` §4.13。**所以别再按 "192.168.4.1 热点" 那套流程测。**
- **socket 只推小帧**（帧上限 768 B）：整张绑定表走 HTTP。往 socket 里塞大 JSON 会被
  丢弃，这是曾经卡死页面一整天的根因，见 `docs/HANDOFF-websocket-debug.md` §4。
- **HTTP 只有一个连接槽，且必须"没人用就立刻放"**。三种状态三种预算：空闲（答完等复用）
  300 ms / 还没发过请求（预测性连接）250 ms / 请求或响应在动 4 s。把中间那种并到 4 s 里，
  会让手机端的预测性连接把槽位堵死 —— 实测三条静默连接让真实请求等 29 s，
  表现为"手机登录不进去"。见 `docs/HANDOFF-websocket-debug.md` 根因 5。
- **配置页没有认证**（2026-09-16 改版）：不设密码，改成**短按 BOOT 开 30 分钟窗口**，
  从拿到 IP 起算，到期板子主动关 Wi-Fi。开机不自动连。改动前先读 `docs/AUTH.md`。
  旧机制（4 位密码 + HMAC cookie + 派生 WS token）的实测弱点已随代码一起删掉，
  `docs/AUTH.md` §3 留了删除理由。**别把新机制的防护能力说得比实际强**：
  窗口期内同网段任何人都能改键映射，它防的是"默认不在线"，不是"在线时的鉴权"。
- **`Host` 头白名单不能删**（`wifi_ui.cpp` 的 `hostIsLocal`）：只接受 IPv4 字面量
  与 `<name>.local`。去掉密码之后这是堵 DNS rebinding 的唯一手段——恶意网页在浏览器里
  把域名重绑定到板子 IP，此时 Origin 与 Host 都是攻击者的域名，POST 的跨域校验会放行。
- 前提是**仅限局域网，不要暴露到公网**。HTTP/WS 仍是明文（键映射、按键事件可被嗅探）。
- NFC 是小米私有智能卡，与蓝牙零交互，桥接器无 NFC 硬件——不要再朝这个方向探索。
- `ble_store_config` 的 bond 淘汰（删最旧）是预编译库行为；`makeRoomForPeer` 已在
  配对前腾位保护遥控器，勿移除。

- **烧录前必须用 `--output-dir build/MiRemoteBridge` 重新编译**。平时为验证写的
  `arduino-cli compile` 不带这个参数，`build/MiRemoteBridge` 里的 bin 就一直是旧版，
  直接 `upload --input-dir build/MiRemoteBridge` 会把**旧固件烧进去**，而且一切看起来正常。
- **`arduino-cli upload` 不擦 NVS**（反复确认）：自定义绑定、绑定的遥控器、
  主机配对、Wi-Fi 凭据都在，烧完自动重连。烧后验证顺序：串口读 `firmware :` 与
  `NVS loaded` → 短按 BOOT 开窗口后读 `/api/*`、`/app.css` 确认页面资产真的换了 →
  读 `/api/status` 确认 `fwVersion` / `buildTime`。
- **顶栏版本与编译时刻来自 `/api/status`**（`fwVersion` + `__DATE__ __TIME__`），
  页面里**不再存任何构建时间戳**：`gen_web_page.py` 以前把"生成 gzip 资源的时刻"
  压进载荷，页面显示的不是编译时刻（出现过 12:56 烧录、页面显示 10:21）。
  副作用是 `web_page_gz.h` 现在**确定性生成**，跑 `test.ps1` 不再弄脏工作区。

## 7. 立即可做的验证

```powershell
# BOOT 键（不需要人手）：python tests/tools/press_probe.py
#   把控制线驱动到 (1,0) 就是 GPIO9 拉低 = 按住 BOOT（实测结论），保持 1.2 s 即短按。
#   它先喂一个合法 Improv 帧再按，专门复现"客户端连着时按键被吞"那个故障；
#   最后一档按住 2.0 s 看恢复出厂倒计时，离 5 s 留 3 s 余量。
#   约束：任何一次 GPIO9 拉低都要远小于 5 s，且 finally 必须把两根线放回 (0,0)
#   ——残留 (1,0) 在芯片看来是卡住的按键，5 秒后自己清板子。
# 真按满 5 s 恢复出厂仍然只能人手（会清掉全部配对，脚本故意不自动化）：
#   短按 BOOT 应打开 30 分钟窗口；长按 5s 清全部配对（危险）
# 两项硬件检查一次跑完（前提：板子 Wi-Fi 在 30 分钟窗口内 —— 短按 BOOT 或 wifi_on.py）：
#   python tests/tools/check_all.py
#     preconnect_probe   socket 层：槽位是否及时释放（只发 HTTP，不重启板子、不改键）
#     browser_check      页面层：真 Chrome 读 live DOM（渲染/绑定/WS/倒计时/按键推送）
#   跑完板子保持在原状态；与旧的"会清密码"不同，不再需要收尾步骤。
# 注意：配网页开着（Chrome 的 Web Serial）会独占 COM3，烧录前必须先关标签页。
# 不接板子先跑宿主侧全套（含页面断言，约 1 秒，只需要 Chrome + Python）：
#   .\scripts\test.ps1
#   python tests/tools/check_web_ui.py            # 闪存预算 + 161 项页面断言
#
# 两个可选环境变量（都没配时脚本会明确报错，不会猜）：
#   MRB_IP=<板子地址>        板子走 DHCP，地址会变；`wifi on` 会写入 build/.boardip
#   MRB_PYSERIAL=<目录>      pyserial 不在默认 site-packages 时指一下
#   MRB_PW=<密码>            browser_check 用的测试密码（默认 mrbtest99，只用于测试）
```
