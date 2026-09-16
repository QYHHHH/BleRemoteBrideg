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
**已从按需改为常开**（只要存过 Wi-Fi 凭据，开机后自行起来；`wifi off` / `wifi ap off`
只回一句拒绝提示）。射频与内存是被 BLE 与 Wi-Fi 分摊的，但配置可达性优先。

**许可**：GPL-3.0-or-later（2026-09-11 应作者决定由 MIT 改为 GPL-3.0，全树 SPDX
已更新）。要开源发布，README 已按对外标准写好。

## 2. 当前状态

下表的能力条目为 **2026-09-12 真机实测**；此后（2026-09-13 起）又合入了三项，
列在表后。**全部核心功能真机可用**，工作树干净：

| 功能 | 验证 |
| --- | --- |
| 13 键转发（含媒体键），原始码实测 | ✅ 用户实测 |
| 电量透传（97–98%，NVS 持久化） | ✅ Windows 显示确认 |
| 重启到按键生效 3.3 秒 | ✅ 实测（原始 9.5 秒） |
| Windows 键盘/媒体、蓝牙关开重连 | ✅ 用户实测 |
| iPhone 连接（音量/方向键） | ✅ 用户实测 |
| 多主机轮换（Windows↔iPhone <0.1s 接管） | ✅ 实测 |
| BOOT 键：短按无操作 / 长按 5s 恢复出厂 | ⚠️ 代码完成已烧录，物理按压待用户验证 |
| **Web 配置 UI（页面注册/改键/实时按键）** | ✅ **真 Chrome 端到端 8/8 通过**（`tests/tools/browser_check.py`，读 live DOM，含按键实时推送） |
| `bind` 串口命令（设置/列表/解除） | ✅ 实测 |

**2026-09-13 之后合入的**（不在上表的旧实测范围内，验证程度见各自提交）：

| 改动 | 验证 |
| --- | --- |
| Improv 串口配网（`3860ead`，兼容 esp-web-tools / web.esphome.io） | 与串口控制台共用 USB 口；DTR 会拉 GPIO9，已有防护 |
| 版本号构建期覆盖 `v0.0.4` + 调试后缀（`fa26dbd`） | 见 `AGENTS.md` 的版本约定 |
| DTR 防护的代码依据（`f3b4a3a`） | 收到过 Improv 帧就不再触发 5 秒恢复出厂 |

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
  **⚠️ 绝对不要设 `DtrEnable=true`**：本板 DTR 接的是 BOOT（GPIO9），置位等于
  按住 BOOT，固件 5 秒后就会执行**恢复出厂**——实测踩过，代价是清空全部配对、
  Wi-Fi 凭据、网页密码与按键映射。只用 RTS。
  另外**释放顺序是承重的**：pyserial 打开端口时 DTR 与 RTS **默认都置位**，
  随后必须**先放 DTR、再放 RTS**。反过来的话，EN 拉高（复位释放）那一刻 GPIO9
  还是低的，芯片会**进下载模式、固件根本不跑**，症状只是"串口没输出"。
  `tests/tools/board_auth.py` 的 `_SER.dtr = False` / `_SER.rts = False` 两行
  别调换顺序（已加注释）。
  **"只收不发"的串口软件不等于危险**：危险的是"DTR 被置位"，与它发不发数据无关。
  已实测 MobaXterm 普通串口不置位 DTR。判断方法：打开软件后看日志有没有
  `[BUTTON ] key pressed`。详见 README 的 ⚠️ 块与 TESTING §5.3.2。
- **测试**：`python tests/model/check_vectors.py`（宿主端 11098 断言）；设备端
  `selftest` 串口命令。改解析/键表/HID 描述符后两者都要跑。
  页面断言 `python tests/tools/check_web_ui.py`（**161 项**，见
  `EXPECTED_CHECKS`；README/TESTING 里出现过 125/150 都是旧值）。
- **Git**：关键节点自动提交，**中文提交信息**（见 `AGENTS.md`）并注明验证程度
  （编译验证 vs 真机验证）。已有 103 次提交，历史在 Git 里可追溯，不要重建仓库。
- **文档**：用户要求项目开源，README 面向对外发布；一切"实测/未实测"严格区分。
- **Wi-Fi 是常开的，关不掉**：`wifi off` / `wifi ap off` 都被明确拒绝
  （见 `cli.cpp` 的 wifi 分支与 `wifi_ui.h` 的 ALWAYS-ON 注释）。**别再写
  "不用时 `wifi off`" 这类指引**——它已经不成立，README 早期版本也留过这种残留。
  Wi-Fi 与 BLE 共存吃 ~50KB 堆 + 射频时间，这是已知代价，不是待办。

## 6. 已知问题 / 注意

- 板子 USB 偶发掉线（`CM_PROB_PHANTOM`）——硬件/线材问题，重插即可。
- `bind` 的 keycode 语义约定**全 hex**（`bind 3e ...` = 0x3E 语音键）。
- Web UI 现在走 **STA 模式**（加入家里 Wi-Fi 拿 DHCP 地址，`wifi on` 会打印，
  也记在 `build/.boardip`），不再是 AP 热点。原因：AP 模式下 DHCP 在 BLE 共存时的
  ~13.5 KB 堆水位起不来（客户端关联成功却拿不到 IP → `169.254.x.x`），见
  `docs/TESTING.md` §4.13。**所以别再按 "192.168.4.1 热点" 那套流程测。**
- **socket 只推小帧**（帧上限 768 B）：整张绑定表走 HTTP。往 socket 里塞大 JSON 会被
  丢弃，这是曾经卡死页面一整天的根因，见 `docs/HANDOFF-websocket-debug.md` §4。
- **HTTP 只有一个连接槽，且必须"没人用就立刻放"**。三种状态三种预算：空闲（答完等复用）
  300 ms / 还没发过请求（预测性连接）250 ms / 请求或响应在动 4 s。把中间那种并到 4 s 里，
  会让手机端的预测性连接把槽位堵死 —— 实测三条静默连接让真实请求等 29 s，
  表现为"手机登录不进去"。见 `docs/HANDOFF-websocket-debug.md` 根因 5。
- **认证机制的现状与已知弱点**（无防爆破、token 走 URL、WS token 按密码静态、
  cookie 固定 7 天且**无法提前失效**（只有改密码才会作废旧会话）、sha1 无盐、无 TLS）
  见 `docs/AUTH.md`。**别把它的防护能力说得比实际强**；
  （旧版写的"cookie 到期语义依赖开机时长"是错的：代码里 `kSessionSeconds` 是
  `7 * 24 * 3600` 的固定值，与开机时长无关，见 `wifi_ui.cpp`。）
  前提是**仅限局域网，不要暴露到公网**。
- **认证层的两条路由规则互为镜像，改动前先读 `docs/AUTH.md` §1/§3**：
  无密码时除 `/setup`、`/ws`、`/api/token` 外一律跳 `/setup`（`/login` 也跳，否则是一个
  永远拒绝的死路表单）；有密码时除 `/login`、`/logout` 外一律要有效会话（`/setup` 也要，
  否则它就是任何人都能用的改密接口）。这两条都在 2026-09-12 修过，别再退回。
- **`Cookie:` 的值必须按"行"结束，不能按 `;` 或缓冲区末尾**（`wifi_ui.cpp` 的 `cookieValue`）。
  请求头里没有 `;`，按 `;` 找结尾会把后面的头一起吞进 cookie 值 → base64 解码失败 → **静默丢会话**。
  实测：同一个 cookie，`Cookie` 后面多一个 `Connection: close` 就从 200 变 302 `/login`。
  **浏览器通常把 `Cookie` 放在最后，所以这条 bug 对浏览器不可见——"浏览器能用"不等于实现对。**
- NFC 是小米私有智能卡，与蓝牙零交互，桥接器无 NFC 硬件——不要再朝这个方向探索。
- `ble_store_config` 的 bond 淘汰（删最旧）是预编译库行为；`makeRoomForPeer` 已在
  配对前腾位保护遥控器，勿移除。

- **烧录前必须用 `--output-dir build/MiRemoteBridge` 重新编译**。平时为验证写的
  `arduino-cli compile` 不带这个参数，`build/MiRemoteBridge` 里的 bin 就一直是旧版，
  直接 `upload --input-dir build/MiRemoteBridge` 会把**旧固件烧进去**，而且一切看起来正常。
- **`arduino-cli upload` 不擦 NVS**（反复确认）：密码、自定义绑定、绑定的遥控器、
  主机配对都在，烧完自动重连。烧后验证顺序：串口读 `firmware :` 与 `NVS loaded` →
  登录（有密码的板子读 `/api/*`、`/app.css` **必须带 cookie**，否则一律 302）后确认
  页面资产真的换了 → 读 `/api/status` 确认 `fwVersion` / `buildTime`。
- **顶栏版本与编译时刻来自 `/api/status`**（`fwVersion` + `__DATE__ __TIME__`），
  页面里**不再存任何构建时间戳**：`gen_web_page.py` 以前把"生成 gzip 资源的时刻"
  压进载荷，页面显示的不是编译时刻（出现过 12:56 烧录、页面显示 10:21）。
  副作用是 `web_page_gz.h` 现在**确定性生成**，跑 `test.ps1` 不再弄脏工作区。

## 7. 立即可做的验证

```powershell
# 物理按键（需要人手）：短按 BOOT 应只出日志不重启；长按 5s 清全部配对（危险）
# 四项硬件检查一次跑完（前提：板子 Wi-Fi 可达 —— 先 python build/wifi_on.py）：
#   python tests/tools/check_all.py
#     auth_guard_check   HTTP 层：认证路由/改密门/会话 cookie/token 门（不需要浏览器）
#     preconnect_probe   socket 层：槽位是否及时释放（只发 HTTP，不重启板子、不碰密码）
#     browser_check      页面层：真 Chrome 读 live DOM（渲染/绑定/WS/按键实时推送）
#     mobile_login_check 手机层：模拟 iPhone 走真实表单完成设置密码与登录
#   ⚠ 后三个会 `pass clear` 板子上的密码，跑完板子处于"首次设置"状态，
#     /login 会拒绝一切输入。check_all.py 结尾会打印它留下的认证状态。
#     （`pass clear` 本身不重启；重启是"打开串口"那一下的 RTS 脉冲造成的，
#      所以只有开端口的那次调用需要等，见 tests/tools/board_auth.py）
# 不接板子先跑宿主侧全套（含页面断言，约 1 秒，只需要 Chrome + Python）：
#   .\scripts\test.ps1
#   python tests/tools/check_web_ui.py            # 闪存预算 + 161 项页面断言
#
# 两个可选环境变量（都没配时脚本会明确报错，不会猜）：
#   MRB_IP=<板子地址>        板子走 DHCP，地址会变；`wifi on` 会写入 build/.boardip
#   MRB_PYSERIAL=<目录>      pyserial 不在默认 site-packages 时指一下
#   MRB_PW=<密码>            browser_check 用的测试密码（默认 mrbtest99，只用于测试）
```
