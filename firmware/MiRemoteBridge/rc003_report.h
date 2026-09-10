/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * rc003_report.h - RC003 notification parsing (pure, no Arduino deps)
 *
 * The RC003 multiplexes several kinds of payload onto the same HID report
 * characteristic notification:
 *
 *   len 1      : single key slot
 *   len 2      : 2-byte frame, key in byte 1 (fallback byte 0)
 *   len 3..7   : key slots start at byte 2 (unusual but seen on ATVV-firmware)
 *   len 8      : classic HID keyboard report [modifier, reserved, k0..k5]
 *   len > 8    : audio (ADPCM) payload - never a key, and this bridge never
 *                subscribes to the audio characteristic anyway
 *
 * A notification whose key slots are all zero means "all keys released".
 *
 * This file deliberately has no Arduino/NimBLE dependency so the host-side
 * model tests can compile and exercise it directly.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Largest payload that is still considered a key report.
#define RC003_KEY_REPORT_MAX_LEN 8

typedef enum {
  RC003_FRAME_UNKNOWN = 0,
  RC003_FRAME_HID_REPORT,  // key report from the HOGP report characteristic
  RC003_FRAME_ATVV_CTL,    // ATVV control-channel opcode frame
  RC003_FRAME_AUDIO,       // audio payload, intentionally ignored
  RC003_FRAME_EMPTY
} rc003_frame_kind_t;

typedef struct {
  uint8_t             raw_code;    // MI_KEY_* ; 0 means "no key"
  bool                pressed;     // true = key down, false = key up
  uint8_t             report_mod;  // raw modifier byte, informational only
  rc003_frame_kind_t  frame_kind;
} rc003_key_event_t;

// Classify a payload without extracting a key.
rc003_frame_kind_t rc003_classify(const uint8_t *data, size_t len);

// Extract the key slot from a HID-style report.
// Returns 0 when all slots are zero (i.e. a release report).
uint8_t rc003_extract_key(const uint8_t *data, size_t len);

// Parse a notification from the HOGP report characteristic.
// Writes at most `max_out` events and returns how many were written.
size_t rc003_parse_hid_report(const uint8_t *data, size_t len,
                              rc003_key_event_t *out, size_t max_out);

// Parse a notification from the ATVV control characteristic.
// Only the voice-button opcodes are interpreted; everything else is ignored.
size_t rc003_parse_atvv_ctl(const uint8_t *data, size_t len,
                            rc003_key_event_t *out, size_t max_out);

// ---------------------------------------------------------------------------
// Press/release normalisation
// ---------------------------------------------------------------------------
// RC003 does not always send an explicit all-zero release before reporting the
// next key. This tiny tracker turns the raw stream into a strictly alternating
// press/release stream, which is what the HID layer needs, and guarantees the
// "no key can stay stuck" property even for malformed input sequences.
typedef struct {
  uint8_t  last_code;   // currently down key, 0 = none
  uint32_t press_count;
  uint32_t release_count;
  uint32_t duplicate_count;
} rc003_tracker_t;

void rc003_tracker_reset(rc003_tracker_t *t);

// Feed one raw event, get 0..2 normalised events in `out`.
size_t rc003_tracker_apply(rc003_tracker_t *t, const rc003_key_event_t *in,
                           rc003_key_event_t *out, size_t max_out);

// Produce the single "release everything" event if a key is still down.
size_t rc003_tracker_release_all(rc003_tracker_t *t,
                                 rc003_key_event_t *out, size_t max_out);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const char *rc003_frame_kind_name(rc003_frame_kind_t k);

// Render `data` as "AA BB CC" into `buf` (always NUL terminated).
void rc003_hex_dump(const uint8_t *data, size_t len, char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
