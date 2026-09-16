/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * status_led.h - the board's two LEDs as link indicators
 *
 * Hezhou CORE-ESP32: D4 = GPIO12, D5 = GPIO13, both active HIGH.
 *
 *   D5  connection to the computer (host)  - breathing: no link yet
 *                                            fast blink: linked, HID not ready
 *                                            solid:      HID subscribed
 *                                            double flash: re-add it in Windows
 *   D4  connection to the remote           - breathing: searching
 *                                            fast blink: connecting
 *                                            solid:      ready
 *                                            dark while a remote key is down
 *                                            double flash: pairing must be redone
 *
 * The double flash is "on 120 ms, off 120 ms, on 120 ms" once per ~1.6 s. Both
 * LEDs still go dark after ten minutes without activity - including the double
 * flash - while the Web UI keeps showing the notice, because the page is where
 * the instruction lives.
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

// Keep normal link indication visible for another ten minutes.
void wake();

// Acknowledge a key. While asleep D4 flashes without restarting the 10-minute timer.
void keyActivity();

}  // namespace status_led
