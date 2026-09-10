/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_core.cpp - BLE stack initialisation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_core.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>

#include "log.h"

namespace {

static const char *kTag = "BLE";
bool s_started = false;

}  // namespace

namespace ble_core {

const char *stackName() {
  return BLEDevice::getBLEStackString().c_str();
}

bool started() { return s_started; }

bool begin(const char *deviceName) {
  if (s_started) {
    BR_LOGW(kTag, "BLE stack already initialised, ignoring duplicate init()");
    return true;
  }

  if (!BLEDevice::init(deviceName ? deviceName : "MiRemoteBridge")) {
    BR_LOGE(kTag, "BLEDevice::init() failed");
    return false;
  }

  BLEDevice::setPower(ESP_PWR_LVL_P9);

  // Just Works bonding, no MITM: the RC003 has no input/output capability and
  // neither does a keyboard-only peripheral. Bonding=true is required so both
  // peers can reconnect from a stored long-term key without re-pairing.
  BLESecurity::setAuthenticationMode(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  s_started = true;
  BR_LOGI(kTag, "host stack: %s, own address: %s", stackName(), BLEDevice::getAddress().toString().c_str());
  return true;
}

}  // namespace ble_core
