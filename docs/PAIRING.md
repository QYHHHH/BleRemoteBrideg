# 配对说明

分两侧：**上游**是 ESP32-C3 与小米遥控器 RC003 配对；**下游**是 Windows 与 ESP32-C3 配对。
上游只需要做一次（之后靠 Bond 自动重连），下游是标准蓝牙键盘配对流程。

全程用命令行 + 串口，不需要桌面操作。

---

## 0. 前置

```powershell
# 1. 编译
.\scripts\build.ps1

# 2. 确认端口（不给 -Port 时只检测、绝不烧录）
.\scripts\flash.ps1
# 输出类似：
#   Detected serial ports:
#   Address Protocol            Vid     Pid     Serial
#   ------- --------            ---     ---     ------
#   COM3    Serial Port (USB)   0x1A86  0x55D3  5458003768
#
# 3. 确认后用显式端口烧录（脚本会先只读探测芯片型号，不是 ESP32-C3 就中止）
.\scripts\flash.ps1 -Port COM3

# 4. 打开串口
.\scripts\monitor.ps1 -Port COM3
```

> **注意**：`-Port` 必须显式给出。脚本在只有一个串口时也只会提示命令，不会自己用。
> 如果你的板子只有原生 USB 口、没有 USB 转串口芯片，需要加 `-CdcOnBoot` 重新编译。

启动后应看到：

```
[   1234][BRIDGE ] MiRemoteBridge 0.3.0
[   1240][BRIDGE ] console ready at 115200 baud
[   1250][BRIDGE ] keymap: back=consumer_back power=kb_alt_f4 voice=kb_ralt_comma
[   1300][BLE    ] host stack: nimble, own address: xx:xx:xx:xx:xx:xx
[   1450][HID    ] HID peripheral up: name="Mi Remote Bridge" report-map 90 bytes, host stack nimble
[   1460][BRIDGE ] bridge running: dual role (central -> RC003, peripheral -> host)
[   1500][RC     ] no bound remote yet - will scan and pair with the first Xiaomi remote it sees
```

---

## 1. 上游：配对小米遥控器 RC003

### 1.1 让遥控器进入可发现状态

RC003 平时会休眠，休眠时**不广播**。任选一种：

- 按住遥控器上任意按键几秒（常见做法：同时按住「主页 + 菜单」或「确定 + 返回」）；
- 或者用遥控器原本配对过的设备（小米电视/盒子）里"删除设备"，让遥控器重新进入配对广播；
- 或者拔掉遥控器电池 3 秒后装回，然后按住某个键。

不同固件版本的进入方式不同，**以实际广播为准**：打开原始日志看有没有设备出现。

```
raw on
scan now
```

`raw on` 会打印每一个匹配到的广播。若迟迟没有目标，用 `scan` 查看扫描器缓存：

```
scan
```

```
3 device(s) seen by the scanner:
  c0:5d:39:xx:xx:xx  rand    -52 dBm  MI RC
  5c:f3:70:xx:xx:xx  pub     -74 dBm  (no name)
  ...
```

### 1.2 固件自动识别规则

| 状态 | 判定条件 |
| --- | --- |
| **未绑定**（首次） | 广播名包含 `MI RC` / `Xiaomi` / `Remote`，**或**广播里带 ATVV 服务 UUID `ab5e0001-...`。普通 HID 服务 `0x1812` **故意不匹配**，以免误抓邻居的键盘鼠标 |
| **已绑定** | 只接受身份地址等于已保存地址的设备；若遥控器轮换了可解析私有地址（RPA），则用已保存的名字兜底 |

匹配成功后固件会自动：停止扫描 → 连接 → 发起配对（Just Works，无需输入 PIN）→ 服务发现 → 订阅通知 → 持久化地址与名字。

日志形态：

```
[  20100][SCAN   ] target found: "MI RC" c0:5d:39:xx:xx:xx rssi=-52 type=1
[  20120][RC     ] connecting to c0:5d:39:xx:xx:xx (type 1)...
[  20450][RC     ] gatt link established
[  20460][SEC    ] link encrypted (bonded=1, authenticated=0)
[  20600][GATT   ] discovered 4 service(s)
[  20601][GATT   ] service 00001812-0000-1000-8000-00805f9b34fb
[  20610][GATT   ] subscribed to HID report 00002a4d-...
[  20611][GATT   ] protocol mode set to Report (0x01)
[  20612][GATT   ] subscribed to ATVV control (voice button only, no audio)
[  20640][GATT   ] ready: 2 subscription(s), notifications live
[  20641][BRIDGE ] RC003 ready: notifications subscribed
```

到这里按遥控器上的键，应看到：

```
[  25230][KEY    ] press 0x80 (VOL_UP) -> CONSUMER 0x00E9
[  25231][KEY    ] latency notify->hid 1180 us
[  25340][KEY    ] release 0x80 (VOL_UP) [key up]
```

如果没反应，见第 3 节 "上游不上报按键"。

### 1.3 手动连接（当自动识别不灵时）

从 `scan` 列表里拿到地址，显式连接：

```
connect c0:5d:39:xx:xx:xx random
```

地址类型：列表里显示 `rand` 用 `random`，`pub` 用 `public`（也可以省略，脚本会两种都试）。

---

## 2. 下游：在 Windows 中配对 BLE 键盘

1. 确认固件正在广播：

   ```
   status
   ```
   ```
   host connected  : no (0)
   ```
   `no` 表示还没连上，此时固件在持续广播。

2. Windows：**设置 → 蓝牙和其他设备 → 添加设备 → 蓝牙**。

3. 列表里应出现 **`Mi Remote Bridge`**。若没有，把 Windows 蓝牙关掉再打开，或重启 C3。

4. 点击配对。采用 Just Works，**不需要输入 PIN**；Windows 可能弹一次"已连接"通知。

5. 回到串口应看到：

   ```
   [  xx][WIN    ] host connected (id 1 addr xx:xx:.. mtu 517 total 1)
   [  xx][WIN    ] report 1 notifications ENABLED (id 1)
   [  xx][WIN    ] report 2 notifications ENABLED
   ```

   `report 1 notifications ENABLED` 是关键：**只有这一行出现后，Windows 才会真正接收按键**。
   如果连接了但没有这一行，说明 Windows 还没完成 HID 枚举，稍等几秒或断开重连。

6. Windows 里应出现：
   - **设置 → 蓝牙和其他设备**：一个名为 `Mi Remote Bridge` 的 **键盘**；
   - **设备管理器 → 键盘**：`Mi Remote Bridge`；
   - **设备管理器 → 人体学接口设备**：多一个符合 HID 标准的消费类控制设备。

---

## 3. 排查

### 上游不上报按键

| 现象 | 检查 |
| --- | --- |
| 完全不广播、扫描列表为空 | 遥控器在休眠。按任意键唤醒，或按 §1.1 让它进入配对广播 |
| 扫描到设备但连不上 | 遥控器已被别的设备（电视/盒子）占用连接。先在原设备上断开 |
| 连上了但 `ready` 前失败 | `raw on` 看服务发现日志；确认 `0x1812` 服务存在且 `0x2A4D` 可 notify |
| `ready` 了但按键无日志 | 打开 `raw on`。若连原始报文都没有，说明没订阅成功 → `reconnect` 重试 |
| 有原始报文但显示 `unknown key code` | 该键码不在表内。把 `raw` 里的码补进 `key_definitions.h` + `keymap.cpp`（见 `KEYMAP.md` §4） |
| 有原始报文但没转换日志 | 报文长度为 0 或全部槽位为 0，被正确识别为"释放" |

### 下游关联不上

| 现象 | 检查 |
| --- | --- |
| Windows 列表里没有设备 | `status` 看是否在广播；`forget win` 强制重广播；关/开 Windows 蓝牙 |
| 已连接但按键没反应 | 确认日志里有 `report 1 notifications ENABLED` |
| 显示为"未知设备" | 删除该配对，`forget win`，再重新配对（有时 Windows 会缓存旧的描述符） |
| 出现按键卡住 | 不应当发生；把 `raw on` 日志附上并记下按下的键。任一侧断线都会触发 `releaseAll()` |

---

## 4. 常用命令速查

| 命令 | 作用 |
| --- | --- |
| `status` | 全量状态：两侧连接、绑定、通知计数、队列、当前按键 |
| `scan` / `scan now` | 查看扫描器缓存 / 立刻开始新一轮扫描 |
| `connect <mac> [public\|random]` | 手动连接指定设备 |
| `reconnect` | 断开并重连 RC003 |
| `forget rc` | 删除 RC003 的 Bond 和保存的地址 |
| `forget win` | 删除主机 Bond 并重新广播 |
| `bond list` | 列出 NimBLE 里所有配对记录 |
| `key <hex> press\|release` | 注入一个合成按键，用于验证下游 HID 通道 |
| `channel consumer <hex>` | 直接发一个 Consumer Usage（验证媒体键） |
| `raw on\|off` | 原始报文日志 |
| `lat on\|off` | 延迟日志 |
| `selftest` | 设备端自检（解析器/映射/队列/分发） |
| `factory` | 清除全部 Bond 与设置并重启 |
