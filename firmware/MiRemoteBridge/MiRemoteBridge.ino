/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * MiRemoteBridge.ino - entry point
 *
 *   Xiaomi Bluetooth Remote 2 Pro (RC003)
 *        |  BLE, central role  (HOGP input reports + ATVV control for the
 *        |                       voice button only - no audio is ever touched)
 *        v
 *   ESP32-C3  --- fixed-capacity event queue ---> HID reports
 *        |  BLE, peripheral role (HID keyboard + Consumer Control)
 *        v
 *   Windows / any BLE HID host
 *
 * One NimBLE host stack, two simultaneous roles. Nothing here deals with USB,
 * audio, Wi-Fi, a companion application or drivers of any kind.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <Arduino.h>

#include "bridge.h"
#include "cli.h"
#include "config.h"
#include "log.h"
#include "reset_button.h"
#include "status_led.h"
#include "wifi_ui.h"

void setup() {
  brlog::begin(BRIDGE_SERIAL_BAUD);
  delay(150);

  cli::begin();
  reset_button::begin();
  status_led::begin();

  if (!bridge::begin()) {
    BR_LOGE("MAIN", "bridge failed to start; console still available for diagnosis");
  } else {
    bridge::printStatus();
  }

  // After the bridge: wifi_ui::begin() reads the saved network out of NVS,
  // and it is settings::begin() (inside bridge::begin) that loads it. Calling
  // this earlier would always see an empty SSID and never arm the auto-start.
  wifi_ui::begin();
}

void loop() {
  cli::poll();
  reset_button::poll();
  status_led::loop();
  bridge::loop();  // drain key releases before each bounded HTTP step
  wifi_ui::loop();
  delay(1);
}
