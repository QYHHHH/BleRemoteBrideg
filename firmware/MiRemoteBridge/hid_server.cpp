/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_server.cpp - downstream BLE HID peripheral role
 *
 * State model
 * -----------
 * The host is shown TWO input reports, one per collection:
 *
 *   report ID 1, 8 bytes  keyboard         (modifier, reserved, 6 key codes)
 *   report ID 2, 2 bytes  consumer control (16-bit usage)
 *
 * Both are rebuilt from scratch from the current "down" set every time anything
 * changes, so neither can drift out of sync with reality.
 *
 * Two invariants are enforced here and they are the reason no key can get stuck
 * on Windows:
 *   1. pressAction() on an already-down action is a no-op.
 *   2. releaseAll() always produces all-zero reports, and is called from the
 *      bridge loop on every disconnect of either side.
 *
 * The GATT service itself lives in hid_gatt.cpp, built directly on NimBLE
 * because this layout needs two characteristics that both carry UUID 0x2A4D --
 * one per report ID, as HOGP defines it -- and the Arduino BLE wrapper cannot
 * register two of those. The history is in docs/TESTING.md sections 4.5/4.6.
 *
 * SPDX-License-Identifier: MIT
 */

#include "hid_server.h"

#include <Arduino.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEAddress.h>
#include <BLEServer.h>
#include <host/ble_gap.h>
#include <string.h>

#include "ble_bonds.h"
#include "config.h"
#include "event_bus.h"
#include "hid_gatt.h"
#include "hid_report_map.h"
#include "log.h"
#include "settings.h"

namespace {

static const char *kTag = "HID";
static const char *kTagHost = "WIN";

// Appearance advertised to the host: HID Keyboard (Bluetooth assigned numbers,
// section 3.2.1). Written out rather than pulled from HIDTypes.h so this file
// does not depend on BLEHIDDevice's header tree.
constexpr uint16_t kAppearanceKeyboard = 0x03C1;

// HID service UUID, used to advertise which service the host should look for.
constexpr uint16_t kHidServiceUuid = 0x1812;

BLEServer *s_server = nullptr;
BLEAdvertising *s_adv = nullptr;
String s_deviceName = BRIDGE_HID_DEVICE_NAME;

struct DownKey {
  uint8_t modifier;
  uint8_t keycode;
};

DownKey s_down[HID_KB_KEY_COUNT];
size_t s_downCount = 0;

bool s_consumerDown = false;
uint16_t s_consumerUsage = 0;

volatile uint8_t s_hostCount = 0;

// ---------------------------------------------------------------------------
// Report builder
//
// Every state change rebuilds and sends both reports, including the field that
// did not change. That is deliberate: the host sees a consistent snapshot and
// there is exactly one code path that can put bytes on the wire, which is what
// makes "no stuck keys" easy to argue about. The cost is one extra 2-byte
// notification per event.
// ---------------------------------------------------------------------------
void sendReports() {
  uint8_t keyboard[HID_KB_REPORT_LEN];
  memset(keyboard, 0, sizeof(keyboard));

  uint8_t mod = HID_MOD_NONE;
  for (size_t i = 0; i < s_downCount; i++) {
    mod |= s_down[i].modifier;
  }
  keyboard[HID_KB_OFFSET_MODIFIER] = mod;
  keyboard[HID_KB_OFFSET_RESERVED] = 0x00;
  for (size_t i = 0; i < s_downCount && i < HID_KB_KEY_COUNT; i++) {
    keyboard[HID_KB_OFFSET_KEYS + i] = s_down[i].keycode;
  }

  uint8_t consumer[HID_CONSUMER_REPORT_LEN];
  consumer[0] = (uint8_t)(s_consumerUsage & 0xFF);
  consumer[1] = (uint8_t)((s_consumerUsage >> 8) & 0xFF);

  hid_gatt::notifyKeyboard(keyboard, sizeof(keyboard));
  hid_gatt::notifyConsumer(consumer, sizeof(consumer));

  BR_LOGD(kTag, "report kb mod=0x%02X keys=%u cons=0x%04X", (unsigned)mod, (unsigned)s_downCount,
          (unsigned)s_consumerUsage);
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
    // An abrupt disconnect does not always produce a final unsubscribe event, so
    // the cached subscription state is cleared here rather than trusted.
    hid_gatt::resetSubscriptions();
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
  //
  // Also worth remembering: "the log does not contain it" is not evidence that
  // it did not happen - it only means nothing printed it.
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

BridgeServerCallbacks s_serverCallbacks;

// ---------------------------------------------------------------------------
// Advertising
// ---------------------------------------------------------------------------
void configureAdvertising() {
  s_adv->setAppearance(kAppearanceKeyboard);
  s_adv->addServiceUUID(BLEUUID((uint16_t)kHidServiceUuid));
  s_adv->setName(s_deviceName);
  s_adv->setScanResponse(true);
  s_adv->setMinInterval(0x20);
  s_adv->setMaxInterval(0x40);
}

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

  // Values the host will read while enumerating.
  hid_gatt::setReportMap(kHidReportMap, HID_REPORT_MAP_LEN);
  hid_gatt::setManufacturer("MiRemoteBridge");
  hid_gatt::setPnpId(0x02 /* USB-IF */, 0x02E5 /* Espressif */, 0x0001, 0x0110);
  // Battery: prefer the last level the remote actually reported (persisted in
  // NVS), so a host connecting right after a cold boot reads a real number
  // rather than a placeholder. BRIDGE_BATTERY_LEVEL only applies before the
  // remote has ever reported anything.
  const int persisted = settings::batteryLevel();
  hid_gatt::setBatteryLevel(persisted >= 0 ? (uint8_t)persisted : (uint8_t)BRIDGE_BATTERY_LEVEL);

  // Registers the HID, Device Information and Battery services. Must happen
  // before the server is started, because starting it pushes the GATT database
  // live.
  if (!hid_gatt::begin()) {
    BR_LOGE(kTag, "failed to register the GATT services");
    return false;
  }

  s_server->start();

  s_adv = s_server->getAdvertising();
  configureAdvertising();

  // Let the core re-arm advertising itself when the host walks away; the loop
  // additionally calls ensureAdvertising() as a safety net.
  s_server->advertiseOnDisconnect(true);

  BLEDevice::startAdvertising();

  BR_LOGI(kTag, "HID peripheral up: name=\"%s\" report-map %u bytes, reports %u+%u bytes, host stack %s",
          s_deviceName.c_str(), (unsigned)HID_REPORT_MAP_LEN, (unsigned)HID_KB_REPORT_LEN,
          (unsigned)HID_CONSUMER_REPORT_LEN, BLEDevice::getBLEStackString().c_str());
  return true;
}

void pressAction(const hid_action_t &action) {
  if (action.kind == HID_ACT_NONE) return;

  if (action.kind == HID_ACT_KEYBOARD) {
    if (action.keycode == HID_KEY_NONE) return;
    if (keyIsDown(action.modifier, action.keycode)) return;  // idempotent

    if (s_downCount >= HID_KB_KEY_COUNT) {
      // Roll the oldest entry off; the RC003 is a single-key remote so this is
      // a safety valve rather than a real code path.
      memmove(&s_down[0], &s_down[1], sizeof(DownKey) * (HID_KB_KEY_COUNT - 1));
      s_downCount = HID_KB_KEY_COUNT - 1;
      BR_LOGW(kTag, "report full, dropped oldest key");
    }
    s_down[s_downCount].modifier = action.modifier;
    s_down[s_downCount].keycode = action.keycode;
    s_downCount++;
    sendReports();
    return;
  }

  if (action.kind == HID_ACT_CONSUMER) {
    if (action.consumer == HID_CONSUMER_NONE) return;
    if (s_consumerDown && s_consumerUsage == action.consumer) return;  // idempotent
    s_consumerDown = true;
    s_consumerUsage = action.consumer;
    sendReports();
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
        sendReports();
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
    sendReports();
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
    sendReports();
    BR_LOGI(kTag, "release-all: reports cleared");
  }
}

bool hostConnected() { return s_hostCount > 0; }

uint8_t hostCount() { return s_hostCount; }

void forceReAdvertise() {
  if (!s_adv) return;

  if (s_server && s_server->getConnectedCount() > 0) {
    BR_LOGW(kTag, "dropping host connection so it must re-pair");
    // Disconnecting from the loop task is safe; the callback only posts.
    s_server->disconnect(s_server->getConnId());
    delay(80);
  }

  BLEDevice::stopAdvertising();
  delay(30);
  s_adv->reset();
  configureAdvertising();
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

int forgetHostBonds() {
  const String remote = settings::rc003Address();

  // Preferred path: a host is connected, so the peer to drop is unambiguous.
  if (s_server && s_server->getConnectedCount() > 0) {
    ble_gap_conn_desc desc;
    if (ble_gap_conn_find(s_server->getConnId(), &desc) == 0) {
      const int removed = ble_bonds::removePeer(BLEAddress(desc.peer_id_addr));
      BR_LOGI(kTagHost, "connected host bond removed: %d record(s)", removed);
      return removed;
    }
    BR_LOGW(kTagHost, "ble_gap_conn_find failed, sweeping every host bond instead");
  }

  // No host connected. Sweep every bond that is not the remote's. This case
  // matters: a host that needs forgetting has usually already failed to connect
  // (driver wedged, or its own pairing was deleted and the two sides now
  // disagree about the link key), so waiting for a connection would deadlock.
  BLEAddress peers[16];
  const int n = ble_bonds::list(peers, 16);
  if (n < 0) {
    BR_LOGE(kTagHost, "bond list failed");
    return 0;
  }

  int removed = 0;
  for (int i = 0; i < n; i++) {
    if (remote.length() > 0 && peers[i].toString().equalsIgnoreCase(remote)) continue;
    const int r = ble_bonds::removePeer(peers[i]);
    if (r > 0) {
      BR_LOGI(kTagHost, "removed host bond %s", peers[i].toString().c_str());
      removed += r;
    }
  }
  if (removed == 0) BR_LOGW(kTagHost, "no host bond to forget");
  return removed;
}

void setBatteryLevel(uint8_t percent) { hid_gatt::setBatteryLevel(percent); }

const char *deviceName() { return s_deviceName.c_str(); }

}  // namespace hid_server
