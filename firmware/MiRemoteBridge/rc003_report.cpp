/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * rc003_report.cpp - RC003 notification parsing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rc003_report.h"

#include <stdio.h>
#include <string.h>

#include "key_definitions.h"

// Byte index at which the key slots start in a HID-style report.
#define RC003_KEY_SLOT_OFFSET 2

rc003_frame_kind_t rc003_classify(const uint8_t *data, size_t len) {
  if (!data || len == 0) return RC003_FRAME_EMPTY;
  if (len > RC003_KEY_REPORT_MAX_LEN) return RC003_FRAME_AUDIO;
  return RC003_FRAME_HID_REPORT;
}

// A byte is only trusted as a key code if it is a code we actually know about.
// Without this guard a stray modifier byte (0x01 = LCTRL, 0x02 = LSHIFT) could
// be mistaken for a key and produce a permanently stuck modifier.
static bool is_plausible_key_code(uint8_t v) {
  switch (v) {
    case MI_KEY_VOL_UP:
    case MI_KEY_VOL_DOWN:
    case MI_KEY_BACK:
    case MI_KEY_POWER:
    case MI_KEY_POWER_ALT:
    case MI_KEY_HOME:
    case MI_KEY_HOME_ALT:
    case MI_KEY_MENU:
    case MI_KEY_MENU_ALT:
    case MI_KEY_TV:
    case MI_KEY_TV_ALT:
    case MI_KEY_UP:
    case MI_KEY_DOWN:
    case MI_KEY_LEFT:
    case MI_KEY_RIGHT:
    case MI_KEY_OK:
    case MI_KEY_VOICE:
    case MI_KEY_VOICE_ALT:
      return true;
    default:
      return false;
  }
}

uint8_t rc003_extract_key(const uint8_t *data, size_t len) {
  if (!data || len == 0) return 0;

  if (len == 1) {
    return data[0];
  }

  if (len == 2) {
    if (data[1] != 0) return data[1];
    if (data[0] != 0) return data[0];
    return 0;
  }

  // len >= 3: key slots live at byte 2..len-1.
  for (size_t i = RC003_KEY_SLOT_OFFSET; i < len; i++) {
    if (data[i] != 0) return data[i];
  }

  // All slots are zero. Some RC003 firmware revisions put the code in byte 0
  // instead. Accept that only when byte 0 really is a known key code, so a
  // modifier-only report is not misread as a keystroke.
  if (data[0] != 0 && is_plausible_key_code(data[0])) return data[0];

  // Byte 1 is rarely used but harmless to consider as a last resort.
  if (len >= 3 && data[1] != 0 && is_plausible_key_code(data[1])) return data[1];

  return 0;
}

size_t rc003_parse_hid_report(const uint8_t *data, size_t len,
                              rc003_key_event_t *out, size_t max_out) {
  if (!out || max_out == 0) return 0;

  rc003_frame_kind_t kind = rc003_classify(data, len);
  if (kind != RC003_FRAME_HID_REPORT) return 0;

  uint8_t key = rc003_extract_key(data, len);

  uint8_t mod = 0;
  if (len >= 2) mod = data[0];

  out[0].raw_code = key;
  out[0].pressed = (key != 0);
  out[0].report_mod = mod;
  out[0].frame_kind = RC003_FRAME_HID_REPORT;
  return 1;
}

size_t rc003_parse_atvv_ctl(const uint8_t *data, size_t len,
                            rc003_key_event_t *out, size_t max_out) {
  if (!data || len == 0 || !out || max_out == 0) return 0;

  uint8_t op = data[0];
  bool pressed = false;
  bool recognised = false;

  // AUDIO_START carrying an HTT reason byte: the user pressed the voice button.
  if (op == ATVV_OP_AUDIO_START && len >= 2 && data[1] == 0x03) {
    pressed = true;
    recognised = true;
  } else if (op == ATVV_OP_AUDIO_STOP || op == ATVV_OP_MIC_CLOSED) {
    pressed = false;
    recognised = true;
  }

  if (!recognised) return 0;

  out[0].raw_code = MI_KEY_VOICE;
  out[0].pressed = pressed;
  out[0].report_mod = 0;
  out[0].frame_kind = RC003_FRAME_ATVV_CTL;
  return 1;
}

// ---------------------------------------------------------------------------
// Tracker
// ---------------------------------------------------------------------------
void rc003_tracker_reset(rc003_tracker_t *t) {
  if (!t) return;
  t->last_code = 0;
}

size_t rc003_tracker_apply(rc003_tracker_t *t, const rc003_key_event_t *in,
                           rc003_key_event_t *out, size_t max_out) {
  if (!t || !in || !out || max_out == 0) return 0;

  size_t n = 0;

  if (in->pressed) {
    if (in->raw_code == 0) return 0;                 // nothing to do
    if (t->last_code == in->raw_code) {
      // Duplicate press report while the key is already held. Nothing is
      // emitted: this is what keeps a mistimed auto-repeat from turning into
      // a burst of keystrokes on the host.
      t->duplicate_count++;
      return 0;
    }
    if (t->last_code != 0 && n < max_out) {
      // A different key went down without an explicit release first.
      rc003_key_event_t rel = *in;
      rel.raw_code = t->last_code;
      rel.pressed = false;
      out[n++] = rel;
      t->release_count++;
    }
    if (n < max_out) {
      out[n++] = *in;
      t->last_code = in->raw_code;
      t->press_count++;
    }
    return n;
  }

  // Release report.
  if (t->last_code == 0) {
    t->duplicate_count++;
    return 0;  // already released, emit nothing
  }
  if (n < max_out) {
    rc003_key_event_t rel = *in;
    rel.raw_code = t->last_code;
    rel.pressed = false;
    out[n++] = rel;
    t->release_count++;
  }
  t->last_code = 0;
  return n;
}

size_t rc003_tracker_release_all(rc003_tracker_t *t,
                                 rc003_key_event_t *out, size_t max_out) {
  if (!t || !out || max_out == 0) return 0;
  if (t->last_code == 0) return 0;

  rc003_key_event_t rel;
  rel.raw_code = t->last_code;
  rel.pressed = false;
  rel.report_mod = 0;
  rel.frame_kind = RC003_FRAME_UNKNOWN;
  out[0] = rel;
  t->last_code = 0;
  t->release_count++;
  return 1;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const char *rc003_frame_kind_name(rc003_frame_kind_t k) {
  switch (k) {
    case RC003_FRAME_HID_REPORT: return "HID";
    case RC003_FRAME_ATVV_CTL:   return "ATVV_CTL";
    case RC003_FRAME_AUDIO:      return "AUDIO";
    case RC003_FRAME_EMPTY:      return "EMPTY";
    case RC003_FRAME_UNKNOWN:
    default:                     return "UNKNOWN";
  }
}

void rc003_hex_dump(const uint8_t *data, size_t len, char *buf, size_t buf_len) {
  if (!buf || buf_len == 0) return;
  buf[0] = '\0';
  if (!data) return;

  size_t pos = 0;
  for (size_t i = 0; i < len; i++) {
    int written = snprintf(buf + pos, buf_len - pos, (i + 1 < len) ? "%02X " : "%02X", data[i]);
    if (written < 0 || (size_t)written >= buf_len - pos) {
      buf[buf_len - 1] = '\0';
      return;
    }
    pos += (size_t)written;
  }
}
