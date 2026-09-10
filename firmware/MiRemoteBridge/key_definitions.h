/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * key_definitions.h - RC003 raw key codes and standard HID usages
 *
 * The RC003 "raw key codes" below are the values the remote puts into the key
 * slot of its HID input report. They are NOT standard HID usages: Xiaomi reuses
 * the HID report layout but substitutes its own code space (e.g. 0x80/0x81 for
 * volume, 0xF1 for back). Because they are not valid usages, Windows' own
 * kbdhid.sys silently drops several of them - which is the whole reason this
 * bridge exists.
 *
 * The code list was cross-checked against the RC003 protocol notes published in
 * the RemoteMapper-ESP32 project (MIT, see docs/THIRD_PARTY_NOTICES.md). The
 * numeric codes themselves are hardware facts observed on the wire; the tables
 * and the implementation here are original.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

// ===========================================================================
// 1. RC003 physical keys (13 total), as seen in the HID report key slot
// ===========================================================================
#define MI_KEY_VOL_UP        0x80   // Volume +
#define MI_KEY_VOL_DOWN      0x81   // Volume -
#define MI_KEY_BACK          0xF1   // Back / return
#define MI_KEY_POWER         0x66   // Power
#define MI_KEY_HOME          0x24   // Home
#define MI_KEY_MENU          0x5D   // Menu
#define MI_KEY_TV            0xC0   // TV
#define MI_KEY_UP            0x52   // D-pad up
#define MI_KEY_DOWN          0x51   // D-pad down
#define MI_KEY_LEFT          0x50   // D-pad left
#define MI_KEY_RIGHT         0x4F   // D-pad right
#define MI_KEY_OK            0x28   // OK / confirm
#define MI_KEY_VOICE         0x04   // Voice (ATVV control channel)

// Alternate codes observed on some RC003 units / firmware revisions. They are
// accepted as synonyms of the key listed in the comment. Keeping them costs a
// few table entries and avoids a silently dead button.
#define MI_KEY_POWER_ALT     0xFF
#define MI_KEY_HOME_ALT      0x4A
#define MI_KEY_MENU_ALT      0x65
#define MI_KEY_TV_ALT        0x35
#define MI_KEY_VOICE_ALT     0x3E   // Voice as seen in the HOGP report path

#define MI_KEY_COUNT 13

// ===========================================================================
// 2. USB HID keyboard modifier bit mask (byte 0 of the keyboard report)
// ===========================================================================
#define HID_MOD_NONE         0x00
#define HID_MOD_LCTRL        0x01
#define HID_MOD_LSHIFT       0x02
#define HID_MOD_LALT         0x04
#define HID_MOD_LGUI         0x08   // Left Windows key
#define HID_MOD_RCTRL        0x10
#define HID_MOD_RSHIFT       0x20
#define HID_MOD_RALT         0x40   // Right Alt (AltGr)
#define HID_MOD_RGUI         0x80   // Right Windows key

// ===========================================================================
// 3. HID keyboard usages (HID Usage Tables 1.12, Usage Page 0x07)
// ===========================================================================
#define HID_KEY_NONE         0x00
#define HID_KEY_A            0x04
#define HID_KEY_D            0x07
#define HID_KEY_ENTER        0x28
#define HID_KEY_ESC          0x29
#define HID_KEY_BACKSPACE    0x2A
#define HID_KEY_TAB          0x2B
#define HID_KEY_SPACE        0x2C
#define HID_KEY_COMMA        0x36
#define HID_KEY_F4           0x3D
#define HID_KEY_F5           0x3E
#define HID_KEY_F8           0x41
#define HID_KEY_RIGHT        0x4F
#define HID_KEY_LEFT         0x50
#define HID_KEY_DOWN         0x51
#define HID_KEY_UP           0x52

// ===========================================================================
// 4. HID Consumer Control usages (Usage Page 0x0C)
// ===========================================================================
#define HID_CONSUMER_NONE        0x0000
#define HID_CONSUMER_POWER       0x0030
#define HID_CONSUMER_SLEEP       0x0032
#define HID_CONSUMER_PLAY_PAUSE  0x00CD
#define HID_CONSUMER_MUTE        0x00E2
#define HID_CONSUMER_VOL_UP      0x00E9
#define HID_CONSUMER_VOL_DOWN    0x00EA
#define HID_CONSUMER_NEXT_TRACK  0x00B5
#define HID_CONSUMER_PREV_TRACK  0x00B6
#define HID_CONSUMER_AC_HOME     0x0223
#define HID_CONSUMER_AC_BACK     0x0224

// Highest consumer usage that still fits the 16-bit array report we advertise
// in the report descriptor (logical/usage maximum 0x03FF).
#define HID_CONSUMER_MAX_USAGE   0x03FF

// ===========================================================================
// 5. Consumer Control operation codes used by the ATVV control channel
//    (only the ones relevant to the voice button are listed)
// ===========================================================================
#define ATVV_OP_AUDIO_START      0x04
#define ATVV_OP_AUDIO_STOP       0x00
#define ATVV_OP_MIC_CLOSED       0x08
