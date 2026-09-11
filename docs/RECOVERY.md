# 故障恢复

本固件需要管理**两组**配对记录：

| 组 | 存在哪里 | 谁发起配对 |
| --- | --- | --- |
| RC003 Bond | C3 的 NimBLE Bond 存储（NVS）+ `settings` 里的 `rc_addr` / `rc_type` / `rc_name` | C3 作为 Central |
| Windows Bond | 同上（NimBLE 存储） | Windows 作为 Central，C3 作为 Peripheral |
| Windows 自己那一侧 | Windows 系统里 | —— |

**关键点**：`forget` 只清掉 C3 这一侧。对端（遥控器 / Windows）仍可能记着旧密钥，
重新连接时加密会失败。所以下表中"对端也要清"这一列必须一起做。

---

## 1. 三条清除命令

在串口控制台执行（`.\scripts\monitor.ps1 -Port COM3`）：

### `forget win` — 清除 Windows 侧配对

```
forget win
```

做的事：删除主机侧的 Bond 记录 → 重新广播。之后 Windows 需要重新配对。

**有没有连接都能用**：
- 有主机连着 → 删掉那一条；
- 没主机连着 → 删掉**除 RC003 以外**的所有 Bond（外设角色只会和主机器配对，
  所以"不是遥控器的"就是主机）。这一点很重要：需要清主机配对的时候，
  主机往往正因为密钥不一致而连不上，如果要求"必须先连上"就永远走不出来。

**⚠️ 必须两边都清，单边清比不清更糟（实测）**

只在 C3 这边清、Windows 那边还留着配对记录，会让两侧密钥不一致，Windows 会陷入
**每 6～7 秒断一次、重连、再断**的循环，期间按下的键会掉进断开的窗口里丢失。
实测数据：单边清之后 5 分钟内断开 4 次；两侧一致时 15 分钟 0 次断开。

所以正确顺序是 **先 `forget win`，再到 Windows 里删除设备**：

设置 → 蓝牙和其他设备 → `Mi Remote Bridge` → `...` → **删除设备**

如果 Windows 里删完又自己冒出来，说明它记住了这个设备，再删一次即可；
两侧最终都处于"未配对"时，重新配对会同时建立新的 Bond。

### `forget rc` — 清除 RC003 侧配对

```
forget rc
```

做的事：删除已保存的 RC003 Bond → 清掉 NVS 里的地址/名字 → 断开 → 重新扫描。
只有在**未绑定**状态下才会重新接受任意小米遥控器。

**对端也要清吗？** 是。让遥控器忘记这台 C3：

- 把它原来的"主机"（小米电视/盒子）里的配对删掉；或
- 按 `PAIRING.md` §1.1 让遥控器重新进入配对广播。

### `factory` — 恢复出厂配对状态

```
factory
```

做的事：清空 NimBLE Bond 存储全部记录 → 清空本项目在 NVS 里的所有键 → 重启。
两侧都需要重新配对。

**对端也要清吗？** 是（两侧都要）。

### 不用串口：BOOT 键长按 5 秒恢复出厂

串口不在手边时，用开发板上的 **BOOT 键**（GPIO9，合宙板上丝印 `BOOT`）：

```
按住 BOOT ≥ 5 秒（按住期间串口每秒打印倒计时）
→ 自动清除全部配对与设置并重启
```

短按（5 秒内松开）**不做任何操作**，只留一条日志（用于确认按键检测正常）；
唯一的功能是长按 5 秒恢复出厂。串口 `factory` 命令与长按走同一条代码路径，
效果完全相同。

**恢复出厂之后**：桥接器两侧的配对全部清空——主机（Windows/iPhone）蓝牙里
删除 `Mi Remote Bridge` 后重新添加；遥控器按 `PAIRING.md` §1.1 进入配对广播，
串口 `connect <mac>` 或等自动配对。

---

## 2. 恢复流程矩阵

| 场景 | 操作 |
| --- | --- |
| **RC003 Bond 失效**（换过电池、换过手机配对、遥控器被别的设备占用） | ① 原设备上删除配对 ② `forget rc` ③ 让遥控器进入配对广播 ④ 等自动识别或 `connect <mac> random` |
| **Windows 侧描述符缓存错乱**（显示未知设备、按键错位） | ① `forget win` ② Windows 删除设备 ③ 重新配对 ④ 确认日志出现 `report 1 (keyboard) notifications ENABLED` 和 `report 2 (consumer) notifications ENABLED` |
| **配对后每 6～7 秒断开重连一次**（实测过） | 两侧配对状态不一致。**必须两边都清**：① `forget win` ② Windows 删除设备 ③ 重新配对。只清 C3 一侧会一直循环 |
| **Windows 重启后连不上** | 通常 Windows 会自动重连。若没连上：Windows 里删掉设备 + `forget win`，然后关/开 Windows 蓝牙重新配对。**不需要**清 RC003 的 Bond |
| **C3 断电重启** | 不需要任何操作。固件优先用已保存地址 + Bond 直连 RC003，失败才回退扫描 |
| **RC003 休眠后唤醒** | 不需要任何操作。固件在后台持续重连（扫描脉冲 20 秒 + 停顿 3 秒循环），唤醒后自动重连并重新订阅 |
| **两侧同时断开** | 不需要任何操作。固件会清空所有按键状态、重新广播，并后台重连 RC003 |
| **出现按键卡住** | 任意断线都会自动 `releaseAll()`。若仍卡住：`key <code> release` 或 `reconnect`；再不行 `reboot`。同时请把 `raw on` 日志留档 |
| **固件行为异常但串口还在** | `log 4` 打开调试日志看细节；`reboot` 重启；`factory` 彻底重置 |
| **完全无法通信（串口无输出）** | 按住 BOOT → 点 RESET → 松开 BOOT 进下载模式，重新 `.\scripts\flash.ps1 -Port <口>` |

---

## 3. 设计上为什么不会卡键

「Windows 上按键卡住」是这类桥接器最常见的事故。固件在三个层面防住它：

1. **幂等**：`pressAction()` 对已经按下的键是空操作；`releaseAction()` 对已松开的键也是空操作。
   遥控器重复发同一个按下报文不会变成连发。
2. **按键状态只有一个来源**：HID 报告每次都从"当前按下的集合"重新构建，不存在状态漂移。
3. **断线必清零**：以下任一事件都会立即 `releaseAll()`，把键盘报告与 Consumer 报告同时清零：
   - RC003 链路断开（`BR_EV_RC_LINK_DOWN`）
   - RC003 配对/服务发现失败（`BR_EV_RC_BOND_FAIL`）
   - 主机断开（`BR_EV_WIN_LINK_DOWN`）
   - 主机刚连上、RC003 刚就绪（`BR_EV_WIN_LINK_UP` / `BR_EV_RC_READY`）
   - 发现一个新键按下而旧键没松开（先释放旧键）

   代码位置：`bridge.cpp` 的 `releaseAllKeys()` / `hid_server.cpp` 的 `releaseAll()`。

4. **上层还有一道兜底**：`parsing` 层只接受已知键码，未知码直接丢弃，不会把杂散字节当成按键发出去。

---

## 4. 判断"问题在哪一侧"

```
status
```

| 输出 | 含义 |
| --- | --- |
| `rc003 state : READY` + `host connected : yes` | 两侧都正常。问题在按键转换 → `raw on` 看原始报文 |
| `rc003 state : SCANNING` / `BACKOFF` | 遥控器没连上（休眠/被占用/Bond 失效） |
| `rc003 state : DIRECT` | 正在尝试用保存的地址直连 |
| `rc003 state : DISCOVERING` | 连上了但服务发现/订阅失败 → 看 `GATT` 日志 |
| `host connected : no` | Windows 没连上 → 对照 `PAIRING.md` §3 下游部分 |
| `bonds : 0` | 两侧都没配对记录 → 需要完整重新配对 |
| `notifications : 0` 且 `last report age : -1` | 从没收到过按键报文 → 遥控器侧问题 |
| `unknown` 计数增长 | 收到了不认识的键码 → 见 `KEYMAP.md` §4 |
| `queue pending` 持续不降 / `dropped > 0` | 主循环被阻塞 → 把日志附上报上来 |
