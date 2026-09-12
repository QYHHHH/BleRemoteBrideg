# 结案记录：配置页 WebSocket 建不起来（已修复，真机验证通过）

> 这份文档起初是一份"交接给下一位"的排查任务书，带着三个**未验证的猜测**。
> 那些猜测**全部是错的**。现在文档保留为结案记录：真实根因是什么、当初猜错在哪、
> 以及后续必须守住的不变量。**不要**再照着旧的猜测方向排查。

---

## 1. 结论（一句话）

页面连不上不是 WebSocket 协议、不是 Chrome 的请求头、也不是缓存 —— 是服务端在
**HTTP 连接槽**和**帧缓冲大小**上各有一处契约违反，四处 bug 叠加。

真机验收：`python tests/tools/browser_check.py` → **8/8 全过**（真 Chrome，读 live DOM）。

## 2. 真实的四个根因

| # | 根因 | 症状 | 修法 |
| --- | --- | --- | --- |
| 1 | **HTTP 连接槽被空闲连接长期占用**。浏览器会并行开 4–6 条连接（页面/CSS/JS/升级），每条答完后仍以 keep-alive 占着**唯一的** exchange 槽，占满 `kProgressTimeoutMs`(4 s) 才释放。WS 升级请求永远排在队尾，**根本轮不到**。 | Chrome 的握手得不到任何响应；`curl` 单连接却能成功 —— 这正是"Chrome 失败 / curl 成功"的真实原因 | 新增 `kKeepAliveIdleMs = 300`：答完的连接标记 `idle`，只用 300 ms 就释放槽位；`listen` backlog 4→8 |
| 2 | **WS 升级分支不可达**。升级响应发完后代码重置了 `s_http.sending`，而真正执行升级的 `!left` 分支依赖它仍为 true。 | socket 发了 101 后**就干等**，直到空闲超时被关；页面看到的正是"连上→掉线→重连"死循环 | 升级响应发完且 `s_pendingUpgrade` 时**不重置**，让下一轮 `!left` 分支接手 |
| 3 | **`closeExchange()` 连带关掉 WS**。HTTP 槽（与 WS 是两个 fd）一拆，把活的页面一起带走。 | 页面无限重连的第二个来源 | `closeExchange()` 不再碰 WS；`stopHttp()` 单独负责 `wsClose()` |
| 4 | **帧缓冲溢出被静默丢弃**（**最致命、也是最后才找到的**）。绑定快照拼进 `char buf[1024]`，交给 **768 字节**的帧缓冲；`wsQueueRaw` 放不下就 `return false`，**没有一行日志**。 | 页面卡在"正在读取设备状态"，13 张卡片全是"等待读取"。WS 其实是通的（CDP 抓到 101 和 status 帧），所以前两轮"修好了"都是误判 | ① `wsQueueRaw` 拒绝时打 `BR_LOGW`（**静默**才是病根）；② 绑定数据改走 HTTP（`Content-Length` 天然分帧，多大都行），socket 只推小帧 |

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
python tests/tools/browser_check.py
```

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
5. **测试完 `pass clear`**，别把测试密码留在用户设备上。
6. **`tests/tools/gen_web_page.py --check`** 已修好（旧版把每次都会变化的构建时间戳
   压进 gzip 里做字节比对，**过一分钟就必然报 stale**；现在解压后比对并忽略时间戳）。

## 7. 相关提交

- `0f9e99c` WS 独立 fd（HTTP 不再被页面阻塞）—— 真机验证
- 之后一次提交：上述四个根因的完整修复 + 页面改为"绑定走 HTTP / socket 只推小帧" + 本文件
- `tests/tools/browser_check.py` 加入版本控制（此前只存在于被 gitignore 的 `build/`）
