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
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <nimble/ble.h>

#include "event_bus.h"
#include "log.h"
#include "rc003_client.h"
#include <os/os_mbuf.h>

namespace {

static const char *kTag = "BLE";
bool s_started = false;

// Is this ENC_CHANGE status a *confirmed* key/authentication failure, as opposed
// to a timeout or a plain link loss?
//
// The distinction is the whole point of the upstream repair state: a remote that
// went to sleep, walked out of range or lost power produces a disconnect (and
// sometimes a timeout), and latching "your pairing is dead" on any of those
// would strand a perfectly good remote. Only these two HCI codes mean the two
// sides disagree about the link key:
//   0x05 Authentication Failure - the peer rejected our key
//   0x06 PIN or Key Missing     - the peer asked for a key one of us no longer has
bool keyFailure(int status) {
  return status == BLE_HS_ERR_HCI_BASE + BLE_ERR_AUTH_FAIL ||
         status == BLE_HS_ERR_HCI_BASE + BLE_ERR_PINKEY_MISSING;
}

int diagnosticGap(ble_gap_event *event, void *) {
  if(event->type==BLE_GAP_EVENT_NOTIFY_RX) {
    uint8_t data[32];size_t length=OS_MBUF_PKTLEN(event->notify_rx.om);
    if(length<=sizeof(data) && !os_mbuf_copydata(event->notify_rx.om,0,length,data))
      rc003_client::handleNotification(event->notify_rx.conn_handle,event->notify_rx.attr_handle,data,length);
  }
  if(event->type==BLE_GAP_EVENT_DISCONNECT)
    BR_LOGW(kTag,"disconnect handle=%u reason=%d",event->disconnect.conn.conn_handle,event->disconnect.reason);
  if(event->type==BLE_GAP_EVENT_ENC_CHANGE) {
    BR_LOGI(kTag,"security handle=%u status=%d",event->enc_change.conn_handle,event->enc_change.status);
    ble_gap_conn_desc desc{};
    // One host stack owns both links, so the role is what tells them apart:
    // slave is the computer we serve (downstream), master is the remote we
    // drive (upstream). Never guess from the handle alone.
    if(!ble_gap_conn_find(event->enc_change.conn_handle,&desc)) {
      if(desc.role==BLE_GAP_ROLE_SLAVE) {
        if(event->enc_change.status==0) {
          event_bus::post(BR_EV_WIN_AUTH_OK);
        } else {
          BR_LOGW(kTag,"host authentication failed (status=%d)",event->enc_change.status);
          event_bus::post(BR_EV_WIN_AUTH_FAIL,(uint8_t)event->enc_change.status);
        }
      } else if(desc.role==BLE_GAP_ROLE_MASTER && keyFailure(event->enc_change.status)) {
        BR_LOGW(kTag,"remote authentication/key failure (status=%d)",event->enc_change.status);
        event_bus::post(BR_EV_RC_AUTH_FAIL,(uint8_t)event->enc_change.status);
      }
    }
  }
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
  // Keep the standard Secure Connections path for the Xiaomi RC003 baseline.
  // The older HOGP compatibility probe is isolated to its report subscriptions;
  // changing the security negotiation globally made the known-good RC003
  // reconnect less predictable without fixing the CMCC MIC failure.
  BLESecurity::setAuthenticationMode(/*bonding=*/true, /*mitm=*/false, /*sc=*/true);
  BLESecurity::setCapability(ESP_IO_CAP_NONE);
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  s_started = true;
  BR_LOGI(kTag, "host stack: %s, own address: %s", stackName(), BLEDevice::getAddress().toString().c_str());
  return true;
}

}  // namespace ble_core
