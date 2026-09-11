/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * keymap.h - RC003 raw code -> HID action translation (pure, no Arduino deps)
 *
 * This file deliberately has no dependency on Arduino or NimBLE so it can be
 * compiled and exercised by the host-side model tests (tests/).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "key_definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  HID_ACT_NONE = 0,   // no action (unmapped or disabled)
  HID_ACT_KEYBOARD,   // emit modifier + keycode on report id 1
  HID_ACT_CONSUMER    // emit 16-bit consumer usage on report id 2
} hid_action_kind_t;

typedef struct {
  hid_action_kind_t kind;
  uint8_t           modifier;   // HID_MOD_* (keyboard only)
  uint8_t           keycode;    // HID_KEY_*  (keyboard only)
  uint16_t          consumer;   // HID_CONSUMER_* (consumer only)
} hid_action_t;

// The HID report layout (report id, field offsets and lengths) lives in
// hid_report_map.h so the descriptor, the report builder and the tests all read
// from one place.

// ---------------------------------------------------------------------------
// Mapping table
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t    raw_code;    // MI_KEY_*
  hid_action_t press;     // action emitted while the key is held
} keymap_entry_t;

// Number of physical keys in the default map (13).
size_t keymap_default_count(void);

// The default table. Index 0..keymap_default_count()-1.
const keymap_entry_t *keymap_default_table(void);

// Look up the action for a raw RC003 code.
// Returns a HID_ACT_NONE action when the code is unknown or fully unmapped.
// `table`/`count` may be NULL/0 to use the built-in default map.
hid_action_t keymap_lookup_ex(const keymap_entry_t *table, size_t count, uint8_t raw_code);
hid_action_t keymap_lookup(uint8_t raw_code);

// True when `raw_code` is a code this firmware knows about (including the
// synonym aliases). Used to decide whether an unknown code should be logged.
bool keymap_is_known(uint8_t raw_code);

// Human readable name of a raw code ("VOL_UP", "UNKNOWN", ...). Never NULL.
const char *keymap_raw_name(uint8_t raw_code);

// Human readable description of an action, written into `buf`.
// Example: "KB LALT+F4", "CONSUMER 0x00E9", "NONE".
void keymap_describe(const hid_action_t *action, char *buf, size_t buf_len);

// ---------------------------------------------------------------------------
// Runtime-overridable mappings
// ---------------------------------------------------------------------------
// Three keys have a runtime-selectable behaviour because the "right" choice is
// host-application dependent. Everything else is fixed.
typedef enum {
  MAP_BACK_CONSUMER_BACK = 0,  // AC Back      (browser/app back)
  MAP_BACK_KEYBOARD_ESC,       // Esc
  MAP_BACK_KEYBOARD_ALT_LEFT,  // Alt + Left
  MAP_BACK_COUNT
} keymap_back_mode_t;

typedef enum {
  MAP_POWER_ALT_F4 = 0,        // Alt + F4
  MAP_POWER_CONSUMER_SLEEP,    // Consumer Sleep
  MAP_POWER_CONSUMER_POWER,    // Consumer Power
  MAP_POWER_KEYBOARD_ESC,      // Esc
  MAP_POWER_COUNT
} keymap_power_mode_t;

typedef enum {
  MAP_VOICE_RALT_COMMA = 0,    // Right Alt + Comma (WeChat/TIM voice IME)
  MAP_VOICE_CONSUMER_MUTE,     // Mute
  MAP_VOICE_KB_LCTRL_LGUI,     // Left Ctrl + Left Win, no base key (chord)
  MAP_VOICE_DISABLED,          // do not forward the voice button
  MAP_VOICE_COUNT
} keymap_voice_mode_t;

const char *keymap_back_mode_name(keymap_back_mode_t m);
const char *keymap_power_mode_name(keymap_power_mode_t m);
const char *keymap_voice_mode_name(keymap_voice_mode_t m);

// Parse a human readable mode name; returns false when unrecognised.
bool keymap_back_mode_parse(const char *s, keymap_back_mode_t *out);
bool keymap_power_mode_parse(const char *s, keymap_power_mode_t *out);
bool keymap_voice_mode_parse(const char *s, keymap_voice_mode_t *out);

// Apply the runtime selection. Call once after loading NVS, and again whenever
// the user changes it from the console.
void keymap_set_back_mode(keymap_back_mode_t m);
void keymap_set_power_mode(keymap_power_mode_t m);
void keymap_set_voice_mode(keymap_voice_mode_t m);

keymap_back_mode_t  keymap_get_back_mode(void);
keymap_power_mode_t keymap_get_power_mode(void);
keymap_voice_mode_t keymap_get_voice_mode(void);

// ---------------------------------------------------------------------------
// Programmable bindings (Web UI / console)
//
// A binding remaps one raw code to any single action - a keyboard chord or a
// consumer usage - and takes precedence over both the runtime modes and the
// default table. One key maps to one action; presses and releases are
// forwarded in real time exactly like the built-in map (no click/double/long
// differentiation, no macros).
//
// This layer is deliberately memory-only: persistence lives in the settings
// module (NVS), which calls keymap_set_binding() at boot and on every change.
// ---------------------------------------------------------------------------
#define KEYMAP_BIND_KIND_NONE 0      // remove a binding (fall back to defaults)
#define KEYMAP_BIND_KIND_KB   1      // keyboard: modifier + keycode
#define KEYMAP_BIND_KIND_CONS 2      // consumer: 16-bit usage

#define KEYMAP_MAX_BINDINGS 16

// Set (or with kind == KEYMAP_BIND_KIND_NONE, clear) the binding for one raw
// code. Returns false when the table is full or the arguments are invalid.
bool keymap_set_binding(uint8_t raw_code, uint8_t kind, uint8_t modifier, uint8_t keycode,
                        uint16_t consumer);
// True when this raw code has an explicit user binding.
bool keymap_has_binding(uint8_t raw_code);
// Number of explicit bindings currently held.
size_t keymap_binding_count(void);
// Copy all bindings out (for `bind list` and the Web UI). Returns the count.
size_t keymap_get_bindings(uint8_t *raw_out, hid_action_t *actions_out, size_t max_out);

#ifdef __cplusplus
}
#endif
