/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_core.cpp - BLE stack initialisation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ble_core.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLESecurity.h>

#include "log.h"

namespace {

static const char *kTag = "BLE";
bool s_started = false;
int diagnosticGap(ble_gap_event *event, void *) {
  if(event->type==BLE_GAP_EVENT_DISCONNECT)
    BR_LOGW(kTag,"disconnect handle=%u reason=%d",event->disconnect.conn.conn_handle,event->disconnect.reason);
  if(event->type==BLE_GAP_EVENT_ENC_CHANGE)
    BR_LOGI(kTag,"security handle=%u status=%d",event->enc_change.conn_handle,event->enc_change.status);
  return 0;
}

}  // namespace

namespace ble_core {

const char *stackName() {
  // getBLEStackString() returns a String by value, so returning .c_str()
  // straight from it would hand back a pointer into a destroyed temporary.
  // A function-local static keeps it alive for the life of the program.
  static String s_name = BLEDevice::getBLEStackString();
  return s_name.c_str();
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

  BLEDevice::setCustomGapHandler(diagnosticGap);
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
