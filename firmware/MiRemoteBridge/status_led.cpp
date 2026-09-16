/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * status_led.cpp - D4/D5 link indicators
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "status_led.h"

#include <Arduino.h>
#include <string.h>

#include "bridge.h"
#include "config.h"
#include "hid_gatt.h"
#include "hid_server.h"
#include "rc003_client.h"

namespace {

// Rhythms. The "waiting" state breathes (PWM fade up and down) instead of
// blinking, the "connecting" state blinks fast, "ready" is steady on.
constexpr uint32_t kBreathPeriodMs = 2000;  // one full fade up + down
constexpr uint32_t kFastHalfMs = 90;        // ~5.5 Hz blink
constexpr uint32_t kPwmFreq = 5000;         // above flicker perception
constexpr uint8_t kPwmBits = 8;
constexpr uint32_t kIdleOffMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kKeyFlashMs = 140;

enum class Show { Solid, Breathing, Fast };

struct Blinker {
  int pin = -1;
  bool fastOn = false;   // blink phase for Show::Fast
  uint32_t next = 0;     // next blink toggle
  int duty = -1;         // last duty written, to skip redundant PWM writes
};

Blinker s_host{BRIDGE_LED_HOST_PIN};
Blinker s_remote{BRIDGE_LED_REMOTE_PIN};
uint32_t s_awakeUntil = 0;
uint32_t s_keyFlashUntil = 0;
bool s_keyFlashFromSleep = false;

bool before(uint32_t now, uint32_t deadline) {
  return (int32_t)(deadline - now) > 0;
}

// Host (computer) link: nothing -> breathing, linked but HID not usable yet ->
// fast, HID reports subscribed -> solid. "Linked but not subscribed" is the
// window Windows spends enumerating the device, so fast blink is accurate.
Show hostShow() {
  if (!hid_server::hostConnected()) return Show::Breathing;
  if (!hid_gatt::keyboardSubscribed()) return Show::Fast;
  return Show::Solid;
}

// Remote link: the upstream state machine's own phases map straight onto the
// three rhythms (IDLE/SCANNING/BACKOFF = waiting, the connect+discover legs =
// connecting, subscribed = ready).
Show remoteShow() {
  if (rc003_client::notified()) return Show::Solid;
  const char *st = rc003_client::stateName();
  if (!strcmp(st, "CONNECTING") || !strcmp(st, "DIRECT") || !strcmp(st, "DISCOVERING")) {
    return Show::Fast;
  }
  return Show::Breathing;
}

void write(Blinker &b, int duty) {
  if (duty == b.duty) return;
  ledcWrite(b.pin, (uint32_t)duty);
  b.duty = duty;
}

void run(Blinker &b, Show want, bool forceOff, uint32_t now) {
  int duty;
  switch (want) {
    case Show::Solid:
      duty = 255;
      break;
    case Show::Breathing: {
      // Triangle wave: linear fade up over the first half of the period and
      // back down over the second. Duty is what the eye reads as brightness,
      // and a linear ramp reads as an even breath.
      const uint32_t half = kBreathPeriodMs / 2;
      const uint32_t t = now % kBreathPeriodMs;
      duty = (t < half) ? (int)(t * 255 / half) : (int)((kBreathPeriodMs - t) * 255 / half);
      break;
    }
    case Show::Fast:
      if ((int32_t)(now - b.next) >= 0) {
        b.next = now + kFastHalfMs;
        b.fastOn = !b.fastOn;
      }
      duty = b.fastOn ? 255 : 0;
      break;
    default:
      duty = 0;
      break;
  }
  if (forceOff) duty = 0;  // key down: dark, regardless of the rhythm
  write(b, duty);
}

}  // namespace

namespace status_led {

void begin() {
  ledcAttach(BRIDGE_LED_HOST_PIN, kPwmFreq, kPwmBits);
  ledcAttach(BRIDGE_LED_REMOTE_PIN, kPwmFreq, kPwmBits);
  ledcWrite(BRIDGE_LED_HOST_PIN, 0);
  ledcWrite(BRIDGE_LED_REMOTE_PIN, 0);
  wake();
}

void wake() { s_awakeUntil = millis() + kIdleOffMs; }

void keyActivity() {
  const uint32_t now = millis();
  s_keyFlashFromSleep = !before(now, s_awakeUntil);
  s_keyFlashUntil = now + kKeyFlashMs;
}

void loop() {
  static uint32_t last = 0;
  const uint32_t now = millis();
  // 50 Hz: fine enough for both the 2 s breath and the 90 ms fast half-period,
  // and cheap enough to sit next to the HID path.
  if (now - last < 20) return;
  last = now;

  const bool awake = before(now, s_awakeUntil);
  const bool keyFlash = before(now, s_keyFlashUntil);
  if (!awake) {
    write(s_host, 0);
    write(s_remote, keyFlash && s_keyFlashFromSleep ? 255 : 0);
    return;
  }

  run(s_host, hostShow(), false, now);
  // A key being down darkens the REMOTE led: that is the "the bridge heard
  // you" feedback the user asked for, and it uses the same active-key state
  // the console and Web UI show.
  run(s_remote, remoteShow(), keyFlash || bridge::activeRawCode() != 0, now);
}

}  // namespace status_led
