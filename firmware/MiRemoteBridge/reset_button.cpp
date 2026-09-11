/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * reset_button.cpp - BOOT key (GPIO9): short press = reboot, hold 5 s = factory
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reset_button.h"

#include <Arduino.h>

#include "ble_bonds.h"
#include "config.h"
#include "log.h"
#include "settings.h"

namespace {

const char *kTag = "BUTTON";

// The Hezhou CORE-ESP32 board wires its BOOT key to GPIO9 with an on-board
// pull-up; pressing pulls the line to GND.
constexpr int kPin = 9;
// A press shorter than this is mechanical bounce, not intent.
constexpr uint32_t kDebounceMs = 50;
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
        BR_LOGI(kTag, "key pressed - hold 5 s for factory reset (short press does nothing)");
      }
      break;

    case St::Pressed:
      if (!down) {
        const uint32_t held = now - s_edgeMs;
        s_state = St::Idle;
        // Deliberate policy: a short press does NOTHING. The only function of
        // this key is the 5-second factory-reset hold, which cannot be
        // triggered accidentally by a quick press. Logged so the press is
        // still observable on the console.
        BR_LOGI(kTag, "short press (%lu ms) - no action (hold 5 s for factory reset)",
                (unsigned long)held);
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
