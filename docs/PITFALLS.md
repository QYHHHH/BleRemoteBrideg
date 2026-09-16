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

- **本板 DTR 接的是 BOOT（GPIO9）**——`DtrEnable=true` 等于按住 BOOT，5 秒后会
  触发固件的恢复出厂。监控时必须 `DtrEnable=$false`。详见
  [`SERIAL-CONSOLE.md`](SERIAL-CONSOLE.md) 的"与上位工具的边界"。
- **pyserial 打开端口瞬间 DTR/RTS 都会被置位**——`board_auth.py` 用
  `_SER.dtr = False` + `_SER.rts = False` 的**顺序**是承重的，必须**先放 DTR
  再放 RTS**，反了会被 strap 锁存成下载模式（EN 拉高那一刻 GPIO9 还是低）。
  看 [`TESTING.md`](TESTING.md) §DTR 处理里有完整取证。