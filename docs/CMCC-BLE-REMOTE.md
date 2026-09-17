# 中国移动蓝牙语音遥控器接入排查

> **2026-09-16 后续进展（先看这条）**：这台遥控器**已经接入成功并端到端可用**——
> 按下去电脑确实响应。绕过办法是订阅时**只认第一条 HID report**
> （`rc003_client.cpp` 的 `cmccSingleReportProbe`），不再触发下面记录的 MIC 断链。
> 代价是**遥控器上有部分物理键按下无响应**（怀疑是分布在其他 report 上的键，
> 但没逐条验证）。实测到的键码清单与限制写在 README 的兼容性一节。
> **下面的结论是当时的推断，其中"两端密钥不是同一个"并未最终证实。**

针对"配对 + 加密已完成、部分通知已订阅，随后 **MIC 校验失败**断开"这一现象的定位与排查记录。

> **结论**：**两端用来加密的密钥不是同一个**。遥控器是标准 HOGP 设备（手机实测可用），
> 问题出在 ESP32 这一侧的密钥来源。有两个候选，用一行日志就能判定，见第 2.2 节。
>
> 附带结论：既然是标准 HOGP，`discoverAndSubscribe()` 的订阅逻辑可以直接复用，
> **不需要**新写一个 client。
>
> 另外：**GitHub 上没有现成的开源实现可抄**（见第 1 节）。

---

## 1. GitHub 搜索结论

用 GitHub API 跑了中英文关键词（`蓝牙遥控器 ESP32`、`魔百盒`、`机顶盒 遥控器 蓝牙`、
`咪咕 遥控器`、`BLE HID remote ESP32 set-top box`、`HOGP remote control esp32 central`、
`bluetooth remote control bridge esp32` 等），结果：

| 仓库 | 星数 | 说明 |
| --- | --- | --- |
| `cuicui-V5/RemoteMapper-ESP32` | 61 | 小米蓝牙遥控器 2 Pro（RC003）的 ESP32 桥接 |
| `daishuge/mi-remote-gateway` | 3 | 同样是 RC003，LILYGO T-Dongle-S3 |
| `zx804262156/oszso-vibe-coding-gateway` | 1 | 还是 RC003，ESP32-S3 |
| `finger563/esp-usb-ble-hid` | 20 | 通用 BLE Central → USB HID，不含机顶盒遥控器逻辑 |

**没有**任何针对中国移动 / 魔百盒 / 咪咕蓝牙语音遥控器的仓库。可复用的只有
"BLE Central + HID 订阅 + 断线重连"的通用骨架——而本项目自己已经有了。

---

## 2. 现象定位

**MIC 校验失败不是 GATT 问题，也不是按键解析问题，是链路层加密密钥来源不一致。**

MIC（Message Integrity Check）是加密链路上的报文完整性校验码。链路加密完成后，
收到的**第一个加密包**如果 MIC 对不上，协议栈会直接拆链路。所以：

- 能走到"部分通知订阅成功" → 服务发现、CCCD 写都是正常的；
- 之后挂在 MIC → 加密**表面成功**，但两端用来加密的密钥不是同一个。

这跟"连不上 / 扫不到 / 服务发现为空"是完全不同的故障，不要往连接参数、
广播、RSSI 那些方向查。

### 2.1 MIC 失败意味着什么

MIC 失败 = **双方都认为加密流程走完了，但各自手里的密钥不是同一个**。
能造成这种状态的有两类原因，而且**这两类都不会表现为"配对阶段直接失败"**：

| 类别 | 机制 |
| --- | --- |
| **陈旧 bond** | 一方拿存储里的旧 LTK 发起加密，另一方用当前的 LTK 校验 |
| **协商不一致** | SC / Legacy 混用，或 Just Works 与 Passkey 协商结果不同，两边推导出不同密钥 |

> **修正**：早前版本的本文档写过"参数不匹配会在配对阶段就失败，所以加密成功后才挂
> 可以排除参数问题"——**这个推断是错的**。上面第二类恰恰就是"配对看起来成功、
> 密钥却不同"的典型成因。参数问题不能靠这个现象排除。

实证：**手机能连上这只遥控器，各个按键都能用**，说明遥控器是标准 HOGP 设备，
SM 实现正常。但**手机能连不等于 ESP32 能连**——Android 的 BLE 栈对 SC 回退、
异常外设的容错比 NimBLE 宽松得多。

### 2.2 一行日志判定走哪条路

`rc003_client.cpp` 的 `discoverAndSubscribe()` 在每次连接后会打一行
（日志 tag `kTagSec`）：

```
security begin encrypted=0 bonded=? stored=?
```

| 观测 | 含义 | 下一步 |
| --- | --- | --- |
| `stored=1` 或 `bonded=1` | ESP32 手里**已经有这条 bond**。连上后会直接用存的 LTK 发起加密（甚至自动加密），**不会走新配对**——所以遥控器进不进配对模式都没用 | 走第 3.1 节：把 ESP32 的 NVS 彻底清掉 |
| `stored=0 bonded=0` | 确实在走新配对，密钥却对不上 → 问题在协商本身 | 走第 3.2 节：试 `sc=false` |

`stored` 来自 `rc003_client.cpp` 的 `storedPeerKey()`，它读的是
NimBLE store 里 `peer_sec` 的 `ltk_present`。**这一行是当前最省事的判据。**

---

## 3. 两个候选根因

### 3.1 ESP32 侧残留陈旧 bond（先查这个）

ESP32 的 NimBLE bond store 是**持久化在 NVS** 的。如果它已经存了一条指向这台
遥控器的 bond（之前几轮失败尝试留下的），那么连接建立后会：

1. 直接用存储里的 LTK 发起加密，**不会走新的配对流程**；
2. 遥控器按自己当前的 LTK 校验；
3. 密钥不同 → 第一个加密包 MIC 失败 → 拆链路。

**关键**：这种情况下，**遥控器进不进配对模式都没有用**——因为 ESP32 根本没触发
新配对。这正好解释了"遥控器明明进了配对模式，还是 MIC 失败"。

判定方法见 2.2（看 `security begin ... stored=?`）。修复见第 4.1 节。

> **重烧固件不会清 NVS。** 之前反复试的那几轮，bond 很可能还躺在里面。

### 3.2 协商不一致：SC / IO 能力（其次）

如果 2.2 判定 `stored=0 bonded=0`，说明确实走了新配对、密钥却仍不同，
那问题就在协商本身：

- **SC 不兼容**：`ble_core.cpp` 的 `setAuthenticationMode()` 传的是 `sc=true`。
  若遥控器**自称**支持 LE Secure Connections 但实现有问题，两边会各自用不同的
  方式推导密钥，配对"成功"、MIC 失败。这是最典型的一种；
- **IO 能力 / 固定配对码**：`ble_core.cpp` 的 `setCapability()` 传的是
  `ESP_IO_CAP_NONE`（Just Works）。若遥控器实际期望 Passkey Entry（常见
  `000000` / `123456`），两边算出的 STK 不同，同样表现为配对成功 + MIC 失败；
- **密钥掩码要了 IRK**：`ble_core.cpp` 的 `setInitEncryptionKey()` /
  `setRespEncryptionKey()` 要的是 `ENC | ID`，ID 即 IRK。

**注意**：手机能连上**不能**排除这一条。Android 的 BLE 栈对 SC 回退和异常外设的
容错比 NimBLE 宽松，同一个遥控器在手机上能跑、在 NimBLE 上翻车是常见的。

试法见第 6 节矩阵，最省事的一刀是 `sc=false`。

### 3.3 已降级：遥控器侧 bond 槽被占

早前版本把这条列为主因，依据是"手机连过之后遥控器的主人变成了手机"。
但用户后续确认**用 ESP32 连的时候也把遥控器进了配对模式**，这条的权重就下来了：

- 遥控器若在配对模式下会清掉旧 bond，那它侧边就没有旧密钥；
- 但**部分遥控器支持多 bond**，进配对模式只加不清——所以这条不能完全排除，
  只是优先级降到 3.1 和 3.2 之后。

若 3.1 / 3.2 都排除了仍失败，再回头查：把手机的配对记录删掉、机顶盒里也删掉，
让遥控器只认 ESP32 一个主机。

---

## 4. 处理顺序（顺序很重要）

**必须先把遥控器上所有旧主机清干净，再让 ESP32 成为第一个配对方。**

1. **手机上忽略这个遥控器**（蓝牙设置 → 忘记此设备）；
2. **机顶盒蓝牙设置里删掉这个遥控器**；
3. **遥控器长按配对组合键 10 s** 恢复出厂 / 清配对，直到指示灯快闪。
   组合键因型号而异，常见是「返回 + 菜单」或「音量+ / 音量-」同按；
4. 让遥控器重新进入配对模式；
5. **立刻用 ESP32 连，中间别让手机插手**。

> **只让遥控器进配对模式是不够的。** 用户已经试过：ESP32 连接时遥控器就在配对模式，
> 仍然 MIC 失败。原因是如果 ESP32 手里已有陈旧 bond（见 3.1），它会直接用存下来的
> LTK 加密，**压根不会触发新配对**——遥控器那边进不进配对模式都无所谓。
> 所以第 4.1 步的"清 ESP32"不是可选项。

### 4.1 ESP32 侧也要清干净

> **重烧固件不会清 NVS。** 之前反复试的那几轮，ESP32 侧可能也留了残留记录。

要真干净，两个办法：

- 走 `factory` 命令（会连设置一起清）；
- 或者 `esptool erase_flash` 把整个 flash 擦了再烧。

**不要依赖 `forget rc`。** 它走的是 `ble_bonds::removePeer()` → `ble_gap_unpair()`。
NimBLE 头文件里
`ble_gap_unpair()` 的说明是 "The keys related to that peer device are removed
from storage"，理论上会同时清 peer_sec 和 our_sec；但本 SDK 只提供预编译的
`libbt.a`，没有源码，无法逐行核实它究竟清了几条记录，所以不把它当作可靠手段。

---

## 5. 验证

### 5.1 先看日志判定，再动手

按 2.2 的判据先看 `security begin ... stored=?`：

- **`stored=1` / `bonded=1`** → 走 4.1，把 ESP32 的 NVS 彻底清掉再连。
  **这是当前最可能命中的一条**，而且成本最低（不用动手机和机顶盒）。
- **`stored=0` / `bonded=0`** → 走第 6 节矩阵，先试 `sc=false`。

如果 `stored=1` 且清干净后就好了 → 实锤是 ESP32 侧陈旧 bond。

### 5.2 对照实验

用 nRF Connect 连上后，从手机侧解绑，再立刻用 ESP32 连，看还 MIC 不 MIC。

若 ESP32 侧确认干净（`stored=0`）而仍然失败，则问题在协商本身（3.2），
不是 bond 状态。

### 5.3 读准断开原因码

项目已有日志埋点，不用加代码：

- `ble_core.cpp` 的 GAP 事件回调会打 `disconnect handle=... reason=...` 和
  `security handle=... status=...`

对照 HCI 错误码：

| reason | 含义 | 指向 |
| --- | --- | --- |
| **0x3D (61)** | MIC Failure | 密钥来源不一致，即本文档场景 |
| 0x13 (19) | Remote User Terminated | 遥控器主动断开，多半是绑定流程不满意 |
| 0x16 (22) | Local Host Terminated | ESP32 自己断的 |
| 0x08 (8) | Connection Timeout | 连接参数 / 信号问题，另查 |

### 5.4 抓空口包（有条件时）

nRF52840 dongle + Wireshark + nRF Sniffer，看 Pairing Request / Response 里的
**AuthReq 字节**：

| bit | 含义 |
| --- | --- |
| 0 | Bonding |
| 1 | MITM |
| 2 | Secure Connections |
| 3 | Keypress notifications |
| 4 | CT2 |

再加 IO Capability 字段，就能**直接读出对端支持什么**。只在第 3.2 节的
协商不一致需要排查时才用得上。

---

## 6. 若清完 bond 仍然失败的备选矩阵

每次只改一个变量，重烧，看串口日志：

| 轮次 | bonding | mitm | sc | 密钥掩码 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 0（当前） | true | false | true | ENC+ID | RC003 实测可用 |
| 1 | true | false | **false** | ENC+ID | 退 Legacy |
| 2 | true | false | false | **ENC** | 再去掉 IRK |
| 3 | 不发起加密 | — | — | — | 跳过 SM 直接订阅 |
| 4 | true | **true** | false | ENC | 配 `setStaticPIN()` |

每轮之前**都要清遥控器 bond**（第 4 节），否则旧密钥会污染结果。

---

## 7. 代码改动建议

**好消息：既然是标准 HOGP，不需要新写 client。**

`rc003_client.cpp` 的 `discoverAndSubscribe()` 那套 HID 订阅逻辑可以直接复用，
只需要摘掉其中对 RC003 的假设：

- 其中"HID 服务 `0x1812` 优先、电池 `0x180F` 次之"的两趟订阅顺序，是按 RC003
  的服务布局调优的，新遥控器未必适用；
- 其中"必须先加密才能用 HID"的前提，对新遥控器需重新确认。

另外可考虑：

1. 把 SM 参数做成运行时可配（走现有 CLI），这样第 6 节的矩阵不用反复重烧；
2. 加一个"跳过加密"的开关，用于验证 3.3 的最后一问。

**当前状态**：以上代码改动均未实施，用户暂不需要。

---

## 8. 待补充

- [ ] **`security begin encrypted=? bonded=? stored=?` 那一行的实际值**（判定走 3.1 还是 3.2）
- [ ] 断开 reason 原始值（确认是不是 0x3D）
- [ ] 遥控器具体型号 / 丝印
- [ ] 清 bond 后重连的结果
- [ ] 若仍失败：nRF Connect 服务列表截图
