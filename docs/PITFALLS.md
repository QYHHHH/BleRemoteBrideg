# 踩坑实录（开源最值钱的部分）

全部在真机上复现过，完整取证在 [`TESTING.md`](TESTING.md) §4。踩坑的**直接原因**
在这里，关键的修复细节去对应文档。

1. **板子必须 `FlashMode=dio`。** FQBN 默认 `qio` 把 flash 驱动配成 QIO（镜像头仍是
   DIO），部分板子的 Macronix flash 在 QIO 下不回应，bootloader 读到全 0xFF →
   `invalid magic number` 复位循环。实测 80M+QIO ❌ / 40M+QIO ❌ / 80M+DIO ✅。
2. **两个顶层集合共用一条 Report 特征 = Windows 拒绝启动 HID**（`Code 10`，
   问题状态 `0xC0110002` = `HIDP_STATUS_INVALID_REPORT_TYPE`）。合法 HID ≠ Windows
   接受；必须"每个集合自己的报告 ID + 各自的 Report 特征"。
3. **Arduino BLE 封装层注册不了两条同 UUID 特征**，第二条被静默丢弃且其 `m_pService`
   永不赋值 → `notify()` 读野指针、芯片崩溃。这是把 HID 服务下沉到 `ble_gatt_svc_def`
   的原因。
4. **`BLEClient::connect()` 的 timeout 参数被库忽略**；**`BLEAddress` 内部字节逆序**，
   用 `getNative()` 拼字符串再喂回去会得到反序地址——地址一律用 `toString()` 的
   规范文本形式。
5. **链路监督超时是硬复位恢复速度的旋钮**：库默认 4 秒，硬复位无法发断开包，
   遥控器会陪一个不存在的主机干等 4 秒。缩短到 1 秒后重启恢复快了 3 倍，
   代价为零。
6. **惰性发现与遍历顺序**：`getCharacteristics()` 首次访问才做 ATT 发现，
   且服务 map 按 UUID 字符串排序（电池排在 HID 前）。"先按键后电池"必须把
   过滤放在惰性调用**之前**。
7. **清配对必须两侧同时清**：只清桥接器侧，主机还留着旧密钥 → 每 6–7 秒
   断开重连一次（MTU 从 256 掉到 23），期间按键丢失。

## 串口监控陷阱（与 `SERIAL-CONSOLE.md` 联动）

- **DTR / RTS 是成对起作用的**，不是各管一根。本板是经典双三极管自动下载电路，
  2026-09-17 上机实测（判据 = HTTP `uptimeMs` 是否归零 + 串口有没有
  `[BUTTON ] key pressed`）：

  | (DTR, RTS) | 后果 |
  | --- | --- |
  | `(0,0)` / `(1,1)` | EN 高、GPIO9 高 —— **正常运行** |
  | `(0,1)` | EN 低 —— **板子被按在复位里** |
  | `(1,0)` | GPIO9 低 —— **等于按住 BOOT**（满 5 秒恢复出厂） |

- 所以 **`DtrEnable=true` 单独并不危险**，只有 RTS 同时为低（`(1,0)`）才是"按住
  BOOT"。监控时把**两根都设 `$false`**（`(0,0)`）就既不复位也不按住 BOOT。
- **复位发生在控制线"经过 `(0,1)`"的那一刻**，本项目脚本有**两处**：
  1. **`open()` 本身** —— pyserial 先置 RTS 再置 DTR，路径是
     `(0,0)→(0,1)→(1,1)`，所以**用 pyserial 开一次口就复位一次**。
     实测（`tests/tools/open_probe.py`）：不预置控制线开 → 出现启动横幅；把两根线
     预置成 `(0,0)` 再开 → 没有横幅、板子照常应答。
  2. **脚本自己那两行** —— `s.dtr = False` 先放 DTR（`(1,1)→(0,1)` → EN 拉低），
     再 `s.rts = False`（`(0,1)→(0,0)` → EN 拉高、板子启动），**再复位一次**。

  ⇒ `build/ser.py` / `serlog.py` 默认就是**两次**复位（刻意如此，保证拿到干净的
  启动横幅）。加 **`SER_NORESET=1` 实测降到 0 次**：它在 open 前先把两根线预置成
  `(0,0)`，上一次工具也留在 `(0,0)` 时就没有任何跳变。
- **Chromium 与 .NET 不复位**：Chrome 在一次 `SetCommState` 里同时置两根，不经过
  `(0,1)`；.NET `SerialPort` 默认 `(0,0)`。所以"开配网页会重启板子"不成立。
- **`close()` 不释放控制线。** 实测把 RTS 置位后关端口，板子仍被按在复位里，
  直到下一次 open 显式 `rts=False` 才起来。脚本收尾要显式放开两根，别只靠
  `close()`。
- 取证脚本：`tests/tools/line_probe.py`（逐根线隔离，结论以此为准）/
  `tests/tools/open_probe.py`（open 是否复位）/ `tests/tools/press_probe.py`
  （把 `(1,0)` 当"不用手按的 BOOT 键"，用来回归 BOOT 键行为）。
  另有一个早期的 `build/rts_probe.py` 留在 `build/`（该目录被 gitignore，
  只在开发机上存在，不要当依据引用）。
  详见 [`TESTING.md`](TESTING.md) §5.3.2。