/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * selftest.cpp - on-device verification without any peer device
 *
 * What this covers, and what it does not:
 *
 *   covered   - report parsing, the press/release tracker, the whole key map
 *               including the synonym codes and runtime mode overrides, the
 *               event queue, and the parse -> queue -> bridge -> HID dispatch
 *               path (the HID notifications simply have no subscriber yet).
 *   not       - anything that needs the real RC003 or Windows over the air:
 *               scanning, pairing, bonding, GATT discovery, subscription,
 *               actual HID delivery and the radio timing. Those are Stage 4/5
 *               hardware acceptance items and are listed in docs/TESTING.md.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "selftest.h"

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

#include "bridge.h"
#include "config.h"
#include "event_bus.h"
#include "event_queue.h"
#include "hid_report_map.h"
#include "key_definitions.h"
#include "keymap.h"
#include "log.h"
#include "rc003_report.h"
#include "selftest_vectors.h"

namespace {

static const char *kTag = "TEST";

int s_pass = 0;
int s_fail = 0;

// The console logger is rate limited so that no component can stall the loop
// by flooding the UART. A failing selftest is exactly such a flood, and the
// totals at the end are the part that must never be dropped - so only the
// first few failures are printed in full and the rest are only counted.
constexpr int kMaxFailDetails = 8;

void ok(bool condition, const char *what) {
  if (condition) {
    s_pass++;
    return;
  }
  s_fail++;
  if (s_fail <= kMaxFailDetails) {
    brlog::always(kTag, "FAIL: %s", what);
  }
}

void okf(bool condition, const char *fmt, ...) {
  if (condition) {
    s_pass++;
    return;
  }
  s_fail++;
  if (s_fail > kMaxFailDetails) return;
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  brlog::always(kTag, "FAIL: %s", buf);
}

void reportSuppressed() {
  if (s_fail > kMaxFailDetails) {
    brlog::always(kTag, "... %d further failure(s) not printed individually", s_fail - kMaxFailDetails);
  }
}

bool sameAction(const hid_action_t &a, const st_action_expect_t &e) {
  return (uint8_t)a.kind == e.kind && a.modifier == e.modifier && a.keycode == e.keycode && a.consumer == e.consumer;
}

// ---------------------------------------------------------------------------
// Vector suites
// ---------------------------------------------------------------------------
void suiteParse() {
  for (size_t i = 0; i < kParseVectorCount; i++) {
    const st_parse_vec_t &v = kParseVectors[i];
    rc003_key_event_t out[2];
    const size_t n = rc003_parse_hid_report(v.bytes, v.len, out, 2);
    okf(n == v.expect_count, "parse[%s] count %u != %u", v.name, (unsigned)n, (unsigned)v.expect_count);
    if (n != v.expect_count) continue;
    for (size_t k = 0; k < n; k++) {
      okf(out[k].raw_code == v.expect[k].code, "parse[%s] code 0x%02X != 0x%02X", v.name, out[k].raw_code,
          v.expect[k].code);
      okf(out[k].pressed == v.expect[k].pressed, "parse[%s] pressed %d != %d", v.name, (int)out[k].pressed,
          (int)v.expect[k].pressed);
    }
  }
}

void suiteAtvv() {
  for (size_t i = 0; i < kAtvvVectorCount; i++) {
    const st_parse_vec_t &v = kAtvvVectors[i];
    rc003_key_event_t out[2];
    const size_t n = rc003_parse_atvv_ctl(v.bytes, v.len, out, 2);
    okf(n == v.expect_count, "atvv[%s] count %u != %u", v.name, (unsigned)n, (unsigned)v.expect_count);
    if (n != v.expect_count) continue;
    for (size_t k = 0; k < n; k++) {
      okf(out[k].raw_code == v.expect[k].code, "atvv[%s] code 0x%02X != 0x%02X", v.name, out[k].raw_code,
          v.expect[k].code);
      okf(out[k].pressed == v.expect[k].pressed, "atvv[%s] pressed mismatch", v.name);
    }
  }
}

void suiteTracker() {
  for (size_t i = 0; i < kTrackerVectorCount; i++) {
    const st_tracker_vec_t &v = kTrackerVectors[i];
    rc003_tracker_t t;
    rc003_tracker_reset(&t);

    rc003_key_event_t got[16];
    size_t gotCount = 0;
    for (size_t step = 0; step < v.input_count; step++) {
      rc003_key_event_t in;
      in.raw_code = v.input_codes[step];
      in.pressed = (v.input_pressed[step] != 0);
      in.report_mod = 0;
      in.frame_kind = RC003_FRAME_HID_REPORT;
      gotCount += rc003_tracker_apply(&t, &in, &got[gotCount], 16 - gotCount);
    }

    okf(gotCount == v.expect_count, "tracker[%s] count %u != %u", v.name, (unsigned)gotCount,
        (unsigned)v.expect_count);
    if (gotCount != v.expect_count) continue;
    for (size_t k = 0; k < gotCount; k++) {
      okf(got[k].raw_code == v.expect[k].code, "tracker[%s] code 0x%02X != 0x%02X", v.name, got[k].raw_code,
          v.expect[k].code);
      okf(got[k].pressed == v.expect[k].pressed, "tracker[%s] pressed mismatch at %u", v.name, (unsigned)k);
    }
  }
}

void suiteKeymap() {
  for (size_t i = 0; i < kKeymapVectorCount; i++) {
    const st_keymap_vec_t &v = kKeymapVectors[i];
    const hid_action_t a = keymap_lookup(v.code);
    okf(sameAction(a, v.expect), "keymap[%s] 0x%02X: kind=%u mod=0x%02X key=0x%02X cons=0x%04X (want kind=%u mod=0x%02X key=0x%02X cons=0x%04X)",
        v.name, v.code, (unsigned)a.kind, a.modifier, a.keycode, a.consumer, (unsigned)v.expect.kind, v.expect.modifier,
        v.expect.keycode, v.expect.consumer);
  }
}

void suiteModes() {
  // Remember whatever the user configured and put it back afterwards.
  const keymap_back_mode_t savedBack = keymap_get_back_mode();
  const keymap_power_mode_t savedPower = keymap_get_power_mode();
  const keymap_voice_mode_t savedVoice = keymap_get_voice_mode();

  for (size_t i = 0; i < kModeVectorCount; i++) {
    const st_mode_vec_t &v = kModeVectors[i];
    if (v.axis == 0) {
      keymap_set_back_mode((keymap_back_mode_t)v.mode_index);
    } else if (v.axis == 1) {
      keymap_set_power_mode((keymap_power_mode_t)v.mode_index);
    } else {
      keymap_set_voice_mode((keymap_voice_mode_t)v.mode_index);
    }
    const hid_action_t a = keymap_lookup(v.code);
    okf(sameAction(a, v.expect), "mode axis=%u idx=%u code=0x%02X mismatch", (unsigned)v.axis,
        (unsigned)v.mode_index, v.code);
  }

  keymap_set_back_mode(savedBack);
  keymap_set_power_mode(savedPower);
  keymap_set_voice_mode(savedVoice);
}

void suiteQueue() {
  SpscRing<int, 8> q;
  int v = 0;

  ok(q.empty(), "fresh queue should be empty");
  ok(q.capacity() == 7, "queue capacity should be N-1");

  for (int i = 0; i < 7; i++) {
    okf(q.push(i), "push %d should succeed", i);
  }
  okf(!q.push(99), "push into a full queue must fail");
  okf(q.dropped() == 1, "full push must be counted as a drop, got %lu", (unsigned long)q.dropped());

  bool orderOk = true;
  for (int i = 0; i < 7; i++) {
    if (!q.pop(v) || v != i) orderOk = false;
  }
  ok(orderOk, "queue must preserve FIFO order");
  ok(q.empty(), "queue should be empty after draining");
  ok(!q.pop(v), "pop on an empty queue must fail");

  // Wrap-around: interleave pushes and pops past the end of the buffer.
  bool wrapOk = true;
  for (int round = 0; round < 5; round++) {
    for (int i = 0; i < 5; i++) {
      if (!q.push(round * 100 + i)) wrapOk = false;
    }
    for (int i = 0; i < 5; i++) {
      if (!q.pop(v) || v != round * 100 + i) wrapOk = false;
    }
  }
  ok(wrapOk, "queue must behave correctly across the ring wrap-around");
}

void suiteDescriptorConstants() {
  ok(HID_KB_REPORT_LEN == 8, "keyboard report must be 8 bytes");
  ok(HID_CONSUMER_REPORT_LEN == 2, "consumer report must be 2 bytes");
  ok(HID_KB_OFFSET_KEYS + HID_KB_KEY_COUNT == HID_KB_REPORT_LEN,
     "the 6 key slots must exactly fill the rest of the keyboard report");
  ok(HID_REPORT_ID_KEYBOARD != HID_REPORT_ID_CONSUMER, "the two reports must use different report IDs");
  ok(kHidReportMap[0] == 0x05 && kHidReportMap[1] == 0x01, "report map must start with Usage Page (Generic Desktop)");
  ok(kHidReportMap[HID_REPORT_MAP_LEN - 1] == 0xC0, "report map must end with End Collection");
  ok(HID_REPORT_MAP_LEN > 80 && HID_REPORT_MAP_LEN < 200, "report map size is implausible");
}

// ---------------------------------------------------------------------------
// Dispatch simulation: parse -> tracker -> event queue -> bridge -> HID
// ---------------------------------------------------------------------------
rc003_tracker_t s_simTracker;

void simFeedReport(const uint8_t *bytes, size_t len) {
  rc003_key_event_t raw[2];
  const size_t n = rc003_parse_hid_report(bytes, len, raw, 2);
  rc003_key_event_t norm[3];
  for (size_t i = 0; i < n; i++) {
    const size_t m = rc003_tracker_apply(&s_simTracker, &raw[i], norm, 3);
    for (size_t k = 0; k < m; k++) {
      event_bus::post(BR_EV_RC_KEY, norm[k].raw_code, norm[k].pressed);
    }
  }
  bridge::loop();
}

// A conventional 8-byte report with the key in slot 0 is what the RC003 uses
// for most of its buttons; the 1-byte form is covered by the vector suite.
void simPress(uint8_t code) {
  const uint8_t report[8] = {0, 0, code, 0, 0, 0, 0, 0};
  simFeedReport(report, sizeof(report));
}

void simRelease() {
  const uint8_t report[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  simFeedReport(report, sizeof(report));
}

void simDisconnect() {
  event_bus::post(BR_EV_RC_LINK_DOWN);
  bridge::loop();
}

void simHostDisconnect() {
  event_bus::post(BR_EV_WIN_LINK_DOWN);
  bridge::loop();
}

}  // namespace

namespace selftest {

int runVectors() {
  s_pass = 0;
  s_fail = 0;

  brlog::always(kTag, "--- vector suite ---");
  suiteParse();
  suiteAtvv();
  suiteTracker();
  suiteKeymap();
  suiteModes();
  suiteQueue();
  suiteDescriptorConstants();

  reportSuppressed();
  brlog::always(kTag, "vector suite: %d passed, %d failed", s_pass, s_fail);
  return s_fail;
}

int runSimulation() {
  s_pass = 0;
  s_fail = 0;

  brlog::always(kTag, "--- dispatch simulation ---");
  rc003_tracker_reset(&s_simTracker);

  // 1. simple press/release
  simPress(MI_KEY_VOL_UP);
  ok(bridge::activeRawCode() == MI_KEY_VOL_UP, "press VOL_UP should leave it active");
  simRelease();
  ok(bridge::activeRawCode() == 0, "release should clear the active key");

  // 2. duplicated press reports must not produce a second keystroke
  const uint32_t before = bridge::reportsSent();
  simPress(MI_KEY_OK);
  simPress(MI_KEY_OK);
  simPress(MI_KEY_OK);
  const uint32_t afterPresses = bridge::reportsSent();
  okf(afterPresses - before == 1, "three identical press reports must yield exactly one report, got %lu",
      (unsigned long)(afterPresses - before));
  simRelease();
  simRelease();
  const uint32_t afterReleases = bridge::reportsSent();
  okf(afterReleases - afterPresses == 1, "duplicated release reports must yield exactly one report, got %lu",
      (unsigned long)(afterReleases - afterPresses));
  ok(bridge::activeRawCode() == 0, "duplicated releases must leave nothing active");

  // 3. a new key down before the previous one is released
  simPress(MI_KEY_UP);
  ok(bridge::activeRawCode() == MI_KEY_UP, "UP should be active");
  simPress(MI_KEY_RIGHT);
  ok(bridge::activeRawCode() == MI_KEY_RIGHT, "RIGHT should have taken over from UP");
  simRelease();
  ok(bridge::activeRawCode() == 0, "release after takeover should clear everything");

  // 4. every physical key, press and release
  for (size_t i = 0; i < kAllKeyCodeCount; i++) {
    const uint8_t code = kAllKeyCodes[i];
    simPress(code);
    okf(bridge::activeRawCode() == code, "key 0x%02X should become active after press", code);
    simRelease();
    okf(bridge::activeRawCode() == 0, "key 0x%02X should be released", code);
  }

  // 5. long hold: keep pressing, then release
  simPress(MI_KEY_VOL_DOWN);
  for (int i = 0; i < 20; i++) simPress(MI_KEY_VOL_DOWN);
  ok(bridge::activeRawCode() == MI_KEY_VOL_DOWN, "long hold should keep the key active");
  simRelease();
  ok(bridge::activeRawCode() == 0, "long hold must release cleanly");

  // 6. rapid alternating keys
  for (int i = 0; i < 25; i++) {
    simPress((i % 2) ? MI_KEY_LEFT : MI_KEY_RIGHT);
    simRelease();
  }
  ok(bridge::activeRawCode() == 0, "rapid alternating keys must end clean");

  // 7. disconnect of either side clears everything
  simPress(MI_KEY_BACK);
  ok(bridge::activeRawCode() == MI_KEY_BACK, "BACK should be active before the disconnect test");
  simDisconnect();
  ok(bridge::activeRawCode() == 0, "an RC003 disconnect must clear the active key");

  simPress(MI_KEY_MENU);
  simHostDisconnect();
  ok(bridge::activeRawCode() == 0, "a host disconnect must clear the active key");

  // 8. both sides gone at once
  simPress(MI_KEY_TV);
  simDisconnect();
  simHostDisconnect();
  ok(bridge::activeRawCode() == 0, "both sides disconnecting must still clear the active key");

  // 9. voice button straight after a fresh tracker
  rc003_tracker_reset(&s_simTracker);
  const uint8_t atvvStart[4] = {0x04, 0x03, 0x00, 0x00};
  rc003_key_event_t raw[1];
  const size_t n = rc003_parse_atvv_ctl(atvvStart, sizeof(atvvStart), raw, 1);
  ok(n == 1 && raw[0].raw_code == MI_KEY_VOICE && raw[0].pressed, "ATVV AUDIO_START must parse as a voice press");
  if (n == 1) {
    rc003_key_event_t norm[3];
    const size_t m = rc003_tracker_apply(&s_simTracker, &raw[0], norm, 3);
    for (size_t k = 0; k < m; k++) event_bus::post(BR_EV_RC_KEY, norm[k].raw_code, norm[k].pressed);
    bridge::loop();
  }
  ok(bridge::activeRawCode() == MI_KEY_VOICE, "voice button should be active after ATVV press");
  const uint8_t atvvStop[1] = {0x00};
  if (rc003_parse_atvv_ctl(atvvStop, sizeof(atvvStop), raw, 1) == 1) {
    rc003_key_event_t norm[3];
    const size_t m = rc003_tracker_apply(&s_simTracker, &raw[0], norm, 3);
    for (size_t k = 0; k < m; k++) event_bus::post(BR_EV_RC_KEY, norm[k].raw_code, norm[k].pressed);
    bridge::loop();
  }
  ok(bridge::activeRawCode() == 0, "voice button must release on ATVV AUDIO_STOP");

  reportSuppressed();
  brlog::always(kTag, "dispatch simulation: %d passed, %d failed", s_pass, s_fail);
  return s_fail;
}

int runAll() {
  const int a = runVectors();
  const int b = runSimulation();
  bridge::releaseAllKeys();
  brlog::always(kTag, "selftest total: %d failure(s)", a + b);
  if (a + b == 0) {
    brlog::always(kTag, "RESULT: PASS");
  } else {
    brlog::always(kTag, "RESULT: FAIL");
  }
  return a + b;
}

}  // namespace selftest
