/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * bridge_events.h - events exchanged between the BLE callbacks and the loop
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  BR_EV_NONE = 0,
  BR_EV_RC_KEY,          // RC003 key normalised to press/release
  BR_EV_RC_LINK_UP,      // GATT link to the RC003 established
  BR_EV_RC_READY,        // services discovered and notifications subscribed
  BR_EV_RC_LINK_DOWN,    // RC003 link lost (sleep, out of range, power off)
  BR_EV_RC_BOND_FAIL,    // pairing/bonding with the RC003 failed
  BR_EV_RC_BATTERY,      // RC003 reported its charge; `code` = percent (0-100)
  BR_EV_WIN_LINK_UP,     // a host (Windows) connected to our HID server
  BR_EV_WIN_LINK_DOWN    // the host disconnected
} bridge_event_type_t;

typedef struct {
  uint8_t  type;     // bridge_event_type_t
  uint8_t  code;     // MI_KEY_* for BR_EV_RC_KEY, percent for BR_EV_RC_BATTERY
  bool     pressed;  // for BR_EV_RC_KEY
  uint32_t ts_ms;    // millis() at the moment the BLE callback ran
  uint32_t ts_us;    // micros() at the same instant, for latency measurement
} bridge_event_t;

const char *bridge_event_name(uint8_t type);

#ifdef __cplusplus
}
#endif
