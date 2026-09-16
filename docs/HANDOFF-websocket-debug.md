# 结案记录：配置页 WebSocket 建不起来（已修复，真机验证通过）

> 这份文档起初是一份"交接给下一位"的排查任务书，带着三个**未验证的猜测**。
> 那些猜测**全部是错的**。现在文档保留为结案记录：真实根因是什么、当初猜错在哪、
> 以及后续必须守住的不变量。**不要**再照着旧的猜测方向排查。

---

## 1. 结论（一句话）

页面连不上不是 WebSocket 协议、不是 Chrome 的请求头、也不是缓存 —— 是服务端在
**HTTP 连接槽**和**帧缓冲大小**上各有一处契约违反，四处 bug 叠加。

真机验收：`python tests/tools/browser_check.py` → **8/8 全过**（真 Chrome，读 live DOM）。

## 2. 真实的五个根因

| # | 根因 | 症状 | 修法 |
| --- | --- | --- | --- |
| 1 | **HTTP 连接槽被空闲连接长期占用**。浏览器会并行开 4–6 条连接（页面/CSS/JS/升级），每条答完后仍以 keep-alive 占着**唯一的** exchange 槽，占满 `kProgressTimeoutMs`(4 s) 才释放。WS 升级请求永远排在队尾，**根本轮不到**。 | Chrome 的握手得不到任何响应；`curl` 单连接却能成功 —— 这正是"Chrome 失败 / curl 成功"的真实原因 | 新增 `kKeepAliveIdleMs = 300`：答完的连接标记 `idle`，只用 300 ms 就释放槽位；`listen` backlog 4→8 |
| 2 | **WS 升级分支不可达**。升级响应发完后代码重置了 `s_http.sending`，而真正执行升级的 `!left` 分支依赖它仍为 true。 | socket 发了 101 后**就干等**，直到空闲超时被关；页面看到的正是"连上→掉线→重连"死循环 | 升级响应发完且 `s_pendingUpgrade` 时**不重置**，让下一轮 `!left` 分支接手 |
| 3 | **`closeExchange()` 连带关掉 WS**。HTTP 槽（与 WS 是两个 fd）一拆，把活的页面一起带走。 | 页面无限重连的第二个来源 | `closeExchange()` 不再碰 WS；`stopHttp()` 单独负责 `wsClose()` |
| 4 | **帧缓冲溢出被静默丢弃**（**最致命、也是最后才找到的**）。绑定快照拼进 `char buf[1024]`，交给 **768 字节**的帧缓冲；`wsQueueRaw` 放不下就 `return false`，**没有一行日志**。 | 页面卡在"正在读取设备状态"，13 张卡片全是"等待读取"。WS 其实是通的（CDP 抓到 101 和 status 帧），所以前两轮"修好了"都是误判 | ① `wsQueueRaw` 拒绝时打 `BR_LOGW`（**静默**才是病根）；② 绑定数据改走 HTTP（`Content-Length` 天然分帧，多大都行），socket 只推小帧 |
| 5 | **还没发请求的新连接独占槽位 4 秒**（2026-09-12 补查，**手机"登录不进去"的真因**）。`accept` 之后的超时用的是 `kProgressTimeoutMs`(4 s)；300 ms 的短窗口只对"已经答过一次"的连接生效。而浏览器——手机 Safari 尤其——会做**预测性连接**：TCP 连上却一个字节都不发。 | 实测：一条静默连接让真实请求等 **4.8 s**，两条 **18.9 s**，三条 **29.0 s**（单独请求仅 0.01–0.08 s）。手机上表现为"密码输进去就是进不去" | 新增 `kAcceptGraceMs = 250`：**一个字节都还没发的连接**只用 250 ms 就放槽；同时修掉"复用的 keep-alive socket 收到新请求后仍留在 idle 状态"这个相关缺陷。修复后 3～4 条静默连接下真实请求 ~1 s |

## 3. 当初猜错在哪（供以后避坑）

| 旧猜测 | 实际 |
| --- | --- |
| A. `dispatch()` 解析不了 Chrome 更长的握手头（`kHeaderLimit=1536`、`sscanf` 格式） | ❌ 头部缓冲**够用**，解析正常。真正的问题是请求**排在别的连接后面，从未被读取** |
| B. `Sec-WebSocket-Extensions: permessage-deflate` 导致 Chrome 主动断开 | ❌ 服务端不响应该扩展即合法，Chrome 接受。101 是**成功**的 |
| C. `onclose` 的 code 没传进横幅 | ❌ 横幅文案无关键。`why` 为空是因为压根没进过 `onclose` 的正确分支 |

**教训**：`curl` 成功而 Chrome 失败时，第一反应是"请求内容差异"，但**并行连接数**是更
常见的差异来源 —— curl 只开一条，浏览器开 4–6 条。先数连接，再比头。

## 4. 现在必须守住的不变量

1. **socket 只推小帧。** 帧上限是 `s_wsOutBuf`（768 B）。任何"把整张绑定表发过去"的
   想法都必须走 HTTP。`wsCommand` 里 `get` 现在直接回 `bindings are served over HTTP`，
   **不要**把它改回返回绑定表。
2. **`wsQueueRaw` 拒绝时必须打日志。** 静默丢弃让这个问题藏了一整天。缓冲区大小是
   编译期常量，别绕开这个函数手写帧。
3. **HTTP 槽和 WS 是两个 fd，生命周期不得耦合。** `closeExchange()` 不该关 WS。
4. **空闲连接的让槽时间要短**（`kKeepAliveIdleMs`）。改成 4 s 就会退回原 bug。
5. **一个描述符只有一条实现。** 绑定 JSON 曾在 `handleBindings()` 和
   `buildBindingsJson()` 里各写一份，后者已删。改接口别只改一处。

## 5. 验证方式

```bash
# 前提：板子 Wi-Fi 可达（串口 wifi on），COM3 空闲，本机装有 Chrome
python tests/tools/check_all.py        # 一次跑完下面三项，给一个总判定
```

三层检查各管一件事，互不替代（`check_all.py` 依次跑完）：

| 脚本 | 层次 | 覆盖 |
| --- | --- | --- |
| `preconnect_probe.py` | socket | 槽位是否及时释放（静默连接、keep-alive 都不得堵住真实请求） |
| `browser_check.py` | 页面 | 真 Chrome 读 live DOM：渲染、绑定、WS、按键实时推送 |
| `browser_check.py` | 浏览器 | 真 Chrome 走完页面 JS 与 WS（窗口期内） |

脚本用 CDP 驱动**真实 Chrome**，从 `Runtime.evaluate` 读 live DOM（不靠截图推断），
并抓 `Network` 域的握手与帧。IP 自动从 `build/.boardip` 取（DHCP 会变），
可用 `MRB_IP` 覆盖。

八项断言：13 张卡片 / 无失控横幅 / 表头非占位 / 卡片有真实动作 / 绑定计数 /
`S.on` 为真 / **按键推送（页面计数真的增长）** / 推送后仍连着。

> ⚠️ `S.keyPress` **只在收到 `key` 帧后才被赋值**，5 s 的 `status` 心跳走 `stat()`
> 不碰它。所以首帧之前读到的 `keyPress` 是 `undefined`（JSON 序列化后读回 `None`），
> 这**不代表页面坏了** —— 它是合法基线，按 0 处理。

## 6. 环境坑（仍然有效）

1. **打开串口会复位设备**（RTS 控制 EN）。任何串口命令后必须等 **25–28 s**
   再做 HTTP 探测，否则会看到"设备完全不可达"的假象。
2. **shell 的 coreutils 大半不可用**（`head`/`tail`/`ls`/`dirname` 都 not found）。
   不要拼 `cmd | head`、`&& sleep` 之类的命令链 —— 管道失败会让 `&&` 短路，
   后续检查读到旧日志，于是"编译烧录成功"是假的。用 Python 脚本。
3. **Python 里改 C/JS 源码**：整块 `old_string` 匹配常因缩进/转义失败，且脚本中断后
   **文件不会落盘**。用关键词锚点 + 计数断言，写盘后立即读回验证。
4. **`esptool --after no-reset` 绝对不能用**：会停在 ROM 下载模式，固件不跑，像死机。
5. **测试脚本不再改动板子的认证状态**（2026-09-16 起没有密码可清）。
   旧版脚本结尾会 `pass clear`，把设备留在"首次设置"状态，这个坑已经不存在了。
6. **页面顶栏的时间戳曾经必然是错的**。`gen_web_page.py` 把"生成时的时间"写进 gzip
   载荷，所以页面显示的是**生成 web_page_gz.h 那一刻**，不是编译/烧录时刻（出现过
   烧录 12:56、页面显示 10:21）。现在时间戳彻底从页面里拿掉：顶栏读
   `/api/status` 的 `fwVersion` + `buildTime`（固件里的 `__DATE__ __TIME__`）。
   副作用是 `web_page_gz.h` 变成**确定性生成物**，跑 `test.ps1` 不再弄脏工作区，
   `--check` 也不必再"抹掉时间戳再比对"。
7. **跑 CDP 检查时不要固定调试端口、不要复用 profile 目录。** 上一次运行残留的 Chrome 会
   以独占方式占着固定端口，新实例于是 **IPv4 bind 失败（`WSAEACCES` 0x271D）并静默退化成
   只监听 IPv6**；同时复用 profile 目录会让新实例"移交"给卡死的旧实例然后自己退出。
   症状是 `Chrome never exposed a page target`。现在 `cdp.py` 每次取**动态空闲端口 +
   独立 profile 目录**，并且 `local_json` **IPv4/IPv6 都试**。
8. **给"页面渲染完成"留时间再断言。** 落到 `/` 不等于加载完：表头还会停在占位文案、`S.on`
   还是 false，直到页面取完 `/api/bindings`、`/api/status` 并开好 WebSocket
   （实测约 1.1 s）。在稳定之前读 DOM 会把**正常代码判成坏**——和读 `S.keyPress` 是同一个坑。
   用 `browser_check.py` 里的 `settle()` 那种轮询。
9. **PowerShell 工具不捕获 stdout**，但可以用它做 CIM 查询：把结果写文件再读。
   **不要**从 Bash 里调 powershell（会被安全策略拦下）。查/杀残留进程时，只按命令行里的
   `--remote-debugging-port` 过滤，**绝不**笼统地按进程名杀 —— 用户自己的浏览器在同一台机器上。

## 7. 相关提交

- `0f9e99c` WS 独立 fd（HTTP 不再被页面阻塞）—— 真机验证
- `d05a8db` 前四个根因的完整修复 + 页面改为"绑定走 HTTP / socket 只推小帧" + 本文件改写为结案记录
- 随后一次提交：根因 5（静默连接的 250 ms 宽限）+ 清理死代码（Basic 认证相关）+ 认证现状记录
  `docs/AUTH.md` + `tests/tools/` 下的 `cdp.py` / `preconnect_probe.py` / `browser_check.py` /
  `check_all.py` —— **两项硬件检查全部真机通过**
