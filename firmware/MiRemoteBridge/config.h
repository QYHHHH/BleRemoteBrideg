/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * config.h - compile-time configuration
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
#define BRIDGE_FW_NAME        "MiRemoteBridge"
#define BRIDGE_FW_VERSION     "0.3.0"

// ---------------------------------------------------------------------------
// Downstream (peripheral) role: what Windows sees
// ---------------------------------------------------------------------------
// Keep this short: the BLE legacy advertising payload is only 31 bytes and the
// scan-response carries the name. Windows caches the name at pairing time, so
// changing it after pairing requires re-pairing on the Windows side.
#define BRIDGE_HID_DEVICE_NAME  "Mi Remote Bridge"

// Battery level reported over the BLE battery service (cosmetic only).
#define BRIDGE_BATTERY_LEVEL    100

// ---------------------------------------------------------------------------
// Upstream (central) role: Xiaomi Bluetooth Remote 2 Pro (RC003)
// ---------------------------------------------------------------------------
// RC003 is a HOGP (HID over GATT, 0x1812) device that additionally exposes the
// proprietary ATVV service used for its voice/microphone channel.
//
// This firmware never touches the audio path: it does NOT subscribe to the ATVV
// audio characteristic and never sends MIC_OPEN. The ATVV control characteristic
// is subscribed read-only purely so the physical voice *button* can be observed
// as a key. Set to 0 to disable even that.
#define BRIDGE_ATVV_CTL_ENABLE  1

#define RC003_HOGP_SVC_UUID     "1812"
#define RC003_HID_REPORT_UUID   "2a4d"
#define RC003_PROTOCOL_MODE_UUID "2a4e"
#define RC003_HID_CTRL_POINT_UUID "2a4c"

#define RC003_ATVV_SVC_UUID     "ab5e0001-5a21-4f05-bc7d-af01f617b664"
#define RC003_ATVV_CHAR_CMD_UUID "ab5e0002-5a21-4f05-bc7d-af01f617b664"
#define RC003_ATVV_CHAR_AUD_UUID "ab5e0003-5a21-4f05-bc7d-af01f617b664"
#define RC003_ATVV_CHAR_CTL_UUID "ab5e0004-5a21-4f05-bc7d-af01f617b664"

// Name fragments used for identification while *unbound* only.
//
// The remote advertises a LOCALISED name. An RC003 in pairing mode reported
// "小米蓝牙语音遥控器" ("Xiaomi Bluetooth Voice Remote") on the bench, which no
// ASCII fragment matches - that is why the Chinese forms are listed too.
//
// The Chinese entries are written as explicit UTF-8 byte escapes instead of
// literal characters so the match cannot depend on the compiler's
// source-charset setting.
//
// Deliberately absent: a bare "小米" / "Xiaomi". This bridge lives in a flat with
// several Xiaomi appliances - a Mijia scale and an air quality monitor both
// appeared in the first scan - so matching the brand alone would eventually
// latch onto the wrong device. Only product words ("remote", "voice") count.
#define RC003_NAME_HINT_1   "MI RC"
#define RC003_NAME_HINT_2   "MI Remote"
#define RC003_NAME_HINT_3   "Remote"
#define RC003_NAME_HINT_4   "RC003"
#define RC003_NAME_HINT_5   "\xe9\x81\xa5\xe6\x8e\xa7"  // 遥控 - "remote"
#define RC003_NAME_HINT_6   "\xe8\xaf\xad\xe9\x9f\xb3"  // 语音 - "voice"

// ---------------------------------------------------------------------------
// Scan / reconnect timing
// ---------------------------------------------------------------------------
// Scanning runs only while the remote is not connected. It is duty-cycled so
// the concurrently active link to Windows (peripheral role) keeps enough radio
// time and the serial log stays readable.
#define BRIDGE_SCAN_INTERVAL_MS   100
#define BRIDGE_SCAN_WINDOW_MS     50
#define BRIDGE_SCAN_BURST_MS      20000   // scan continuously for this long
#define BRIDGE_SCAN_IDLE_MS       3000    // then pause this long before retrying

// There is no connect-timeout knob to set on this library version.
//
// BLEClient::connect(addr, type, timeoutMs) silently ignores its third argument:
// the NimBLE backend passes the private m_connectTimeout to ble_gap_connect()
// instead, it defaults to 30 s, and no setter for it exists anywhere in the BLE
// library (checked by grep). A connect attempt to a peer that is not advertising
// therefore blocks for the full 30 s. That is normal behaviour, not a hang, and
// the log has to say so - otherwise the silence is indistinguishable from a
// deadlock, which is exactly how it read on the bench.
#define BRIDGE_CONNECT_LIB_TIMEOUT_MS  30000

// ---------------------------------------------------------------------------
// Runtime behaviour toggles
// ---------------------------------------------------------------------------
// Print every raw RC003 report on the serial console. Can be toggled at runtime
// with the `raw on` / `raw off` console command.
#define BRIDGE_LOG_RAW_DEFAULT    0

// Forward key codes that are not present in the mapping table (as raw HID
// keyboard usage). Off by default: an unknown Xiaomi code is almost never a
// valid HID usage, and forwarding it could produce arbitrary keystrokes.
#define BRIDGE_PASS_UNKNOWN_KEYS  0

// Print the raw electrical timing measurement (notification -> HID notify) for
// every key event. `lat on` / `lat off`.
#define BRIDGE_LATENCY_LOG_DEFAULT 1

// Optional status LED. Disabled by default because GPIO8 is wired to different
// things on different C3 boards. Set to 1 and adjust the pin if you want it.
#define BRIDGE_STATUS_LED_ENABLE  0
#define BRIDGE_STATUS_LED_PIN     8
#define BRIDGE_STATUS_LED_ACTIVE_LOW 1

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------
#define BRIDGE_SERIAL_BAUD        115200
#define BRIDGE_CONSOLE_LINE_MAX   96

// ---------------------------------------------------------------------------
// Event queue
// ---------------------------------------------------------------------------
// Fixed capacity, single-producer (BLE host task) / single-consumer (loop).
// Sized so a burst of keystrokes produced while the loop is busy is never lost.
#define BRIDGE_EVENT_QUEUE_SIZE   64

// ---------------------------------------------------------------------------
// NVS
// ---------------------------------------------------------------------------
#define BRIDGE_NVS_NAMESPACE      "mibridge"
