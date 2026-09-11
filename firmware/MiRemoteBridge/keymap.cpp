/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * keymap.cpp - RC003 raw code -> HID action translation
 *
 * Every mapping is strictly one-to-one: one physical button down produces one
 * HID key down, one button up produces one HID key up. There is no auto-repeat,
 * no macro, no combo sequencing and no game logic anywhere in this file.
 *
 * SPDX-License-Identifier: MIT
 */

#include "keymap.h"

#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Default map: 13 physical keys of the RC003.
//
// Choice notes:
//  * D-pad / OK use plain arrow keys and Enter - the most useful defaults on a
//    desktop and the ones most likely to work in every application.
//  * VOL+ / VOL- use the Consumer Control page, which is exactly what Windows
//    expects for a media device. They are *not* forwarded as keyboard keys,
//    so they will not disturb a focused text field.
//  * HOME = Win+D (show desktop), TV = F8, MENU = Space.
//  * BACK / POWER / VOICE are runtime-selectable, see keymap_set_*_mode().
// ---------------------------------------------------------------------------
static const keymap_entry_t kDefaultTable[] = {
    // raw code            action kind        modifier       keycode        consumer
    { MI_KEY_VOL_UP,   { HID_ACT_CONSUMER, HID_MOD_NONE, HID_KEY_NONE,  HID_CONSUMER_VOL_UP  } },
    { MI_KEY_VOL_DOWN, { HID_ACT_CONSUMER, HID_MOD_NONE, HID_KEY_NONE,  HID_CONSUMER_VOL_DOWN} },
    { MI_KEY_UP,       { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_UP,    0                    } },
    { MI_KEY_DOWN,     { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_DOWN,  0                    } },
    { MI_KEY_LEFT,     { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_LEFT,  0                    } },
    { MI_KEY_RIGHT,    { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_RIGHT, 0                    } },
    { MI_KEY_OK,       { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_ENTER, 0                    } },
    { MI_KEY_HOME,     { HID_ACT_KEYBOARD, HID_MOD_LGUI, HID_KEY_D,     0                    } },
    { MI_KEY_MENU,     { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_SPACE, 0                    } },
    { MI_KEY_TV,       { HID_ACT_KEYBOARD, HID_MOD_NONE, HID_KEY_F8,    0                    } },
    // Filled in by keymap_lookup() from the runtime mode selection:
    { MI_KEY_BACK,     { HID_ACT_CONSUMER, HID_MOD_NONE, HID_KEY_NONE,  HID_CONSUMER_AC_BACK  } },
    { MI_KEY_POWER,    { HID_ACT_KEYBOARD, HID_MOD_LALT, HID_KEY_F4,    0                    } },
    { MI_KEY_VOICE,    { HID_ACT_KEYBOARD, HID_MOD_RALT, HID_KEY_COMMA, 0                    } },
};

#define DEFAULT_TABLE_COUNT (sizeof(kDefaultTable) / sizeof(kDefaultTable[0]))

size_t keymap_default_count(void) {
  return DEFAULT_TABLE_COUNT;
}

const keymap_entry_t *keymap_default_table(void) {
  return kDefaultTable;
}

// ---------------------------------------------------------------------------
// Runtime mode selection
// ---------------------------------------------------------------------------
static keymap_back_mode_t  s_back_mode  = MAP_BACK_CONSUMER_BACK;
static keymap_power_mode_t s_power_mode = MAP_POWER_ALT_F4;
static keymap_voice_mode_t s_voice_mode = MAP_VOICE_RALT_COMMA;

void keymap_set_back_mode(keymap_back_mode_t m) {
  if (m >= 0 && m < MAP_BACK_COUNT) s_back_mode = m;
}
void keymap_set_power_mode(keymap_power_mode_t m) {
  if (m >= 0 && m < MAP_POWER_COUNT) s_power_mode = m;
}
void keymap_set_voice_mode(keymap_voice_mode_t m) {
  if (m >= 0 && m < MAP_VOICE_COUNT) s_voice_mode = m;
}

keymap_back_mode_t  keymap_get_back_mode(void)  { return s_back_mode; }
keymap_power_mode_t keymap_get_power_mode(void) { return s_power_mode; }
keymap_voice_mode_t keymap_get_voice_mode(void) { return s_voice_mode; }

static const char *const kBackNames[MAP_BACK_COUNT] = {
    "consumer_back",  // MAP_BACK_CONSUMER_BACK
    "kb_esc",         // MAP_BACK_KEYBOARD_ESC
    "kb_alt_left",    // MAP_BACK_KEYBOARD_ALT_LEFT
};

static const char *const kPowerNames[MAP_POWER_COUNT] = {
    "kb_alt_f4",        // MAP_POWER_ALT_F4
    "consumer_sleep",   // MAP_POWER_CONSUMER_SLEEP
    "consumer_power",   // MAP_POWER_CONSUMER_POWER
    "kb_esc",           // MAP_POWER_KEYBOARD_ESC
};

static const char *const kVoiceNames[MAP_VOICE_COUNT] = {
    "kb_ralt_comma",   // MAP_VOICE_RALT_COMMA
    "consumer_mute",   // MAP_VOICE_CONSUMER_MUTE
    "kb_lctrl_lgui",   // MAP_VOICE_KB_LCTRL_LGUI
    "disabled",        // MAP_VOICE_DISABLED
};

const char *keymap_back_mode_name(keymap_back_mode_t m) {
  return (m >= 0 && m < MAP_BACK_COUNT) ? kBackNames[m] : "?";
}
const char *keymap_power_mode_name(keymap_power_mode_t m) {
  return (m >= 0 && m < MAP_POWER_COUNT) ? kPowerNames[m] : "?";
}
const char *keymap_voice_mode_name(keymap_voice_mode_t m) {
  return (m >= 0 && m < MAP_VOICE_COUNT) ? kVoiceNames[m] : "?";
}

static bool parse_mode(const char *s, const char *const *names, int count, int *out) {
  if (!s || !names || !out) return false;
  for (int i = 0; i < count; i++) {
    // Accept an unambiguous prefix so "kb_a" works for "kb_alt_left" etc.
    size_t n = strlen(names[i]);
    if (strncmp(s, names[i], n) == 0 && (s[n] == '\0' || s[n] == ' ')) {
      *out = i;
      return true;
    }
  }
  return false;
}

bool keymap_back_mode_parse(const char *s, keymap_back_mode_t *out) {
  int v = 0;
  if (!parse_mode(s, kBackNames, MAP_BACK_COUNT, &v)) return false;
  *out = (keymap_back_mode_t)v;
  return true;
}
bool keymap_power_mode_parse(const char *s, keymap_power_mode_t *out) {
  int v = 0;
  if (!parse_mode(s, kPowerNames, MAP_POWER_COUNT, &v)) return false;
  *out = (keymap_power_mode_t)v;
  return true;
}
bool keymap_voice_mode_parse(const char *s, keymap_voice_mode_t *out) {
  int v = 0;
  if (!parse_mode(s, kVoiceNames, MAP_VOICE_COUNT, &v)) return false;
  *out = (keymap_voice_mode_t)v;
  return true;
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------
static hid_action_t none_action(void) {
  hid_action_t a;
  a.kind = HID_ACT_NONE;
  a.modifier = HID_MOD_NONE;
  a.keycode = HID_KEY_NONE;
  a.consumer = HID_CONSUMER_NONE;
  return a;
}

// ---------------------------------------------------------------------------
// Programmable bindings: one raw code -> one action, set from the Web UI or
// the `bind` console command. Memory-only here; persistence lives in the
// settings module (NVS), which replays the saved records into
// keymap_set_binding() at boot. Lookups scan this table first - an explicit
// user binding outranks both the runtime modes and the default table.
// ---------------------------------------------------------------------------
struct BindingRecord {
  uint8_t      raw_code;
  hid_action_t press;
};

static BindingRecord s_bindings[KEYMAP_MAX_BINDINGS];
static size_t s_bindingCount = 0;

bool keymap_set_binding(uint8_t raw_code, uint8_t kind, uint8_t modifier, uint8_t keycode,
                        uint16_t consumer) {
  if (raw_code == 0x00) return false;  // the all-zero frame is "no key", never a code

  // Replace an existing binding for this code in place.
  for (size_t i = 0; i < s_bindingCount; i++) {
    if (s_bindings[i].raw_code != raw_code) continue;
    if (kind == KEYMAP_BIND_KIND_NONE) {
      s_bindings[i] = s_bindings[s_bindingCount - 1];
      s_bindingCount--;
      return true;
    }
    s_bindings[i].press.kind = (kind == KEYMAP_BIND_KIND_CONS) ? HID_ACT_CONSUMER : HID_ACT_KEYBOARD;
    s_bindings[i].press.modifier = modifier;
    s_bindings[i].press.keycode = keycode;
    s_bindings[i].press.consumer = consumer;
    return true;
  }

  if (kind == KEYMAP_BIND_KIND_NONE) return true;  // clearing an absent binding: done
  if (s_bindingCount >= KEYMAP_MAX_BINDINGS) return false;

  BindingRecord &r = s_bindings[s_bindingCount++];
  r.raw_code = raw_code;
  r.press.kind = (kind == KEYMAP_BIND_KIND_CONS) ? HID_ACT_CONSUMER : HID_ACT_KEYBOARD;
  r.press.modifier = modifier;
  r.press.keycode = keycode;
  r.press.consumer = consumer;
  return true;
}

bool keymap_has_binding(uint8_t raw_code) {
  for (size_t i = 0; i < s_bindingCount; i++) {
    if (s_bindings[i].raw_code == raw_code) return true;
  }
  return false;
}

size_t keymap_binding_count(void) { return s_bindingCount; }

size_t keymap_get_bindings(uint8_t *raw_out, hid_action_t *actions_out, size_t max_out) {
  const size_t n = (s_bindingCount < max_out) ? s_bindingCount : max_out;
  for (size_t i = 0; i < n; i++) {
    raw_out[i] = s_bindings[i].raw_code;
    actions_out[i] = s_bindings[i].press;
  }
  return n;
}

// Resolve the three runtime-selectable keys.
static hid_action_t resolve_dynamic(uint8_t raw_code) {  hid_action_t a = none_action();

  if (raw_code == MI_KEY_BACK) {
    switch (s_back_mode) {
      case MAP_BACK_CONSUMER_BACK:
        a.kind = HID_ACT_CONSUMER;
        a.consumer = HID_CONSUMER_AC_BACK;
        break;
      case MAP_BACK_KEYBOARD_ESC:
        a.kind = HID_ACT_KEYBOARD;
        a.keycode = HID_KEY_ESC;
        break;
      case MAP_BACK_KEYBOARD_ALT_LEFT:
        a.kind = HID_ACT_KEYBOARD;
        a.modifier = HID_MOD_LALT;
        a.keycode = HID_KEY_LEFT;
        break;
      default:
        break;
    }
    return a;
  }

  if (raw_code == MI_KEY_POWER || raw_code == MI_KEY_POWER_ALT) {
    switch (s_power_mode) {
      case MAP_POWER_ALT_F4:
        a.kind = HID_ACT_KEYBOARD;
        a.modifier = HID_MOD_LALT;
        a.keycode = HID_KEY_F4;
        break;
      case MAP_POWER_CONSUMER_SLEEP:
        a.kind = HID_ACT_CONSUMER;
        a.consumer = HID_CONSUMER_SLEEP;
        break;
      case MAP_POWER_CONSUMER_POWER:
        a.kind = HID_ACT_CONSUMER;
        a.consumer = HID_CONSUMER_POWER;
        break;
      case MAP_POWER_KEYBOARD_ESC:
        a.kind = HID_ACT_KEYBOARD;
        a.keycode = HID_KEY_ESC;
        break;
      default:
        break;
    }
    return a;
  }

  if (raw_code == MI_KEY_VOICE || raw_code == MI_KEY_VOICE_ALT) {
    switch (s_voice_mode) {
      case MAP_VOICE_RALT_COMMA:
        a.kind = HID_ACT_KEYBOARD;
        a.modifier = HID_MOD_RALT;
        a.keycode = HID_KEY_COMMA;
        break;
      case MAP_VOICE_CONSUMER_MUTE:
        a.kind = HID_ACT_CONSUMER;
        a.consumer = HID_CONSUMER_MUTE;
        break;
      case MAP_VOICE_KB_LCTRL_LGUI:
        // A chord of two modifiers with no base key. The host sees Left Ctrl
        // and Left Win go down together, which is a valid HID report and a
        // useful prefix for tools that bind their own Ctrl+Win+<key> hotkeys.
        a.kind = HID_ACT_KEYBOARD;
        a.modifier = HID_MOD_LCTRL | HID_MOD_LGUI;
        a.keycode = HID_KEY_NONE;
        break;
      case MAP_VOICE_DISABLED:
      default:
        break;
    }
    return a;
  }

  return a;
}

static const keymap_entry_t *find_entry(const keymap_entry_t *table, size_t count, uint8_t raw_code) {
  for (size_t i = 0; i < count; i++) {
    if (table[i].raw_code == raw_code) return &table[i];
  }
  return NULL;
}

// The keys whose target is chosen at runtime. resolve_dynamic() owns them
// completely, including the "deliberately unmapped" answer.
static bool is_dynamic_key(uint8_t raw_code) {
  return raw_code == MI_KEY_BACK || raw_code == MI_KEY_POWER || raw_code == MI_KEY_POWER_ALT ||
         raw_code == MI_KEY_VOICE || raw_code == MI_KEY_VOICE_ALT;
}

hid_action_t keymap_lookup_ex(const keymap_entry_t *table, size_t count, uint8_t raw_code) {
  // Explicit user bindings (Web UI / `bind` command) win over everything: they
  // are the user's most deliberate statement about what a key should do.
  for (size_t i = 0; i < s_bindingCount; i++) {
    if (s_bindings[i].raw_code == raw_code) return s_bindings[i].press;
  }

  // Runtime-selectable keys must NOT fall through to the static table when the
  // runtime answer is NONE: that NONE means "the user turned this button off"
  // (e.g. `map voice disabled`), and falling through would silently ignore the
  // setting because the same code also has a default row in the table.
  if (is_dynamic_key(raw_code)) {
    return resolve_dynamic(raw_code);
  }

  if (!table || count == 0) {
    table = kDefaultTable;
    count = DEFAULT_TABLE_COUNT;
  }

  const keymap_entry_t *e = find_entry(table, count, raw_code);

  // Synonym aliases: a variant code maps to the same action as its canonical key.
  if (!e) {
    uint8_t canonical = 0;
    switch (raw_code) {
      case MI_KEY_POWER_ALT: canonical = MI_KEY_POWER; break;
      case MI_KEY_HOME_ALT:  canonical = MI_KEY_HOME;  break;
      case MI_KEY_MENU_ALT:  canonical = MI_KEY_MENU;  break;
      case MI_KEY_TV_ALT:    canonical = MI_KEY_TV;    break;
      default: break;
    }
    if (canonical) e = find_entry(table, count, canonical);
  }

  return e ? e->press : none_action();
}

hid_action_t keymap_lookup(uint8_t raw_code) {
  return keymap_lookup_ex(NULL, 0, raw_code);
}

bool keymap_is_known(uint8_t raw_code) {
  if (raw_code == 0) return false;
  if (keymap_lookup(raw_code).kind != HID_ACT_NONE) return true;
  // Known but intentionally unmapped (e.g. voice with voice mode disabled).
  switch (raw_code) {
    case MI_KEY_VOICE:
    case MI_KEY_VOICE_ALT:
      return true;
    default:
      return false;
  }
}

const char *keymap_raw_name(uint8_t raw_code) {
  switch (raw_code) {
    case MI_KEY_VOL_UP:    return "VOL_UP";
    case MI_KEY_VOL_DOWN:  return "VOL_DOWN";
    case MI_KEY_BACK:      return "BACK";
    case MI_KEY_POWER:     return "POWER";
    case MI_KEY_HOME:      return "HOME";
    case MI_KEY_MENU:      return "MENU";
    case MI_KEY_TV:        return "TV";
    case MI_KEY_UP:        return "UP";
    case MI_KEY_DOWN:      return "DOWN";
    case MI_KEY_LEFT:      return "LEFT";
    case MI_KEY_RIGHT:     return "RIGHT";
    case MI_KEY_OK:        return "OK";
    case MI_KEY_VOICE:     return "VOICE";
    case MI_KEY_POWER_ALT: return "POWER_ALT";
    case MI_KEY_HOME_ALT:  return "HOME_ALT";
    case MI_KEY_MENU_ALT:  return "MENU_ALT";
    case MI_KEY_TV_ALT:    return "TV_ALT";
    case MI_KEY_VOICE_ALT: return "VOICE_ALT";
    default:               return "UNKNOWN";
  }
}

void keymap_describe(const hid_action_t *action, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  if (!action) {
    snprintf(buf, buf_len, "NONE");
    return;
  }
  switch (action->kind) {
    case HID_ACT_KEYBOARD:
      if (action->modifier) {
        snprintf(buf, buf_len, "KB mod=0x%02X key=0x%02X", action->modifier, action->keycode);
      } else {
        snprintf(buf, buf_len, "KB key=0x%02X", action->keycode);
      }
      break;
    case HID_ACT_CONSUMER:
      snprintf(buf, buf_len, "CONSUMER 0x%04X", action->consumer);
      break;
    case HID_ACT_NONE:
    default:
      snprintf(buf, buf_len, "NONE");
      break;
  }
}
