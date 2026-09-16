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
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reset_button.h"

#include <Arduino.h>

#include "ble_bonds.h"
#include "config.h"
#include "improv_serial.h"
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
// Latched for the duration of one hold: an Improv provisioning client owns this
// pin, so the hold must not fire. Cleared when the key is released, which is
// also when it is safe to warn about it again.
bool s_sessionOwnsPin = false;

void doFactoryResetAndReboot() {
  BR_LOGW(kTag, "FACTORY RESET: wiping all bonds and settings");
  ble_bonds::removeAll();
  settings::clearAll();
  delay(200);
  ESP.restart();
}

bool dtrClaimedThisHold() {
  // Same DTR-sharing rule the factory-reset hold already uses: a browser or
  // serial tool that is mid-IMPROV handshake keeps DTR low for the whole
  // provisioning window, so any BOOT press during that window belongs to the
  // client, not to a finger. Returning true here lets the caller swallow the
  // press; the improv module's own five-minute session window takes care of
  // the timing.
  if (!improv_serial::sessionActive()) return false;
  s_sessionOwnsPin = true;
  return true;
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
        s_sessionOwnsPin = false;
      }
      break;

    case St::Confirming:
      if (!down) {
        s_state = St::Idle;  // bounce, ignore
        s_sessionOwnsPin = false;
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
        s_sessionOwnsPin = false;
        // Short press: open / refresh the 30-minute Wi-Fi window. Anything
        // under kShortPressMaxMs counts as a deliberate "I want the page"
        // tap, anything longer is the start of a factory-reset hold and the
        // user can still back out by releasing early.
        if (held < kShortPressMaxMs) {
          if (dtrClaimedThisHold()) {
            BR_LOGW(kTag, "short press ignored: an Improv provisioning client is using this port (DTR is wired to GPIO9)");
          } else {
            wifi_ui::triggerRejoin();
          }
        } else {
          BR_LOGI(kTag, "press held %lu ms - released before factory reset (threshold is 5 s)",
                  (unsigned long)held);
        }
        break;
      }
      if (now - s_edgeMs >= kFactoryHoldMs) {
        // This board wires the USB-serial DTR line to GPIO9, so a browser that
        // holds the port open - which is exactly what Improv provisioning does
        // - pulls this pin low and looks identical to a finger on the key.
        //
        // A provisioning client sends its first frame within a second or so of
        // opening the port, so by the time the five-second mark arrives we can
        // tell the two apart. Once we can, the verdict is latched for the rest
        // of the hold: the DTR line stays asserted for as long as the browser
        // keeps the port, which can be minutes, and re-deciding every poll
        // would wipe the board the moment the client went quiet.
        if (!s_sessionOwnsPin && improv_serial::sessionActive()) {
          s_sessionOwnsPin = true;
          BR_LOGW(kTag, "hold ignored: an Improv provisioning client is using this port (DTR is wired to GPIO9)");
        }
        if (s_sessionOwnsPin) break;

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
