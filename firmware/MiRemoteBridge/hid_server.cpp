/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_server.cpp - downstream BLE HID peripheral role
 *
 * State model
 * -----------
 * The host is shown one 10-byte input report (report ID 1) that contains a
 * keyboard collection followed by a Consumer Control collection. The report is
 * rebuilt from scratch from the current "down" set every time anything
 * changes, so it can never drift out of sync with reality.
 *
 * Two invariants are enforced here and they are the reason no key can get stuck
 * on Windows:
 *   1. pressAction() on an already-down action is a no-op.
 *   2. releaseAll() always produces an all-zero report, and is called from the
 *      bridge loop on every disconnect of either side.
 *
 * A note on why there is only one report characteristic: see the long comment
 * in hid_report_map.h. Short version - the Arduino-ESP32 3.3.11 BLE wrapper
 * cannot register two characteristics that share UUID 0x2A4D, and doing so
 * crashes the chip. Sharing one report ID between the two collections avoids
 * the broken path entirely and is legal HID.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hid_server.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLEServer.h>
#include <string.h>

#include "ble_bonds.h"
#include "config.h"
#include "event_bus.h"
#include "hid_report_map.h"
#include "log.h"

namespace {

static const char *kTag = "HID";
static const char *kTagHost = "WIN";

BLEServer *s_server = nullptr;
BLEHIDDevice *s_hid = nullptr;
BLECharacteristic *s_inputReport = nullptr;
BLEAdvertising *s_adv = nullptr;
String s_deviceName = BRIDGE_HID_DEVICE_NAME;

struct DownKey {
  uint8_t modifier;
  uint8_t keycode;
};

DownKey s_down[HID_INPUT_KEY_COUNT];
size_t s_downCount = 0;

bool s_consumerDown = false;
uint16_t s_consumerUsage = 0;

volatile bool s_hostSubscribed = false;
volatile uint8_t s_hostCount = 0;

// ---------------------------------------------------------------------------
// Report builder
//
// Every state change rebuilds and sends the entire report, including the field
// that did not change. That is deliberate: the host sees a consistent snapshot
// and there is exactly one code path that can put bytes on the wire, which is
// what makes "no stuck keys" easy to argue about.
// ---------------------------------------------------------------------------
void sendInputReport() {
  if (!s_inputReport) return;

  uint8_t report[HID_INPUT_REPORT_LEN];
  memset(report, 0, sizeof(report));

  uint8_t mod = HID_MOD_NONE;
  for (size_t i = 0; i < s_downCount; i++) {
    mod |= s_down[i].modifier;
  }
  report[HID_INPUT_OFFSET_MODIFIER] = mod;
  report[HID_INPUT_OFFSET_RESERVED] = 0x00;
  for (size_t i = 0; i < s_downCount && i < HID_INPUT_KEY_COUNT; i++) {
    report[HID_INPUT_OFFSET_KEYS + i] = s_down[i].keycode;
  }
  report[HID_INPUT_OFFSET_CONSUMER] = (uint8_t)(s_consumerUsage & 0xFF);
  report[HID_INPUT_OFFSET_CONSUMER + 1] = (uint8_t)((s_consumerUsage >> 8) & 0xFF);

  s_inputReport->setValue(report, sizeof(report));
  s_inputReport->notify();

  BR_LOGD(kTag, "report mod=0x%02X keys=%u consumer=0x%04X", (unsigned)report[HID_INPUT_OFFSET_MODIFIER],
          (unsigned)s_downCount, (unsigned)s_consumerUsage);
}

bool keyIsDown(uint8_t modifier, uint8_t keycode) {
  for (size_t i = 0; i < s_downCount; i++) {
    if (s_down[i].modifier == modifier && s_down[i].keycode == keycode) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
class BridgeServerCallbacks : public BLEServerCallbacks {
 public:
#if defined(CONFIG_NIMBLE_ENABLED)
  void onConnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
    s_hostCount = (uint8_t)pServer->getConnectedCount();
    BR_LOGI(kTagHost, "host connected (id %u addr %s mtu %u total %u)", (unsigned)desc->conn_handle,
            BLEAddress(desc->peer_id_addr).toString().c_str(), (unsigned)ble_att_mtu(desc->conn_handle),
            (unsigned)s_hostCount);
    event_bus::post(BR_EV_WIN_LINK_UP);
  }

  void onDisconnect(BLEServer *pServer, ble_gap_conn_desc *desc) override {
    s_hostCount = (uint8_t)pServer->getConnectedCount();
    s_hostSubscribed = false;
    BR_LOGW(kTagHost, "host disconnected (handle %u, remaining %u)", (unsigned)desc->conn_handle,
            (unsigned)s_hostCount);
    // The loop reacts by clearing every key and re-arming advertising. Doing
    // either from inside this GAP callback would re-enter the host task.
    event_bus::post(BR_EV_WIN_LINK_DOWN);
  }

  void onMtuChanged(BLEServer *pServer, ble_gap_conn_desc *desc, uint16_t mtu) override {
    (void)pServer;
    (void)desc;
    BR_LOGD(kTag, "mtu -> %u", (unsigned)mtu);
  }

  // NOTE: pairing events are NOT observable from here. onAuthenticationComplete
  // lives on BLESecurityCallbacks (registered once via
  // BLEDevice::setSecurityCallbacks, a single global slot shared by both the
  // upstream and downstream links) rather than on BLEServerCallbacks. It was
  // deliberately left unregistered: the same class also carries onConfirmPIN and
  // onAuthorizationRequest, whose return values the BLE library actually uses, so
  // installing it to gain one log line would put the already-working RC003
  // pairing at risk. The real evidence that Windows bonds is Windows' own
  // BTHUSB event ("the remote adapter paired successfully").
#else
  void onConnect(BLEServer *pServer) override {
    (void)pServer;
    event_bus::post(BR_EV_WIN_LINK_UP);
  }
  void onDisconnect(BLEServer *pServer) override {
    (void)pServer;
    event_bus::post(BR_EV_WIN_LINK_DOWN);
  }
#endif
};

// Windows only receives notifications on a report once it has written the CCCD.
// Knowing the exact moment it subscribes makes the log far easier to read
// during bring-up ("connected" vs "ready").
class InputReportCallbacks : public BLECharacteristicCallbacks {
 public:
#if defined(CONFIG_NIMBLE_ENABLED)
  void onSubscribe(BLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc, uint16_t subValue) override {
    (void)pCharacteristic;
    const bool enabled = (subValue != 0);
    s_hostSubscribed = enabled;
    BR_LOGI(kTagHost, "report %u notifications %s (handle %u)", (unsigned)HID_REPORT_ID_INPUT,
            enabled ? "ENABLED" : "disabled", (unsigned)desc->conn_handle);
  }
#endif
};

BridgeServerCallbacks s_serverCallbacks;
InputReportCallbacks s_inputReportCallbacks;

}  // namespace

namespace hid_server {

bool begin() {
  if (s_server) {
    BR_LOGW(kTag, "already started");
    return true;
  }

  s_server = BLEDevice::createServer();
  if (!s_server) {
    BR_LOGE(kTag, "createServer() failed");
    return false;
  }

  s_server->setCallbacks(&s_serverCallbacks);

  s_hid = new BLEHIDDevice(s_server);
  if (!s_hid) {
    BR_LOGE(kTag, "BLEHIDDevice allocation failed");
    return false;
  }

  s_inputReport = s_hid->inputReport(HID_REPORT_ID_INPUT);
  if (!s_inputReport) {
    BR_LOGE(kTag, "failed to create the input report characteristic");
    return false;
  }
  s_inputReport->setCallbacks(&s_inputReportCallbacks);

  // NOTE: BLEHIDDevice::manufacturer(String) only writes the value - the
  // characteristic itself is created by the no-argument overload. Calling the
  // string overload first dereferences a null pointer, so call both in order.
  s_hid->manufacturer();
  s_hid->manufacturer("MiRemoteBridge");
  s_hid->pnp(0x02 /* input */, 0x02E5 /* Espressif */, 0x0001, 0x0110);
  s_hid->hidInfo(0x00 /* country */, 0x02 /* normally connectable */);
  s_hid->reportMap((uint8_t *)kHidReportMap, HID_REPORT_MAP_LEN);
  s_hid->setBatteryLevel(BRIDGE_BATTERY_LEVEL);
  s_hid->startServices();

  s_adv = s_server->getAdvertising();
  s_adv->setAppearance(HID_KEYBOARD);
  s_adv->addServiceUUID(s_hid->hidService()->getUUID());
  s_adv->setName(s_deviceName);
  s_adv->setScanResponse(true);
  s_adv->setMinInterval(0x20);
  s_adv->setMaxInterval(0x40);

  // Let the core re-arm advertising itself when the host walks away; the loop
  // additionally calls ensureAdvertising() as a safety net.
  s_server->advertiseOnDisconnect(true);

  BLEDevice::startAdvertising();

  BR_LOGI(kTag, "HID peripheral up: name=\"%s\" report-map %u bytes, input report %u bytes, host stack %s",
          s_deviceName.c_str(), (unsigned)HID_REPORT_MAP_LEN, (unsigned)HID_INPUT_REPORT_LEN,
          BLEDevice::getBLEStackString().c_str());
  return true;
}

void pressAction(const hid_action_t &action) {
  if (action.kind == HID_ACT_NONE) return;

  if (action.kind == HID_ACT_KEYBOARD) {
    if (action.keycode == HID_KEY_NONE) return;
    if (keyIsDown(action.modifier, action.keycode)) return;  // idempotent

    if (s_downCount >= HID_INPUT_KEY_COUNT) {
      // Roll the oldest entry off; the RC003 is a single-key remote so this is
      // a safety valve rather than a real code path.
      memmove(&s_down[0], &s_down[1], sizeof(DownKey) * (HID_INPUT_KEY_COUNT - 1));
      s_downCount = HID_INPUT_KEY_COUNT - 1;
      BR_LOGW(kTag, "report full, dropped oldest key");
    }
    s_down[s_downCount].modifier = action.modifier;
    s_down[s_downCount].keycode = action.keycode;
    s_downCount++;
    sendInputReport();
    return;
  }

  if (action.kind == HID_ACT_CONSUMER) {
    if (action.consumer == HID_CONSUMER_NONE) return;
    if (s_consumerDown && s_consumerUsage == action.consumer) return;  // idempotent
    s_consumerDown = true;
    s_consumerUsage = action.consumer;
    sendInputReport();
    return;
  }
}

void releaseAction(const hid_action_t &action) {
  if (action.kind == HID_ACT_KEYBOARD) {
    for (size_t i = 0; i < s_downCount; i++) {
      if (s_down[i].modifier == action.modifier && s_down[i].keycode == action.keycode) {
        for (size_t j = i; j + 1 < s_downCount; j++) {
          s_down[j] = s_down[j + 1];
        }
        s_downCount--;
        sendInputReport();
        return;
      }
    }
    return;
  }

  if (action.kind == HID_ACT_CONSUMER) {
    if (!s_consumerDown) return;
    if (action.consumer != HID_CONSUMER_NONE && action.consumer != s_consumerUsage) return;
    s_consumerDown = false;
    s_consumerUsage = 0;
    sendInputReport();
    return;
  }
}

void releaseAll() {
  const bool dirty = (s_downCount > 0) || s_consumerDown;

  s_downCount = 0;
  memset(s_down, 0, sizeof(s_down));
  s_consumerDown = false;
  s_consumerUsage = 0;

  if (dirty) {
    sendInputReport();
    BR_LOGI(kTag, "release-all: report cleared");
  }
}

bool hostConnected() { return s_hostCount > 0; }

uint8_t hostCount() { return s_hostCount; }

void forceReAdvertise() {
  if (!s_adv || !s_hid) return;

  if (s_server && s_server->getConnectedCount() > 0) {
    BR_LOGW(kTag, "dropping host connection so it must re-pair");
    // Disconnecting from the loop task is safe; the callback only posts.
    s_server->disconnect(s_server->getConnId());
    delay(80);
  }

  BLEDevice::stopAdvertising();
  delay(30);
  s_adv->reset();
  s_adv->setAppearance(HID_KEYBOARD);
  s_adv->addServiceUUID(s_hid->hidService()->getUUID());
  s_adv->setName(s_deviceName);
  s_adv->setScanResponse(true);
  s_adv->setMinInterval(0x20);
  s_adv->setMaxInterval(0x40);
  BLEDevice::startAdvertising();
  BR_LOGI(kTag, "advertising restarted");
}

void ensureAdvertising() {
  if (!s_adv) return;
  if (s_server && s_server->getConnectedCount() > 0) return;
  if (s_adv->isAdvertising()) return;
  BLEDevice::startAdvertising();
  BR_LOGI(kTag, "advertising re-armed");
}

int forgetBondForConnectedHost() {
  if (!s_server || s_server->getConnectedCount() == 0) {
    BR_LOGW(kTagHost, "no connected host, nothing to forget");
    return 0;
  }

  // Read the descriptor of the single connection we care about.
  ble_gap_conn_desc desc;
  const int rc = ble_gap_conn_find(s_server->getConnId(), &desc);
  if (rc != 0) {
    BR_LOGE(kTagHost, "ble_gap_conn_find failed: %d", rc);
    return 0;
  }

  return ble_bonds::removePeer(BLEAddress(desc.peer_id_addr));
}

const char *deviceName() { return s_deviceName.c_str(); }

}  // namespace hid_server
