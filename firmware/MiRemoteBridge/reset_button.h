/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * reset_button.h - the board's BOOT key (GPIO9) as a recovery button
 *
 * The Hezhou CORE-ESP32 board exposes two keys: RESET (hard reset of the chip,
 * invisible to firmware) and BOOT wired to GPIO9. GPIO9 is a strapping pin for
 * the ROM bootloader (hold it during power-up to enter the download mode), but
 * at runtime it is a free input with an on-board pull-up, pressed = LOW.
 *
 * This module turns it into a recovery button:
 *
 *   short press  (< 5 s)   -> deliberately does nothing (logged only)
 *   hold >= 5 s            -> factory reset (wipe all bonds and settings, then
 *                             reboot)
 *
 * Progress is logged once per second while held, so the outcome of a press is
 * always visible on the console.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace reset_button {

// Configure GPIO9 as input with pull-up. Call once from setup().
void begin();

// Poll the key. Call from loop() as often as possible; the state machine is
// non-blocking and never delays.
void poll();

// Wipe every bond and every persisted setting, then reboot. Shared by the
// 5-second hold and the `factory` console command so both paths behave
// identically.
void factoryResetAndReboot();

}  // namespace reset_button
