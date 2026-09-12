/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * bridge.h - the glue: event loop, key dispatch and state bookkeeping
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

namespace bridge {

// Settings -> keymap modes -> BLE stack -> HID server -> RC003 central.
// Order matters: the HID peripheral is up before the central starts looking
// for the remote, so Windows can connect while the remote is still asleep.
bool begin();

// Called from loop(). Drains the event queue and dispatches to the HID server.
void loop();

// Clears every HID key state and the tracked RC003 key. Called on any
// disconnect of either side.
void releaseAllKeys();

// One key that is currently down on the RC003 side (raw code + translated
// action), or "none".
uint8_t activeRawCode();

// Count of key-downs that were actually forwarded, and the last raw code.
// The Web UI compares the counter between status polls, so a press shorter
// than the poll interval still produces a visible flash instead of vanishing.
uint32_t keyPresses();
// Count every recognized press/release transition, including unmapped keys.
uint32_t keyEvents();
uint8_t lastKeyRaw();

// Print the multi-line status block used by `status` and at boot.
void printStatus();

// Counters for the selftest/status output.
uint32_t eventsHandled();
uint32_t reportsSent();
uint32_t unknownKeys();

}  // namespace bridge
