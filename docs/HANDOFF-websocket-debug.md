# 交接任务：修好 MiRemoteBridge 配置页的 WebSocket 建连

> 给接手的 Agent：这份文档是自包含的。**不需要读整个代码库**，按"关键代码位置"一节的
> 指引精准下钻即可。所有"已排除"的项都真机验证过，**不要重复验证**（会浪费大量时间，
> 而且这个环境的测试有很多坑，见"排查工具"）。

---

## 1. 项目一句话

ESP32-C3 双角色 BLE 桥接器：把小米 RC003 遥控器（BLE 中央）的按键转发成标准 HID
键盘/媒体键（BLE 外设）给 Windows。另有一个 Web 配置页用于改按键映射。

- 工具链：Arduino-ESP32 3.3.11，`arduino-cli` 在 `.tools/`，FQBN 必须带 `FlashMode=dio`
  （qio 会让 flash 读回 0xFF 导致复位循环）
- 设备 IP：`192.168.1.100`（路由器 DHCP，无密码时页面显示"设置访问密码"页）
- 编译产物：`build/MiRemoteBridge/`

## 2. 症状（唯一待修问题）

**页面完整加载了，但 WebSocket 建不起来。**

真机表现（用户浏览器截图）：

```
顶部栏:  MiRemoteBridge  RC003 CONTROL  build 2026-09-12 01:48   ← 新版本，页面是新的
横幅:    无法连接桥接器（连接断开，自动重连中）  [重新连接]
标题:    正在读取设备状态                                        ← 从未更新
```

即：HTML/CSS/JS 都拿到了、JS 也跑了（卡片、SVG 图标、连线都渲染），
`connectWS()` 走完 `/api/token` 后进入 WebSocket，然后**立刻 onclose**，
页面进入无限重连；`stat()` 从未被调用（状态文字停在初始值）。

## 3. 已排除（真机验证过，勿重复）

| 假设 | 验证方式 | 结论 |
| --- | --- | --- |
| 浏览器缓存旧页面 | 无痕 + 清站点数据 + 页面顶部 build 时间戳 | ❌ 排除（build 时间戳是新的） |
| JS 被截断 | curl `/app.js`（gzip 解压后 14915 字节，结尾 `connectWS();` 完整） | ❌ 排除（修过：EAGAIN 曾误判为停滞，已算作 progress） |
| WebSocket 独占 HTTP 槽 | WS 开着时 curl `/api/status` ×3 | ❌ 排除（3/3 200，已改为独立 fd） |
| token 为空 | curl `/api/token`（带 cookie）| ❌ 排除（返回 40 字符 hex） |
| `/ws` 握手失败 | curl 带 Upgrade 头 → `101 Switching Protocols` + Accept 值正确 | ❌ 排除（服务端握手本身没问题） |
| 设备 IP 变了 | ARP 查设备 MAC `60-55-f9-77-89-58` | ❌ 排除（仍是 .19） |
| 密码残留 | 串口 `pass clear` | ❌ 排除 |
| 设备不可达 | 注意：**大量"不可达"是测试脚本自身的时序 bug**（见第 5 节） | ❌ 排除 |

**关键差异**：`curl` 的握手**成功**，Chrome 的握手**失败**。所以问题在
**"Chrome 发的请求"与"curl 发的请求"的差异**上——这是首要排查方向。

## 4. 关键代码位置（精准下钻）

### 4.1 服务端：`firmware/MiRemoteBridge/wifi_ui.cpp`（约 1600 行，手写 socket 状态机）

| 函数 / 区域 | 作用 | 为什么重要 |
| --- | --- | --- |
| `dispatch()` | 解析请求行 + 遍历请求头 → 路由 | **请求头解析在这里**（`sscanf(s_http.io, "%7s %383s %11s", ...)`），Chrome 的头比 curl 长得多 |
| `pollHttp()` | 主循环：accept → recv → dispatch → 发送；含 WS 分支 | WS 用独立 fd（`s_wsFd`），HTTP 用 `s_http.fd` |
| `wsHandshake(const char *key)` | 计算 `Sec-WebSocket-Accept`、发 101 | key 来自 `dispatch()` 里对 `Sec-WebSocket-Key:` 的解析 |
| `wsConsume()` | 解析客户端帧（必须解掩码） | 浏览器发的帧**一定带掩码** |
| `sessionMatches()` / `readSessionCookie()` | HMAC cookie 会话校验 | `/ws` 的 token 校验与 cookie 无关（token 在 query 里） |
| 常量 `kHeaderLimit` | 请求头缓冲上限（**1536**） | Chrome 的 WS 握手头约 600–900 字节，含完整 User-Agent；**若被截断，dispatch 会失败** |

`/ws` 的 token 校验（`dispatch()` 内）：

```cpp
const bool isWs = strcmp(target, "/ws") == 0;
if (isWs) {
  if (settings::hasWebPassword()) {
    const char *tok = query ? strstr(query, "token=") : nullptr;
    const String expected = settings::webToken();
    if (!tok || expected.length() == 0 || expected != String(tok + 6)) {
      errorResponse(401, "Unauthorized", "websocket token required");
      return;
    }
  }
}
```

### 4.2 页面：`firmware/MiRemoteBridge/web_page.h`（单文件 HTML+CSS+JS，源）

JS 里与本次问题相关的函数（用 `Ctrl+F` 定位）：

- `connectWS()` — `fetch('/api/token')` → `openSocket(token)`
- `openSocket(token)` — `new WebSocket('ws://' + location.host + '/ws?token=' + encodeURIComponent(token))`，
  绑 `onopen/onclose/onerror/onmessage`
- `wsClosed(why)` — `online(false, why)` + 2.5 秒后重连
- `wsMessage(j)` — 处理 `type: key / status / bindings ...`
- `online(v, why)` — 控制横幅；`why` 写入 `#offWhy`

> 页面构建链：`web_page.h`（源）→ `python tests/tools/gen_web_page.py` →
> 拆成 HTML/CSS/JS 三份并 gzip → `firmware/MiRemoteBridge/web_page_gz.h`。
> **改完页面必须跑 gen**，否则设备发的是旧资源。gen 还会把 `__BUILDTIME__`
> 替换成本次构建时间，显示在页面顶部栏。

## 5. 排查工具（**务必先读这一节的坑**）

```bash
# 一键：重新生成资源 + 编译 + 烧录（失败会明确报错，比手工拼命令可靠）
python build/deploy.py

# 真机验证（设密码 → 取页面 → WS 握手 → WS 开着时 HTTP）
python build/verify_page.py
```

### ⚠️ 这个环境的坑（会误导判断）

1. **打开串口会复位设备**：pyserial 打开 COM3 的瞬间拉 RTS/DTR 脉冲（ESP32 的 EN 由 RTS 控制）。
   所以**任何串口命令（`pass clear` / `wifi status`）都会重启板子**。
   → 串口操作后**必须等 25–28 秒**（Wi-Fi 8 秒启动 + 关联）再做 HTTP 探测，
   否则会看到"设备完全不可达"的假象（**我今晚在这上面浪费了大量时间**）。
2. **shell 的 coreutils 大半不可用**（`head`/`sleep`/`dirname` 都 not found）。
   → 不要写 `cmd | head`、`&& sleep 5` 之类的命令链：管道会失败导致 `&&` 短路，
   而后续的检查又读**旧日志**，于是"编译烧录成功"是假的（**踩过两次**）。
   用 `build/deploy.py` 或 Python 脚本，不要拼 shell。
3. **Python 脚本里改 C/JS 源码时**：整块 `old_string` 匹配常因缩进/转义细微差异失败，
   而**脚本中断后文件不会落盘**、编译却仍然"成功"（因为文件没变）。
   → 用**关键词锚点 + 计数断言**（`assert s.count(anchor) == 1`），
   并**写盘后立即读回验证**（`assert '新文本' in open(f).read()`）。
4. **串口日志抓取**：`python build/serial_capture.py`（后台常驻，写
   `build/serial_live.log`）。它占用 COM3，**抓取运行期间无法烧录**（先停抓取）。
5. **登录/密码**：设备无密码时访问会显示"设置访问密码"页（填密码即登录，无需用户名）。
   串口 `pass clear` 清除；`pass <新密码>` 设置。**测试完请 `pass clear`**，别把测试密码留在用户设备上。

## 6. 我的最佳猜测（按优先级，均未验证）

### 猜测 A（最可能）：Chrome 的握手请求头触发了服务端的解析缺陷

`dispatch()` 用固定 1536 字节缓冲收请求头，并用 `sscanf("%7s %383s %11s")` 解析请求行。
Chrome 的 WS 握手会多带这些头，且顺序与 curl 不同：

```
Origin: http://192.168.1.100
Cookie: mrb_sess=...
User-Agent: Mozilla/5.0 (...) ...（200+ 字节）
Sec-WebSocket-Extensions: permessage-deflate; client_max_window_bits
Pragma / Cache-Control / Accept-Encoding / Accept-Language
```

**怀疑点**：
- `Sec-WebSocket-Key` 的解析（在 `dispatch()` 内用 `strncasecmp(line, "Sec-WebSocket-Key:", 18)` +
  `sscanf(line + 18, "%63s", key)`）在头部顺序变化时是否仍能取到？
- 头部缓冲接近上限时是否被截断（`kHeaderLimit`）？
- **注意**：`/ws` 路由的 key 提取是**第二次遍历 `s_http.io`**（`dispatch()` 开头已经遍历过一次
  并做过 `*end = 0` 再恢复的操作）——两次遍历之间的**状态是否互相污染**值得检查。

**验证思路**：在 `wsHandshake()` 入口打日志（key 值、`s_http.used` 字节数），
用 Chrome 连一次看日志；同时用 curl 连一次对比。

### 猜测 B：`Sec-WebSocket-Extensions: permessage-deflate`

Chrome 默认请求压缩扩展。规范上服务端**不响应**该扩展即为合法（不启用压缩），
但如果服务端的 101 响应里少了必需头、或多了非法头，Chrome 会**直接关闭连接（code 1006）**。
**用 Chrome 的 DevTools → Network → WS 面板看那次握手的响应头**，就能立刻区分
（101 之后是否立刻关闭、是否有异常头）。

### 猜测 C：`onclose` 的 code 没有传到横幅

新横幅应显示具体原因（`连接断开 (code 1006)`），但截图显示的是 HTML 里的默认文字
`（连接断开，自动重连中）`——说明 `online(false, why)` 的 `why` 为空。
`openSocket()` 的 `catch(e){wsClosed();return}` 是**无参调用**，若 `new WebSocket()` 抛异常
（URL 非法等）就会走到这里。**先确认 Chrome 控制台有无异常**。

## 7. 建议的第一步（最快见效）

1. 用 Chrome 打开 `http://192.168.1.100/`（无密码则先设一个），**F12 → Console + Network → WS**
   - 看 Console 有没有 JS 异常
   - 看 WS 面板里那次握手：**请求头全文**、**响应头全文**、关闭 code/reason
   - 这一步能直接把范围从"猜测 A/B/C"缩到一个
2. 同时开 `python build/serial_capture.py` 抓串口日志（`build/serial_live.log`）
   —— 看设备侧在 Chrome 握手时打了什么（`websocket open` / `no/bad session` / 无任何输出）
   - **如果设备侧毫无日志** → 请求根本没被受理（猜测 A 方向：解析失败）
   - **如果设备侧有 `websocket open`** → 握手成功、之后被客户端关闭（猜测 B 方向）
3. 改代码后用 `python build/deploy.py` 部署（它会 gen + 编译 + 烧录并校验），
   再用 `python build/verify_page.py` 回归。

## 8. 约束（用户明确要求过）

- **不引入 WebSocket 库**（曾试过：Arduino 自带 `WebServer` 不支持 WS；`ESPAsyncWebServer`
  带独立异步任务，与 BLE 共存风险未知；用户倾向把现有手写实现修好）
- **页面保持实时推送**（用户明确反对轮询："不要页面的 300 毫秒轮询，我要实时的"）
- **不增加常驻内存**（当前静态 RAM 44932 B / 13%，`Global variables` 一行是硬指标）
- **不破坏现有功能**（BLE 双链路转发、按键映射、配对）—— 任何改动后跑一次
  `build/verify_page.py` 的第 7 项（WS 开着时 HTTP 可用）与串口 `status`（BLE 双链路）

---

## 附：当前设备状态

- 固件：最新（含 WS 独立 fd、HMAC 登录、token 401、setup 强制跳转、backlog=4）
- 密码：**已清**（首次访问显示设置页）
- 串口日志：`build/serial_live.log`（历史若干次会话）
- Git：工作树干净，最近提交 `e778d58` / 之后的 `s_wsFd` 修复（见 `git log --oneline -6`）
