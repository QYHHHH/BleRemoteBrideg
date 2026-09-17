/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * config.h - compile-time configuration
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

// ---------------------------------------------------------------------------
// Firmware identity
// ---------------------------------------------------------------------------
// The git tag is the release; this string is what the device reports. Nothing
// ties them together automatically, so bump both in the same commit:
//
//   release      v0.0.4          tag: v0.0.4
//   debug build  v0.0.4-improv   tag: unchanged
//
// A suffix names the one thing that build was made to test. It never means
// "newer than the tag" - a build that is ahead of the tag is waiting for the
// next release, not carrying a suffix. The point of the suffix is that a board
// found on a desk weeks later still says which experiment is on it.
//
// To make a suffixed build without editing this tracked file, pass -Version to
// scripts\build.ps1 or scripts\flash.ps1. That writes version_local.h (which
// .gitignore covers) and it wins over the default below.
#define BRIDGE_FW_NAME        "MiRemoteBridge"

#if defined(__has_include)
#if __has_include("version_local.h")
#include "version_local.h"  // build-time override, never committed
#endif
#endif

#ifndef BRIDGE_FW_VERSION
#define BRIDGE_FW_VERSION     "v0.0.7b"
#endif

// ---------------------------------------------------------------------------
// Downstream (peripheral) role: what Windows sees
// ---------------------------------------------------------------------------
// Keep this short: the BLE legacy advertising payload is only 31 bytes and the
// scan-response carries the name. Windows caches the name at pairing time, so
// changing it after pairing requires re-pairing on the Windows side.
#define BRIDGE_HID_DEVICE_NAME  "Mi Remote Bridge"

// Battery level served over our own BLE battery service (0x180F/0x2A19).
//
// This is only the value used *before* the remote has reported one. The RC003
// exposes the standard battery service, so the real number is read from it and
// forwarded to the host - see BRIDGE_BATTERY_PASSTHROUGH. Reporting a constant
// 100% would tell Windows the remote is always full, which is worse than saying
// nothing.
#define BRIDGE_BATTERY_LEVEL    100

// Forward the RC003's battery level to the host instead of the constant above.
// Set to 0 to always report BRIDGE_BATTERY_LEVEL.
#define BRIDGE_BATTERY_PASSTHROUGH 1

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

// Standard battery service. The remote exposes it, so its charge can be read
// there and re-published by our own battery service.
#define RC003_BATTERY_SVC_UUID  "180f"
#define RC003_BATTERY_LEVEL_UUID "2a19"

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

// Status LEDs (status_led.cpp). Two on-board LEDs report the two links:
//   D5 = link to the computer   breathing = waiting, fast blink = linked but
//                               HID not ready, solid = subscribed,
//                               double flash = re-add the device in Windows
//   D4 = link to the remote     same rhythms, double flash = pairing must be
//                               redone, plus dark while a remote key is held
// Both LEDs go dark after ten minutes with no activity; the double flash is
// subject to the same window and the Web UI keeps showing the notice.
// Hezhou CORE-ESP32 wiring: D4 = GPIO12, D5 = GPIO13, both active HIGH. They
// are free only because this board runs its flash in DIO mode (see the
// FlashMode note above) - in QIO they double as SPIHD/SPIWP.
#define BRIDGE_LED_HOST_PIN       13   // D5
#define BRIDGE_LED_REMOTE_PIN     12   // D4

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
