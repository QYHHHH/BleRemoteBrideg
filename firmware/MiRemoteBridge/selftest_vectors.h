/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * selftest_vectors.h - AUTO-GENERATED, DO NOT EDIT BY HAND.
 *
 * Source : tests/vectors/key_vectors.json
 * Generator: tests/tools/gen_vectors.py
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t code;
  bool pressed;
} st_key_expect_t;

typedef struct {
  uint8_t kind;      /* 0 none, 1 keyboard, 2 consumer */
  uint8_t modifier;
  uint8_t keycode;
  uint16_t consumer;
} st_action_expect_t;

typedef struct {
  const char *name;
  const uint8_t *bytes;
  uint16_t len;
  uint16_t expect_count;
  st_key_expect_t expect[8];
} st_parse_vec_t;

typedef struct {
  const char *name;
  const uint8_t *input_codes;
  const uint8_t *input_pressed;
  uint16_t input_count;
  uint16_t expect_count;
  st_key_expect_t expect[8];
} st_tracker_vec_t;

typedef struct {
  uint8_t code;
  const char *name;
  st_action_expect_t expect;
} st_keymap_vec_t;

/* mode axis: 0 = back, 1 = power, 2 = voice; mode_index indexes the
 * corresponding keymap_*_mode_t enum. */
typedef struct {
  uint8_t axis;
  uint8_t mode_index;
  uint8_t code;
  st_action_expect_t expect;
} st_mode_vec_t;

static const uint8_t kParseBytes_0[] = {0, 0, 40, 0, 0, 0, 0, 0};
static const uint8_t kParseBytes_1[] = {0, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kParseBytes_2[] = {0, 0, 192, 0, 0, 0, 0, 0};
static const uint8_t kParseBytes_3[] = {128};
static const uint8_t kParseBytes_4[] = {0};
static const uint8_t kParseBytes_5[] = {102};
static const uint8_t kParseBytes_6[] = {0, 128};
static const uint8_t kParseBytes_7[] = {82, 0};
static const uint8_t kParseBytes_8[] = {0, 0, 241};
static const uint8_t kParseBytes_9[] = {0, 0, 0};
static const uint8_t kParseBytes_10[] = {0, 0, 0, 79, 0};
static const uint8_t kParseBytes_11[] = {0, 64, 0};
static const uint8_t kParseBytes_12[] = {2, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t kParseBytes_13[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
static const uint8_t kParseBytes_14[] = {0};
static const uint8_t kParseBytes_15[] = {79};

static const st_parse_vec_t kParseVectors[] = {
  { "8-byte report, OK key in slot 0", kParseBytes_0, 8, 1, { { 40, true }, { 0, false } } },
  { "8-byte report, all slots zero means release", kParseBytes_1, 8, 1, { { 0, false }, { 0, false } } },
  { "8-byte report, TV key (0xC0)", kParseBytes_2, 8, 1, { { 192, true }, { 0, false } } },
  { "1-byte report, Volume Up", kParseBytes_3, 1, 1, { { 128, true }, { 0, false } } },
  { "1-byte report, zero means release", kParseBytes_4, 1, 1, { { 0, false }, { 0, false } } },
  { "1-byte report, Power (0x66)", kParseBytes_5, 1, 1, { { 102, true }, { 0, false } } },
  { "2-byte report, key in byte 1", kParseBytes_6, 2, 1, { { 128, true }, { 0, false } } },
  { "2-byte report, key in byte 0 fallback", kParseBytes_7, 2, 1, { { 82, true }, { 0, false } } },
  { "3-byte report, Back key at index 2", kParseBytes_8, 3, 1, { { 241, true }, { 0, false } } },
  { "3-byte report, all zero is release", kParseBytes_9, 3, 1, { { 0, false }, { 0, false } } },
  { "5-byte report, key in slot 1 of the slot region", kParseBytes_10, 5, 1, { { 79, true }, { 0, false } } },
  { "modifier-only report is not mistaken for a key", kParseBytes_11, 3, 1, { { 0, false }, { 0, false } } },
  { "modifier-only report, modifier in byte 0", kParseBytes_12, 8, 1, { { 0, false }, { 0, false } } },
  { "audio-sized payload is not a key report", kParseBytes_13, 20, 0, { { 0, false } } },
  { "empty payload", kParseBytes_14, 0, 0, { { 0, false } } },
  { "D-pad Right (0x4F) single byte", kParseBytes_15, 1, 1, { { 79, true }, { 0, false } } },
};
static const size_t kParseVectorCount = 16;

static const uint8_t kAtvvBytes_0[] = {4, 3, 0, 0};
static const uint8_t kAtvvBytes_1[] = {0};
static const uint8_t kAtvvBytes_2[] = {8};
static const uint8_t kAtvvBytes_3[] = {11, 0, 1, 0, 0, 0, 120};

static const st_parse_vec_t kAtvvVectors[] = {
  { "AUDIO_START with HTT reason 0x03 is the voice button down", kAtvvBytes_0, 4, 1, { { 4, true }, { 0, false } } },
  { "AUDIO_STOP is the voice button up", kAtvvBytes_1, 1, 1, { { 4, false }, { 0, false } } },
  { "MIC_CLOSED is the voice button up", kAtvvBytes_2, 1, 1, { { 4, false }, { 0, false } } },
  { "CAPS response is not a key event", kAtvvBytes_3, 7, 0, { { 0, false } } },
};
static const size_t kAtvvVectorCount = 4;

static const uint8_t kTrackerCodes_0[] = {128};
static const uint8_t kTrackerPressed_0[] = {1};
static const uint8_t kTrackerCodes_1[] = {128, 128};
static const uint8_t kTrackerPressed_1[] = {1, 1};
static const uint8_t kTrackerCodes_2[] = {128, 0};
static const uint8_t kTrackerPressed_2[] = {1, 0};
static const uint8_t kTrackerCodes_3[] = {128, 0, 0};
static const uint8_t kTrackerPressed_3[] = {1, 0, 0};
static const uint8_t kTrackerCodes_4[] = {0};
static const uint8_t kTrackerPressed_4[] = {0};
static const uint8_t kTrackerCodes_5[] = {82, 79};
static const uint8_t kTrackerPressed_5[] = {1, 1};
static const uint8_t kTrackerCodes_6[] = {82, 79, 0};
static const uint8_t kTrackerPressed_6[] = {1, 1, 0};
static const uint8_t kTrackerCodes_7[] = {0};
static const uint8_t kTrackerPressed_7[] = {1};

static const st_tracker_vec_t kTrackerVectors[] = {
  { "single press", kTrackerCodes_0, kTrackerPressed_0, 1, 1, { { 128, true }, { 0, false } } },
  { "duplicate press while held is swallowed", kTrackerCodes_1, kTrackerPressed_1, 2, 1, { { 128, true }, { 0, false } } },
  { "press then release", kTrackerCodes_2, kTrackerPressed_2, 2, 2, { { 128, true }, { 128, false }, { 0, false } } },
  { "duplicate release is swallowed", kTrackerCodes_3, kTrackerPressed_3, 3, 2, { { 128, true }, { 128, false }, { 0, false } } },
  { "release with nothing held emits nothing", kTrackerCodes_4, kTrackerPressed_4, 1, 0, { { 0, false } } },
  { "new key without a release auto-releases the old one", kTrackerCodes_5, kTrackerPressed_5, 2, 3, { { 82, true }, { 82, false }, { 79, true }, { 0, false } } },
  { "release after an auto-release pair", kTrackerCodes_6, kTrackerPressed_6, 3, 4, { { 82, true }, { 82, false }, { 79, true }, { 79, false }, { 0, false } } },
  { "zero press event is ignored", kTrackerCodes_7, kTrackerPressed_7, 1, 0, { { 0, false } } },
};
static const size_t kTrackerVectorCount = 8;

static const st_keymap_vec_t kKeymapVectors[] = {
  { 128, "VOL_UP", { 2, 0, 0, 233 } },
  { 129, "VOL_DOWN", { 2, 0, 0, 234 } },
  { 82, "UP", { 1, 0, 82, 0 } },
  { 81, "DOWN", { 1, 0, 81, 0 } },
  { 80, "LEFT", { 1, 0, 80, 0 } },
  { 79, "RIGHT", { 1, 0, 79, 0 } },
  { 40, "OK", { 1, 0, 40, 0 } },
  { 36, "HOME", { 1, 8, 7, 0 } },
  { 93, "MENU", { 1, 0, 44, 0 } },
  { 192, "TV", { 1, 0, 65, 0 } },
  { 241, "BACK", { 2, 0, 0, 548 } },
  { 102, "POWER", { 1, 4, 61, 0 } },
  { 4, "VOICE", { 1, 64, 54, 0 } },
  { 62, "VOICE_ALT", { 1, 64, 54, 0 } },
  { 74, "HOME_ALT", { 1, 8, 7, 0 } },
  { 101, "MENU_ALT", { 1, 0, 44, 0 } },
  { 53, "TV_ALT", { 1, 0, 65, 0 } },
  { 255, "POWER_ALT", { 1, 4, 61, 0 } },
  { 233, "UNKNOWN_0xE9", { 0, 0, 0, 0 } },
  { 1, "UNKNOWN_0x01", { 0, 0, 0, 0 } },
};
static const size_t kKeymapVectorCount = 20;

static const st_mode_vec_t kModeVectors[] = {
  { 0, 1, 241, { 1, 0, 41, 0 } },
  { 0, 2, 241, { 1, 4, 80, 0 } },
  { 0, 0, 241, { 2, 0, 0, 548 } },
  { 2, 2, 4, { 0, 0, 0, 0 } },
  { 2, 1, 4, { 2, 0, 0, 226 } },
  { 1, 1, 102, { 2, 0, 0, 50 } },
  { 1, 2, 102, { 2, 0, 0, 48 } },
  { 1, 3, 102, { 1, 0, 41, 0 } },
};
static const size_t kModeVectorCount = 8;

static const uint8_t kAllKeyCodes[] = {128, 129, 241, 102, 36, 93, 192, 82, 81, 80, 79, 40, 4};
static const size_t kAllKeyCodeCount = 13;

static const uint32_t kFuzzIterations = 4000;
static const uint32_t kFuzzSeed = 20260910U;

