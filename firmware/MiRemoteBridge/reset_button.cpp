/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * reset_button.cpp - BOOT key (GPIO9)
 *
 *   short press (< ~3 s)  -> open / refresh the 30-minute config UI window
 *                             (calls wifi_ui::triggerRejoin)
 *   hold >= 5 s           -> factory reset (wipe all bonds and settings, then
 *                             reboot)
 *
 * Progress is logged once per second while held, so the outcome of a press is
 * always visible on the console.
 *
 * GPIO9 is shared with the USB-serial auto-reset circuit, where EN and GPIO9
 * are driven by the DTR/RTS *pair* - a tool that leaves the lines in the (1,0)
 * combination pulls GPIO9 low and looks exactly like a finger on the key.
 *
 * An earlier version stood down while an Improv client was connected, on the
 * theory that a browser holding the port open did that. It does not: Chromium
 * applies DTR and RTS in a single SetCommState, landing on (1,1), which leaves
 * GPIO9 alone (measured 2026-09-17; truth table in docs/WEB-UI.md). That guard
 * could only ever fire for a real press - swallowing the short press and, worse,
 * blocking the factory reset that RECOVERY.md points users at. It is gone. A
 * held key now always means a finger, and both outcomes always fire.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reset_button.h"

#include <Arduino.h>

#include "ble_bonds.h"
#include "config.h"
#include "log.h"
#include "settings.h"
#include "wifi_ui.h"

namespace {

const char *kTag = "BUTTON";

// The Hezhou CORE-ESP32 board wires its BOOT key to GPIO9 with an on-board
// pull-up; pressing pulls the line to GND.
constexpr int kPin = 9;
// A press shorter than this is mechanical bounce, not intent.
constexpr uint32_t kDebounceMs = 50;
// Maximum hold time that still counts as a "short press" for the Wi-Fi window.
// Longer presses are interpreted as factory reset territory, where the per-
// second progress log keeps showing the countdown so the user can still bail.
constexpr uint32_t kShortPressMaxMs = 3000;
// Holding this long triggers the factory reset while the key is still down.
constexpr uint32_t kFactoryHoldMs = 5000;

enum class St { Idle, Confirming, Pressed };

St s_state = St::Idle;
uint32_t s_edgeMs = 0;
uint32_t s_lastProgressMs = 0;

void doFactoryResetAndReboot() {
  BR_LOGW(kTag, "FACTORY RESET: wiping all bonds and settings");
  ble_bonds::removeAll();
  settings::clearAll();
  delay(200);
  ESP.restart();
}

}  // namespace

namespace reset_button {

void begin() { pinMode(kPin, INPUT_PULLUP); }

void poll() {
  const bool down = digitalRead(kPin) == LOW;
  const uint32_t now = millis();

  switch (s_state) {
    case St::Idle:
      if (down) {
        s_state = St::Confirming;
        s_edgeMs = now;
      }
      break;

    case St::Confirming:
      if (!down) {
        s_state = St::Idle;  // bounce, ignore
      } else if (now - s_edgeMs >= kDebounceMs) {
        s_state = St::Pressed;
        s_edgeMs = now;  // restart the clock from the confirmed press
        s_lastProgressMs = now;
        BR_LOGI(kTag, "key pressed - short press opens the config UI; hold 5 s for factory reset");
      }
      break;

    case St::Pressed:
      if (!down) {
        const uint32_t held = now - s_edgeMs;
        s_state = St::Idle;
        // Short press: open / refresh the 30-minute Wi-Fi window. Anything
        // under kShortPressMaxMs counts as a deliberate "I want the page"
        // tap, anything longer is the start of a factory-reset hold and the
        // user can still back out by releasing early.
        if (held < kShortPressMaxMs) {
          wifi_ui::triggerRejoin();
        } else {
          BR_LOGI(kTag, "press held %lu ms - released before factory reset (threshold is 5 s)",
                  (unsigned long)held);
        }
        break;
      }
      if (now - s_edgeMs >= kFactoryHoldMs) {
        doFactoryResetAndReboot();  // never returns
        break;
      }
      // Once-per-second progress so the user can see the countdown and stop
      // short of the threshold if the hold was accidental.
      if (now - s_lastProgressMs >= 1000) {
        s_lastProgressMs = now;
        BR_LOGW(kTag, "held %lu ms - keep holding %lu ms more for factory reset",
                (unsigned long)(now - s_edgeMs),
                (unsigned long)(kFactoryHoldMs - (now - s_edgeMs)));
      }
      break;
  }
}

void factoryResetAndReboot() { doFactoryResetAndReboot(); }

}  // namespace reset_button
