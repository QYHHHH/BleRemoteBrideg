/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * bridge.cpp - event loop, key dispatch and state bookkeeping
 *
 * This is the only place where an RC003 event turns into a HID report, and it
 * always runs in the Arduino loop task - never inside a BLE callback.
 *
 * SPDX-License-Identifier: MIT
 */

#include "bridge.h"

#include <Arduino.h>
#include <string.h>

#include "ble_core.h"
#include "ble_bonds.h"
#include "config.h"
#include "event_bus.h"
#include "hid_server.h"
#include "key_definitions.h"
#include "keymap.h"
#include "log.h"
#include "rc003_client.h"
#include "settings.h"

namespace {

static const char *kTag = "BRIDGE";
static const char *kTagKey = "KEY";

// The RC003 reports one key at a time, so a single slot is enough to know what
// must be released. Keeping it explicit (rather than inferring it from the HID
// server state) makes the "auto release on a new key press" case trivial.
hid_action_t s_activeAction{};
uint8_t s_activeRaw = 0;
bool s_activeDown = false;

uint32_t s_eventsHandled = 0;
uint32_t s_reportsSent = 0;
uint32_t s_unknownKeys = 0;
uint32_t s_lastDropReportMs = 0;

#if BRIDGE_STATUS_LED_ENABLE
void ledWrite(bool on) {
#if BRIDGE_STATUS_LED_ACTIVE_LOW
  digitalWrite(BRIDGE_STATUS_LED_PIN, on ? LOW : HIGH);
#else
  digitalWrite(BRIDGE_STATUS_LED_PIN, on ? HIGH : LOW);
#endif
}
#endif

void ledInit() {
#if BRIDGE_STATUS_LED_ENABLE
  pinMode(BRIDGE_STATUS_LED_PIN, OUTPUT);
  ledWrite(false);
#endif
}

void ledUpdate() {
#if BRIDGE_STATUS_LED_ENABLE
  static uint32_t last = 0;
  static bool state = false;
  const uint32_t period = rc003_client::connected() ? 0 : 500;
  if (period == 0) {
    state = (hid_server::hostConnected());
    ledWrite(state);
    return;
  }
  if (millis() - last >= period) {
    last = millis();
    state = !state;
    ledWrite(state);
  }
#endif
}

// ---------------------------------------------------------------------------
// Key dispatch
// ---------------------------------------------------------------------------
void releaseActive(const char *why) {
  if (!s_activeDown) return;
  hid_server::releaseAction(s_activeAction);
  BR_LOGI(kTagKey, "release 0x%02X (%s) [%s]", s_activeRaw, keymap_raw_name(s_activeRaw), why ? why : "");
  s_activeDown = false;
  s_activeRaw = 0;
  memset(&s_activeAction, 0, sizeof(s_activeAction));
}

void handleKeyEvent(uint8_t rawCode, bool pressed, uint32_t tsUs) {
  if (pressed) {
    if (rawCode == 0) return;

    const hid_action_t action = keymap_lookup(rawCode);

    if (action.kind == HID_ACT_NONE) {
      if (keymap_is_known(rawCode)) {
        // Known button, deliberately unmapped (e.g. voice disabled).
        BR_LOGI(kTagKey, "0x%02X (%s) is mapped to NONE, ignored", rawCode, keymap_raw_name(rawCode));
      } else {
        s_unknownKeys++;
        BR_LOGW(kTagKey, "unknown key code 0x%02X ignored (add it to keymap.cpp if it is real)", rawCode);
#if BRIDGE_PASS_UNKNOWN_KEYS
        hid_action_t passthrough{};
        passthrough.kind = HID_ACT_KEYBOARD;
        passthrough.modifier = HID_MOD_NONE;
        passthrough.keycode = rawCode;
        releaseActive("passthrough");
        hid_server::pressAction(passthrough);
        s_activeAction = passthrough;
        s_activeRaw = rawCode;
        s_activeDown = true;
#endif
      }
      return;
    }

    // A second key going down before the first was released: release the old
    // one first so the host never sees an inconsistent state.
    releaseActive("new key");

    hid_server::pressAction(action);
    const uint32_t dt = (uint32_t)(micros() - tsUs);

    char desc[40];
    keymap_describe(&action, desc, sizeof(desc));
    BR_LOGI(kTagKey, "press 0x%02X (%s) -> %s", rawCode, keymap_raw_name(rawCode), desc);
    if (brlog::latencyEnabled()) {
      BR_LOGI(kTagKey, "latency notify->hid %lu us", (unsigned long)dt);
    }

    s_activeAction = action;
    s_activeRaw = rawCode;
    s_activeDown = true;
    s_reportsSent++;
    return;
  }

  // Release. The code carried by the release event is authoritative; fall back
  // to whatever is currently tracked if it does not match.
  if (s_activeDown && (rawCode == 0 || rawCode == s_activeRaw)) {
    releaseActive("key up");
    s_reportsSent++;
  }
}

// ---------------------------------------------------------------------------
// Connection events
// ---------------------------------------------------------------------------
void handleEvent(const bridge_event_t &ev) {
  s_eventsHandled++;

  switch (ev.type) {
    case BR_EV_RC_KEY:
      handleKeyEvent(ev.code, ev.pressed, ev.ts_us);
      break;

    case BR_EV_RC_LINK_UP:
      BR_LOGI(kTag, "RC003 link up");
      break;

    case BR_EV_RC_READY:
      BR_LOGI(kTag, "RC003 ready: notifications subscribed");
      // Only now can the remote actually deliver keys; make sure the host side
      // starts from a clean state.
      bridge::releaseAllKeys();
      break;

    case BR_EV_RC_LINK_DOWN:
      BR_LOGW(kTag, "RC003 link down -> clearing all key state");
      bridge::releaseAllKeys();
      break;

    case BR_EV_RC_BOND_FAIL:
      BR_LOGW(kTag, "RC003 pairing/discovery failed, will retry");
      bridge::releaseAllKeys();
      break;

    case BR_EV_RC_BATTERY:
      // The remote reports its own charge; re-publish it so the host shows the
      // remote's battery rather than a number this bridge made up.
      BR_LOGI(kTag, "RC003 battery %u%%", (unsigned)ev.code);
      hid_server::setBatteryLevel(ev.code);
      break;

    case BR_EV_WIN_LINK_UP:
      BR_LOGI(kTag, "host connected to HID service");
      bridge::releaseAllKeys();
      break;

    case BR_EV_WIN_LINK_DOWN:
      BR_LOGW(kTag, "host disconnected -> clearing all key state and re-advertising");
      bridge::releaseAllKeys();
      // Keep the RC003 link exactly as it is (requirement 9): only the
      // downstream advertising is restarted.
      break;

    default:
      break;
  }
}

}  // namespace

namespace bridge {

bool begin() {
  for (uint8_t i = 0; i < 40; i++) {
    if (Serial) break;
    delay(50);
  }

  BR_LOGI(kTag, "%s %s", BRIDGE_FW_NAME, BRIDGE_FW_VERSION);
  BR_LOGI(kTag, "console ready at %u baud", (unsigned)BRIDGE_SERIAL_BAUD);

  settings::begin();
  settings::loadKeymapModes();
  brlog::setRawEnabled(settings::rawLog());
  brlog::setLatencyEnabled(settings::latencyLog());
  brlog::setLevel(settings::logLevel());

  keymap_back_mode_t back = keymap_get_back_mode();
  keymap_power_mode_t power = keymap_get_power_mode();
  keymap_voice_mode_t voice = keymap_get_voice_mode();
  BR_LOGI(kTag, "keymap: back=%s power=%s voice=%s", keymap_back_mode_name(back), keymap_power_mode_name(power),
          keymap_voice_mode_name(voice));

  ledInit();

  if (!ble_core::begin(BRIDGE_HID_DEVICE_NAME)) {
    BR_LOGE(kTag, "BLE stack failed to start");
    return false;
  }

  if (!hid_server::begin()) {
    BR_LOGE(kTag, "HID peripheral failed to start");
    return false;
  }

  if (!rc003_client::begin()) {
    BR_LOGE(kTag, "RC003 central failed to start");
    return false;
  }

  BR_LOGI(kTag, "bridge running: dual role (central -> RC003, peripheral -> host)");
  return true;
}

void loop() {
  bridge_event_t ev;
  uint32_t drained = 0;

  // Drain everything currently queued, but never starve the rest of loop().
  while (drained < 32 && event_bus::pop(ev)) {
    handleEvent(ev);
    drained++;
  }

  // Keep advertising alive on the downstream side.
  static uint32_t lastAdvCheck = 0;
  if (millis() - lastAdvCheck > 1000) {
    lastAdvCheck = millis();
    if (!hid_server::hostConnected()) {
      hid_server::ensureAdvertising();
    }
  }

  const uint32_t dropped = event_bus::dropped();
  if (dropped > 0 && (millis() - s_lastDropReportMs) > 5000) {
    s_lastDropReportMs = millis();
    BR_LOGW(kTag, "event queue dropped %lu event(s) in total", (unsigned long)dropped);
  }

  ledUpdate();
}

void releaseAllKeys() {
  s_activeDown = false;
  s_activeRaw = 0;
  memset(&s_activeAction, 0, sizeof(s_activeAction));
  hid_server::releaseAll();
}

uint8_t activeRawCode() { return s_activeDown ? s_activeRaw : 0; }

void printStatus() {
  BR_LOGI(kTag, "--------------- status ---------------");
  BR_LOGI(kTag, "firmware        : %s %s", BRIDGE_FW_NAME, BRIDGE_FW_VERSION);
  BR_LOGI(kTag, "ble stack       : %s", ble_core::stackName());
  BR_LOGI(kTag, "hid device name : %s", hid_server::deviceName());
  BR_LOGI(kTag, "host connected  : %s (%u)", hid_server::hostConnected() ? "yes" : "no",
          (unsigned)hid_server::hostCount());
  BR_LOGI(kTag, "rc003 state     : %s", rc003_client::stateName());
  BR_LOGI(kTag, "rc003 bound     : %s", rc003_client::boundAddress().c_str());
  BR_LOGI(kTag, "rc003 connected : %s", rc003_client::connectedAddress().c_str());
  BR_LOGI(kTag, "rc003 name      : %s", rc003_client::connectedName().c_str());
  BR_LOGI(kTag, "rc003 rssi      : %d dBm", rc003_client::lastRssi());
  BR_LOGI(kTag, "notifications   : %lu", (unsigned long)rc003_client::notifyCount());
  BR_LOGI(kTag, "last report age : %d ms", rc003_client::lastReportAgeMs());
  {
    const int batt = rc003_client::batteryLevel();
    if (batt >= 0) {
      BR_LOGI(kTag, "remote battery  : %d%% (forwarded to host)", batt);
    } else {
      BR_LOGI(kTag, "remote battery  : unknown (host sees the placeholder)");
    }
  }
  BR_LOGI(kTag, "active key      : %s", s_activeDown ? keymap_raw_name(s_activeRaw) : "(none)");
  BR_LOGI(kTag, "bonds           : %d", ble_bonds::count());
  BR_LOGI(kTag, "events/unknown  : %lu / %lu", (unsigned long)s_eventsHandled, (unsigned long)s_unknownKeys);
  BR_LOGI(kTag, "queue pending   : %u (dropped %lu)", (unsigned)event_bus::pending(),
          (unsigned long)event_bus::dropped());
  BR_LOGI(kTag, "keymap modes    : back=%s power=%s voice=%s", keymap_back_mode_name(keymap_get_back_mode()),
          keymap_power_mode_name(keymap_get_power_mode()), keymap_voice_mode_name(keymap_get_voice_mode()));
  BR_LOGI(kTag, "--------------------------------------");
}

uint32_t eventsHandled() { return s_eventsHandled; }
uint32_t reportsSent() { return s_reportsSent; }
uint32_t unknownKeys() { return s_unknownKeys; }

}  // namespace bridge
