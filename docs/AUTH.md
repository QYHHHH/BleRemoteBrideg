# AUTH.md — 配置页的访问机制：它做了什么、没做什么

> 结论先说：**没有认证**。配置页默认不在网络上。短按 BOOT 键可以打开一个 30 分钟的窗口，窗口期内任何人同网段都能访问。超时或再按一下都会重置倒计时。
>
> 本文记录的是**代码实际行为**（2026-09-16 改版，逐行核对 + 真机待验），不是设计意图。任何"应该"但代码没做的事，都写在"已知限制"里。

---

## 1. 机制全貌

| 环节 | 实际做法 |
| --- | --- |
| 开机 | **不自动连 Wi-Fi**。`wifi_ui::begin()` 不再 arm 自动启动。 |
| 打开窗口 | 两条入口：① **短按 BOOT** 键（`< 3 s`），② 串口 `wifi on`。页面**没有**续期按钮（用户明确不要）；`/api/window/extend` 这个端点还在，但只供脚本调用。 |
| 计时起点 | 板子**拿到 IP 地址的那一刻**（不是点击、不是访问、不是 BOOT 按下） |
| 计时长度 | 固定 **30 分钟**（`wifi_ui.cpp` 的 `kWindowMs`） |
| 计时终点 | 板子**主动关 Wi-Fi**：`WiFi.disconnect(true)` + `WiFi.mode(WIFI_OFF)`。HTTP listener 立即停止。BLE 不受影响。 |
| 倒计时显示 | 顶栏 `#pF` 胶囊，紧挨 WebSocket 胶囊，同样大小。板子在 **WebSocket 握手完成时**就发一次剩余秒数，之后每 5 s 心跳、`/api/status`、`/api/window` 都会带；网页把它换算成截止时刻后**自己在前端每秒递减**，所以标签页被浏览器降频后回来也是对的。 |
| 窗口结束提示 | 倒计时归零时页面正中弹出覆盖层 `#closedOverlay`：大字号 **BOOT** + "按一下 BOOT 键重新打开 30 分钟"，一直保留到窗口重新打开（WS 重连带回 `windowActive:true` 才自动收起）。 |
| Host 白名单 | `Host` 必须是 IPv4 字面量或 `<name>.local`，其它一律 403 —— 这是去密码后堵 DNS rebinding 的唯一手段。 |
| POST 跨域 | 仍然校验 `Origin == http://<Host>` + `Sec-Fetch-Site`（POST 分支）。 |
| 凭据 | 无。NVS 不再存密码哈希，不再签发会话 cookie，不再校验 WebSocket token。 |

## 2. 两条路由规则（曾经的认证现在砍掉了）

- `if (!hostIsLocal(host))` → 403。剩下的请求一律进 dispatch。
- `if (post) { crossSite || Origin 不匹配 || body 非空 }` → 403 / 400。这条**就是唯一的写保护**了。

## 3. 为什么去密码

2026-09-12 的 `docs/AUTH.md` 记录的那套机制做了这些事：

- 没密码时 302 到 `/setup`，改密要求 ≥4 位
- 登录走 HMAC-SHA1 签名的会话 cookie（7 天）
- WebSocket 走 `sha1(sha1(pass) + "mrb-ws")` 派生 token

实际保护力接近零：

- 密码最短 4 位，无失败计数、无锁定、无延迟（`AUTH.md §4.1`）
- 密码进 URL（GET 表单）、进浏览器历史
- SHA1 无盐、单轮；NVS 导出可秒破
- 派生 token 静态、会进浏览器历史
- 至少 5 个互不重叠的攻击路径（CSRF、DNS rebinding、setup 模式裸奔、WS 静态 token、爆破）

把这堆代码删掉，换成"按一下板子 30 分钟"之后，**净效果是正的**——

- 攻击窗口从"永远"变成"按下之后 30 分钟"
- 拿窗口的难度从"破解 4 位密码"变成"按一下板子"
- 顺带消灭了 5 个已知弱点的连带代码
- 关机后 BLE 受到的 2.4G 干扰更少（同一颗射频，要么 Wi-Fi 要么 BLE）

## 4. 已知限制

### 4.1 没有任何防爆破

无所谓了——窗口期就是攻击窗口，爆破没有意义。但**窗口期内任何攻击者都能改键映射、配对、清配对**。如果攻击者恰好在你按下 BOOT 之后的 30 分钟内扫描到 LAN，这 30 分钟他就是上帝。

> 缓解：30 分钟够短；攻击者必须物理上知道你按了 BOOT（你可以等他下班再按）；改键操作会被你肉眼看到（按键不再起作用）。

### 4.2 页面是明文 HTTP

密码和 cookie 都没了，但**键映射、按键记录、串口日志**还在明文上。同网段可被嗅探。设计前提不变：**不暴露到公网，不放在不可信的共享网络上**。

### 4.3 Host 白名单防 DNS rebinding，但 mDNS 名字仍可被攻击者注册

`hostIsLocal()` 接受 `<name>.local` 形式的 mDNS 主机名。理论上攻击者可以在路由器上劫持该名字（路由器不强制唯一性的话）。在家庭网络里这几乎不会发生，但记一下。

### 4.4 30 分钟窗口是 millis()，49.7 天溢出

页面一秒级刷新，溢出影响只在边界秒可见。下一窗口期会重新对齐。无操作性影响。

### 4.5 `WiFi.mode(WIFI_OFF)` 是否影响 BLE 共存未在改版时验证

ESP32-C3 单射频，理论上 BLE 栈独立不受影响。**这是必须上机验证的第一件事**——按 BOOT 拿 IP，关 Wi-Fi，观察按键是否仍转发。如有掉键，把 `WiFi.mode(WIFI_OFF)` 换成只 `WiFi.disconnect(true)`。

### 4.6 `Origin` 校验在 GET 上不做

写操作都是 POST，所以写保护仍然有效。但 GET 没有 CSRF 保护——攻击者网页可以任意读 `/api/status`、`/api/bindings`、`/api/nearby`。这本身不是漏洞（同源策略不拦"读响应"，攻击者本来就读不到响应），但意味着 `/api/bindings` 等含键映射的接口**没有认证保护**——任何能读到响应的攻击者（DNS rebinding 绕过 Host 校验的情况）能看到你的映射。

> 缓解：Host 白名单挡 DNS rebinding；30 分钟窗口缩短暴露时间。

## 5. 已验证 / 未验证

- **未验证**（本次改动大，真机未上）：
  - `WiFi.mode(WIFI_OFF)` 后 BLE 是否仍工作（**第一个必须验证**）
  - 短按 BOOT 是否真的触发联网（原"DTR 防护会不会漏掉某个真实工具"已作废：
    该防护 2026-09-17 整套删除，见 `TESTING.md` §5.3.2）
  - `/api/window` 倒计时是否与板子内部计时一致
  - 30 分钟到期后页面是否在 ~1 秒内看到 closed overlay
  - `WiFi.ap off` 命令、Improv 配网、串口 `wifi status` 输出是否都符合预期

> 改版太大，需要全量 `check_all.py` 走一遍。但其中两个脚本（`auth_guard_check.py`、`mobile_login_check.py`）已经删除——它们测的就是已经删掉的密码机制，留着会一直 FAIL。

## 6. 相关代码位置

| 位置 | 内容 |
| --- | --- |
| `wifi_ui.cpp` `hostIsLocal` | Host 白名单，DNS rebinding 防护 |
| `wifi_ui.cpp` `dispatch` | 去掉所有密码路由后的精简版 |
| `wifi_ui.cpp` `kWindowMs` / `s_windowStartedMs` | 30 分钟硬窗口 |
| `wifi_ui.cpp` `disableImpl` / `loop` 头 | 真关 Wi-Fi；到点自动调 |
| `wifi_ui.cpp` `/api/window` / `/api/window/extend` | 倒计时 + 续期端点（后者只给脚本用） |
| `wifi_ui.cpp` `wsSendStatus` + `handleStatus` 的 `windowActive`/`windowRemaining`/`windowAp` | WS 心跳**与** `/api/status` 必须都带这三个字段，否则页面会误判窗口已关闭 |
| `wifi_ui.cpp` `wsHandshake` | 握手一完成就发一次状态，页面不必等 5 s 心跳 |
| `reset_button.cpp` 短按分支 | 调 `wifi_ui::triggerRejoin()` |
| `web_page.h` `#pF` / `#closedOverlay` / `applyWindow` / `paintWindow` | 顶栏倒计时胶囊 + 归零覆盖层 |