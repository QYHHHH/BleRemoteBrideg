/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * status_led.h - the board's two LEDs as link indicators
 *
 * Hezhou CORE-ESP32: D4 = GPIO12, D5 = GPIO13, both active HIGH.
 *
 *   D5  connection to the computer (host)  - slow blink: no link
 *                                            fast blink: linked, HID not ready
 *                                            solid:      HID subscribed
 *   D4  connection to the remote           - slow blink: searching
 *                                            fast blink: connecting
 *                                            solid:      ready
 *                                            dark while a remote key is down
 *
 * The dark-on-press behaviour is the "the bridge heard you" feedback: it uses
 * the same active-key state the console and the Web UI report.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace status_led {

// Configure both LED pins as outputs. Call once from setup().
void begin();

// Non-blocking; drives the blink phases from the current link states.
// Call from loop() as often as convenient (it self-throttles).
void loop();

}  // namespace status_led
