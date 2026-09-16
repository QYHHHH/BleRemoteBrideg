# 麦克风音频可行性评估

评估四种把遥控器麦克风音频送到主机的架构。

---

## 0. 结论

| 架构 | 芯片 | 内存增量 | 主机端 | Windows 可用 | 延迟 |
| --- | --- | --- | --- | --- | --- |
| A：BLE → BLE 中继 | C3（现成） | 13–17 kB | 需支持 ATVV | **否** | 100–200 ms |
| B：USB UAC | **需 S3** | 20–30 kB | 免驱 | 是 | **10–30 ms** |
| C：Wi-Fi 转 PC | C3（现成） | 8–12 kB | 需装程序 | 是 | 50–150 ms |
| **D：BLE → Classic HFP** | **原版 ESP32** | **55–75 kB** | **免驱** | **是** | **85–175 ms** |

> **方案 D（本次新增）**：延迟约 **85–175 ms**，量级和 BLE 中继相当，比 USB UAC 差一个数量级。
> 但它**免驱**——Windows 原生支持 HFP，会直接认成录音设备。
>
> **隐藏成本很大**：换原版 ESP32 意味着必须用 **Bluedroid**（NimBLE 不支持 Classic BT），
> 内存开销比现在大得多，而且**整个 BLE 层要重写**。详见第 6.3 节。

---

## 1. 实测内存基线

数据取自仓库日志，是固件自己上报的读数：

| 状态 | free heap | 最大连续块 | 历史最低 |
| --- | --- | --- | --- |
| 开机瞬间（BLE 未起） | 142,836 B | 114,676 B | 142,788 B |
| BLE + Wi-Fi 起来后 | 86,516 B | 77,812 B | 86,156 B |
| **稳态（网页 websocket 已连）** | **29,108 B** | **20,468 B** | **10,940 B** |

堆总量 264,760 B（ESP32-C3）。

> **真正的约束是 `largest block`，不是 `free heap`。** 稳态最大连续块只有 13–20 kB。
>
> **Wi-Fi 配置页面吃掉约 113 kB**（142 kB → 29 kB）。做音频时关掉 Web UI 是最大的单笔回收。

---

## 2. 音频链路的数据量

参考实现（`dragon3385/MiVoiceMic`、`QL-4/RemoteMapper`）实测：

```
遥控器 --BLE ATVV--> 主机
        16 kHz / 单声道 / IMA ADPCM (4 bit)
```

| 形态 | 码率 |
| --- | --- |
| IMA ADPCM（空口实际传输） | **8,000 B/s** |
| 解码后 PCM | 32,000 B/s |

**BLE 吞吐要求**：MTU 23（默认）时每包 20 B，15 ms 间隔下只有 ~1,333 B/s，**差 6 倍**；
MTU 247 时 ~16,267 B/s，够用。→ **MTU 协商到 247 是硬前提。**

---

## 3. 方案 A：BLE → BLE 中继

```
遥控器 ──BLE(ATVV)──► ESP32-C3 ──BLE(ATVV)──► 主机
         Central 角色          Peripheral 角色
```

内存增量 **13–17 kB**：上游缓冲 2 kB + 下游缓冲 2 kB + ATVV 服务表 ~1 kB +
转发任务栈 4 kB + NimBLE mbuf 池扩容 4–8 kB。

**卡点在主机侧**：ATVV 是 Google 为 Android TV 定义的自定义 GATT profile，
**Android 原生支持，Windows 从未实现**。证据：Windows 上所有可用方案
（`MiVoiceMic`、`techdou/VoiceHub`、`QL-4/RemoteMapper`）都是自己在 PC 端
实现 ATVV 解码 + VB-CABLE 虚拟声卡。若 Windows 原生支持，这些项目一个都不需要存在。

→ **Android 主机成立（免驱），Windows 主机直接排除。**

---

## 4. 方案 B：USB UAC

需要 **ESP32-S3**（有 USB OTG；C3 只有 USB-Serial-JTAG，做不了 USB 音频类）。

内存增量 **20–30 kB**：ADPCM 缓冲 2 kB + PCM 缓冲 8 kB + TinyUSB UAC2 栈 4–8 kB +
USB 任务栈 2–4 kB + mbuf 扩容 2–4 kB。

**延迟最低**（USB 等时传输，缓冲可以做到很小）。参考：`openbrt/voxstick`
（StickS3 上 UAC 麦克风 + HID 复合设备，免驱）。

---

## 5. 方案 C：Wi-Fi 转 PC 程序

内存增量 **8–12 kB**，但需要 PC 端接收程序——**违反项目边界**
（README 明确排除"Windows 伴侣程序、虚拟声卡、内核驱动、Wi-Fi/Web 后台"）。

---

## 6. 方案 D：BLE → Classic BT HFP（本次重点）

### 6.1 为什么这条路通

**Windows 原生支持 HFP**——蓝牙耳机的麦克风就是它，插上就认成录音设备，**免驱**。

ESP32 侧确认可行：ESP-IDF 提供 **HFP Client（免提单元 HF）** API，
`esp_hf_client_audio_data_send()` 可以把麦克风音频发给 AG（也就是 Windows），
走 **Voice Over HCI** 数据路径。支持的编码：

| 编码 | 采样率 | 音质带宽 | 状态机取值 |
| --- | --- | --- | --- |
| **CVSD** | 8 kHz | 窄带 3.4 kHz | `ESP_HF_CLIENT_AUDIO_STATE_CONNECTED` |
| **mSBC** | 16 kHz | 宽带 7 kHz | `ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC` |

> **编码选择很重要**：CVSD 要求喂 **8 kHz / 16-bit / 单声道** PCM，
> 意味着要把遥控器的 16 kHz 音频**降采样一半**，高频直接丢掉。
> **mSBC 是 16 kHz 宽带，和遥控器原生采样率一致，不用降采样**——应当优先用 mSBC。

### 6.2 延迟预算

```
遥控器 ──BLE ATVV──► 原版 ESP32 ──Classic BT HFP──► Windows
     16 kHz ADPCM      解码 + 重采样 + 缓冲        mSBC 16k / CVSD 8k
```

| 环节 | 延迟 | 说明 |
| --- | --- | --- |
| 上游 BLE（连接间隔 + 分帧） | 15–30 ms | 现有代码连接间隔 12 units = 15 ms |
| ATVV 抖动缓冲 | 20–40 ms | 需覆盖 1–2 个连接间隔的抖动 |
| ADPCM 解码 + 重采样 + 转码 | 1–5 ms | 逐样本编码，无帧延迟；CVSD 需 16k→8k 降采样 |
| **HFP 编码** | **CVSD <20 ms / mSBC ~30 ms** | 实测数据 |
| eSCO 传输 + 重传 | 10–30 ms | |
| Windows HFP 驱动 + 音频引擎 | 20–50 ms | 不确定项，波动最大 |
| **合计** | **约 85–175 ms** | 中位约 **120–130 ms** |

**人耳感知参考**：

| 阈值 | 表现 |
| --- | --- |
| < 35 ms | 几乎所有人无法识别 |
| ~ 50 ms | 专业训练人员（如电竞选手）可识别 |
| < 80 ms | 普通人无感知 |

→ **85–175 ms 已经超过普通人可感知的阈值。**

**但对"按住说话、松开出字"的语音听写场景，延迟完全不敏感**——你不需要实时监听自己。
只有在实时通话（需要侧音）场景下才会明显感觉到。

### 6.3 隐藏成本：Bluedroid + 整层重写

这是方案 D 最容易被低估的地方。

| 问题 | 说明 |
| --- | --- |
| **必须用 Bluedroid** | NimBLE 是纯 BLE 栈，**不支持 Classic BT**。要用 HFP 就只能切到 Bluedroid |
| **Bluedroid 比 NimBLE 重得多** | 双模（Classic + BLE）Bluedroid 的堆开销通常在 **40–60 kB** 量级，而当前 NimBLE 很轻 |
| **BLE 层要重写** | `ble_bonds.cpp` 用的是 NimBLE 专有 API（`ble_store_read_our_sec`、`ble_gap_unpair`、`os_mbuf_copydata`、`ble_gap_event`），Bluedroid 是完全不同的一套 API |
| **现有 C3 移植工作作废** | 整个项目从 C3 + NimBLE 换到原版 ESP32 + Bluedroid |

**综合内存增量**：13–17 kB（音频缓冲）+ 40–60 kB（Bluedroid vs NimBLE）
≈ **55–75 kB**，比方案 B（20–30 kB）大一倍以上。

原版 ESP32 有 520 kB SRAM（比 C3 的 400 kB 多），但 Bluedroid 双模 + Wi-Fi
一起吃下来，可用堆未必比现在宽松。

### 6.4 单射频共存风险

原版 ESP32 也只有**一个 2.4 GHz 射频**，而方案 D 要同时跑：

| 链路 | 特性 |
| --- | --- |
| BLE Central（连遥控器） | 周期性连接事件，15 ms 间隔 |
| Classic BT eSCO（连 Windows） | **同步连接，周期性占用固定时隙**（如 S4 设置约每 5 ms 一次） |
| （若保留）Wi-Fi | 与上面两者争用同一射频 |

**eSCO 是同步连接，会按固定周期抢占时隙**，BLE 的连接事件只能塞进空隙里。
ESP32 的共存机制能处理，但 **BLE 侧的吞吐和延迟会明显劣化**，上游音频可能丢包。

这是方案 D 最实际的技术风险：**上游（BLE）和下游（Classic）同时要求低延迟，
而它们只有一个射频。**

---

## 7. 四种方案的延迟横向对比

| 架构 | 延迟 | 免驱 | 芯片 | 内存增量 |
| --- | --- | --- | --- | --- |
| B：USB UAC | **10–30 ms** | 是 | S3 | 20–30 kB |
| C：Wi-Fi 转 PC | 50–150 ms | 否 | C3 | 8–12 kB |
| **D：Classic HFP** | **85–175 ms** | **是** | **原版 ESP32** | **55–75 kB** |
| A：BLE 中继 | 100–200 ms | 是（仅 Android） | C3 | 13–17 kB |

> 延迟数字除 HFP 编码（实测）外均为估算，受实现和缓冲策略影响较大。

---

## 8. 建议

1. **如果延迟是首要指标** → 方案 B（ESP32-S3 + USB UAC），10–30 ms，免驱，内存也最省。
   代价是换芯片，但**不需要重写 BLE 层**（S3 同样跑 NimBLE）。
2. **如果坚持免驱且不换 S3** → 方案 D 可行，但先接受三个代价：
   Bluedroid 内存开销、BLE 层重写、单射频共存导致的音频毛刺风险。
3. **如果主机是 Android** → 方案 A，最省事，不用换芯片。
4. **如果主机是 Windows 且能接受装程序** → 方案 C 或直接用现成的
   `MiVoiceMic` 一类项目，未必需要 ESP32 参与音频。

**方案 D 的性价比取决于"能不能接受 100 ms 级延迟"和"愿不愿意重写 BLE 层"。**
如果两个都能接受，它确实是 Windows 上唯一免驱的 BLE 路径。

---

## 9. 待补充

- [ ] **目标主机是 Windows 还是 Android**（决定性问题）
- [ ] 目标遥控器的麦克风是否标准 ATVV（nRF Connect 看有没有 ATVV 音频特征值）
- [ ] 音频编码：IMA ADPCM 还是 Opus（Opus 需再加 20–30 kB）
- [ ] 延迟容忍度：语音听写（不敏感）还是实时通话（敏感）
- [ ] 是否接受重写 BLE 层
