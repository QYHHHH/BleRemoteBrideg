/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_gatt.cpp - the downstream HID service, built directly on NimBLE
 *
 * See hid_gatt.h for why this is not built with BLEHIDDevice.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hid_gatt.h"

#include <Arduino.h>

#include <host/ble_att.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_uuid.h>
#include <os/os_mbuf.h>

#include <string.h>

#include "config.h"
#include "hid_report_map.h"
#include "log.h"

namespace {

static const char *kTag = "HIDG";

// ---------------------------------------------------------------------------
// Attribute identity
//
// NimBLE hands the access callback back whatever pointer was registered, so a
// small integer is enough to know which attribute is being read or written. An
// enum keeps the definitions readable.
// ---------------------------------------------------------------------------
enum AttrId : uintptr_t {
  A_HID_INFO = 1,
  A_REPORT_MAP,
  A_HID_CONTROL,
  A_PROTOCOL_MODE,
  A_REPORT_KEYBOARD,
  A_REF_KEYBOARD,
  A_REPORT_CONSUMER,
  A_REF_CONSUMER,
  A_MANUFACTURER,
  A_PNP_ID,
  A_BATTERY_LEVEL,
};

// ---------------------------------------------------------------------------
// 16-bit UUIDs
//
// One object per UUID rather than a fresh declaration at each use, so the
// pointers stored in the definitions below stay valid for the life of the
// program (the GATT server keeps them).
// ---------------------------------------------------------------------------
static const ble_uuid16_t kUuidHidService = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t kUuidHidInfo = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t kUuidReportMap = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t kUuidHidControl = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t kUuidReport = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t kUuidProtocolMode = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t kUuidReportReference = BLE_UUID16_INIT(0x2908);
static const ble_uuid16_t kUuidDeviceInfoService = BLE_UUID16_INIT(0x180A);
static const ble_uuid16_t kUuidManufacturerName = BLE_UUID16_INIT(0x2A29);
static const ble_uuid16_t kUuidPnpId = BLE_UUID16_INIT(0x2A50);
static const ble_uuid16_t kUuidBatteryService = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t kUuidBatteryLevel = BLE_UUID16_INIT(0x2A19);

// ---------------------------------------------------------------------------
// Values served to the host
// ---------------------------------------------------------------------------
// HID Information: bcdHID 1.11, country code 0, flags 0x02 (normally
// connectable). 0x02 rather than 0x00 is what real peripherals advertise and it
// tells the host the device is happy to be reconnected to.
uint8_t s_hidInfo[4] = {0x11, 0x01, 0x00, 0x02};

const uint8_t *s_reportMap = nullptr;
size_t s_reportMapLen = 0;

char s_manufacturer[32] = "MiRemoteBridge";

// PnP ID: vendor id source 0x02 (USB-IF), Espressif's vendor id, then a product
// id and version of our own. These only shape the instance path Windows builds.
uint8_t s_pnpId[7] = {0x02, 0xE5, 0x02, 0x00, 0x01, 0x00, 0x01};

uint8_t s_protocolMode = 0x01;  // 1 = report protocol
uint8_t s_batteryLevel = BRIDGE_BATTERY_LEVEL;

// Report Reference descriptors: {report ID, report type}. Type 1 = input. These
// are what tells the host which report ID each 0x2A4D characteristic carries -
// without them the two characteristics would be indistinguishable.
uint8_t s_refKeyboard[2] = {HID_REPORT_ID_KEYBOARD, HID_REPORT_TYPE_INPUT};
uint8_t s_refConsumer[2] = {HID_REPORT_ID_CONSUMER, HID_REPORT_TYPE_INPUT};

// Last report sent on each characteristic. HOGP says a Report characteristic
// reads back as the most recent report, so these are what the read callback
// serves.
uint8_t s_lastKeyboard[HID_KB_REPORT_LEN];
uint8_t s_lastConsumer[HID_CONSUMER_REPORT_LEN];

// Filled in by the stack during registration.
uint16_t s_handleKeyboard = 0;
uint16_t s_handleConsumer = 0;
uint16_t s_handleBattery = 0;

bool s_registered = false;
uint16_t s_connHandle = BLE_HS_CONN_HANDLE_NONE;
bool s_subKeyboard = false;
bool s_subConsumer = false;
bool s_subBattery = false;

struct ble_gap_event_listener s_listener;

// ---------------------------------------------------------------------------
// Access callback
// ---------------------------------------------------------------------------
int appendValue(struct ble_gatt_access_ctxt *ctxt, const void *data, size_t len) {
  if (ctxt->offset >= len) {
    // A read past the end is legal in ATT (it returns a zero-length value).
    return 0;
  }
  const uint8_t *bytes = (const uint8_t *)data + ctxt->offset;
  const size_t remaining = len - ctxt->offset;
  const int rc = os_mbuf_append(ctxt->om, bytes, remaining);
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

int gattAccess(uint16_t connHandle, uint16_t attrHandle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)connHandle;
  (void)attrHandle;

  switch ((AttrId)(uintptr_t)arg) {
    case A_HID_INFO:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_hidInfo, sizeof(s_hidInfo));

    case A_REPORT_MAP:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      if (s_reportMap == nullptr) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_reportMap, s_reportMapLen);

    case A_HID_CONTROL:
      // One byte, host -> device: suspend (0x00) / exit suspend (0x01). This
      // firmware has nothing to suspend, and accepts either silently.
      if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
      return 0;

    case A_PROTOCOL_MODE:
      if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) return appendValue(ctxt, &s_protocolMode, 1);
      if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ctxt->om->om_len < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        s_protocolMode = ctxt->om->om_data[0];
        // Boot protocol would need the Boot Keyboard Input report (0x2A22),
        // which is not implemented - the report map carries no boot-compatible
        // collection. Say so rather than pretend, because if a host ever does
        // switch, nothing it receives will make sense.
        BR_LOGI(kTag, "host set protocol mode 0x%02X%s", (unsigned)s_protocolMode,
                s_protocolMode == 0x00 ? " (boot mode - NOT implemented)" : "");
        return 0;
      }
      return BLE_ATT_ERR_UNLIKELY;

    case A_REPORT_KEYBOARD:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_lastKeyboard, sizeof(s_lastKeyboard));

    case A_REPORT_CONSUMER:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_lastConsumer, sizeof(s_lastConsumer));

    case A_REF_KEYBOARD:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_refKeyboard, sizeof(s_refKeyboard));

    case A_REF_CONSUMER:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_DSC) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_refConsumer, sizeof(s_refConsumer));

    case A_MANUFACTURER:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_manufacturer, strlen(s_manufacturer));

    case A_PNP_ID:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      return appendValue(ctxt, s_pnpId, sizeof(s_pnpId));

    case A_BATTERY_LEVEL:
      if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;
      // Logged on purpose: whether the host actually re-reads this value after
      // a reconnect decides how the stale-100% display gets fixed. Without
      // this, "Windows caches it" and "Windows never came back to look" are
      // indistinguishable from the device side.
      BR_LOGI(kTag, "host READ battery -> %u%%", (unsigned)s_batteryLevel);
      return appendValue(ctxt, &s_batteryLevel, 1);
  }

  return BLE_ATT_ERR_UNLIKELY;
}

// ---------------------------------------------------------------------------
// Service definitions
//
// Descriptor arrays are non-const because ble_gatt_chr_def::descriptors is a
// non-const pointer.
// ---------------------------------------------------------------------------
struct ble_gatt_dsc_def s_dscKeyboard[] = {
    {(const ble_uuid_t *)&kUuidReportReference, BLE_ATT_F_READ, 0, gattAccess, (void *)(uintptr_t)A_REF_KEYBOARD},
    {nullptr, 0, 0, nullptr, nullptr},
};

struct ble_gatt_dsc_def s_dscConsumer[] = {
    {(const ble_uuid_t *)&kUuidReportReference, BLE_ATT_F_READ, 0, gattAccess, (void *)(uintptr_t)A_REF_CONSUMER},
    {nullptr, 0, 0, nullptr, nullptr},
};

// Positional initialisation matches ble_gatt_chr_def exactly:
//   uuid, access_cb, arg, descriptors, flags, min_key_size, val_handle, cpfd
#define HID_CHR(uuid, id, flags, descs, valHandle) \
  {(const ble_uuid_t *)&(uuid), gattAccess, (void *)(uintptr_t)(id), (descs), (ble_gatt_chr_flags)(flags), 0, (valHandle), nullptr}

static const struct ble_gatt_chr_def s_hidCharacteristics[] = {
    HID_CHR(kUuidHidInfo, A_HID_INFO, BLE_GATT_CHR_F_READ, nullptr, nullptr),
    HID_CHR(kUuidReportMap, A_REPORT_MAP, BLE_GATT_CHR_F_READ, nullptr, nullptr),
    HID_CHR(kUuidHidControl, A_HID_CONTROL, BLE_GATT_CHR_F_WRITE_NO_RSP, nullptr, nullptr),
    HID_CHR(kUuidProtocolMode, A_PROTOCOL_MODE, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP, nullptr, nullptr),

    // The two Report characteristics. This is the whole point of the module:
    // both carry 0x2A4D, which ble_gatt_chr_def has no problem with.
    HID_CHR(kUuidReport, A_REPORT_KEYBOARD, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, s_dscKeyboard,
            &s_handleKeyboard),
    HID_CHR(kUuidReport, A_REPORT_CONSUMER, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, s_dscConsumer,
            &s_handleConsumer),

    {nullptr, nullptr, nullptr, nullptr, (ble_gatt_chr_flags)0, 0, nullptr, nullptr},
};

static const struct ble_gatt_chr_def s_deviceInfoCharacteristics[] = {
    HID_CHR(kUuidManufacturerName, A_MANUFACTURER, BLE_GATT_CHR_F_READ, nullptr, nullptr),
    HID_CHR(kUuidPnpId, A_PNP_ID, BLE_GATT_CHR_F_READ, nullptr, nullptr),
    {nullptr, nullptr, nullptr, nullptr, (ble_gatt_chr_flags)0, 0, nullptr, nullptr},
};

static const struct ble_gatt_chr_def s_batteryCharacteristics[] = {
    // Read + notify. The bridge does not measure anything itself, but the RC003
    // publishes its own charge over the standard battery service, so the value
    // is forwarded and the host is told about changes. Until the remote has
    // reported anything, this serves BRIDGE_BATTERY_LEVEL and nothing else.
    HID_CHR(kUuidBatteryLevel, A_BATTERY_LEVEL, BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, nullptr,
            &s_handleBattery),
    {nullptr, nullptr, nullptr, nullptr, (ble_gatt_chr_flags)0, 0, nullptr, nullptr},
};

static const struct ble_gatt_svc_def s_services[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, (const ble_uuid_t *)&kUuidHidService, nullptr, s_hidCharacteristics},
    {BLE_GATT_SVC_TYPE_PRIMARY, (const ble_uuid_t *)&kUuidDeviceInfoService, nullptr, s_deviceInfoCharacteristics},
    {BLE_GATT_SVC_TYPE_PRIMARY, (const ble_uuid_t *)&kUuidBatteryService, nullptr, s_batteryCharacteristics},
    {0, nullptr, nullptr, nullptr},
};

// ---------------------------------------------------------------------------
// Subscription tracking
// ---------------------------------------------------------------------------
int gapEvent(struct ble_gap_event *event, void *arg) {
  (void)arg;
  if (event->type != BLE_GAP_EVENT_SUBSCRIBE) return 0;

  const uint16_t attr = event->subscribe.attr_handle;
  const bool on = event->subscribe.cur_notify != 0 || event->subscribe.cur_indicate != 0;

  if (attr == s_handleKeyboard) {
    s_subKeyboard = on;
    s_connHandle = event->subscribe.conn_handle;
    BR_LOGI(kTag, "report %u (keyboard) notifications %s", HID_REPORT_ID_KEYBOARD, on ? "ENABLED" : "disabled");
  } else if (attr == s_handleConsumer) {
    s_subConsumer = on;
    s_connHandle = event->subscribe.conn_handle;
    BR_LOGI(kTag, "report %u (consumer) notifications %s", HID_REPORT_ID_CONSUMER, on ? "ENABLED" : "disabled");
  } else if (attr == s_handleBattery) {
    s_subBattery = on;
    s_connHandle = event->subscribe.conn_handle;
    BR_LOGI(kTag, "battery level notifications %s", on ? "ENABLED" : "disabled");
  } else {
    // The battery or some other characteristic; nothing to track.
    BR_LOGD(kTag, "subscribe event on handle %u", (unsigned)attr);
  }

  return 0;
}

bool notifyOn(uint16_t handle, bool subscribed, const uint8_t *data, size_t len, uint8_t *mirror) {
  if (!subscribed || handle == 0 || s_connHandle == BLE_HS_CONN_HANDLE_NONE) return false;

  // Keep what was last sent so a host read of the Report characteristic returns
  // the current report rather than a zeroed buffer.
  if (mirror != nullptr && len <= (size_t)UINT16_MAX) {
    memcpy(mirror, data, len);
  }

  struct os_mbuf *om = ble_hs_mbuf_from_flat(data, (uint16_t)len);
  if (om == nullptr) {
    BR_LOGW(kTag, "out of mbufs, report dropped");
    return false;
  }

  // Consumes the mbuf whether or not it succeeds.
  const int rc = ble_gatts_notify_custom(s_connHandle, handle, om);
  if (rc != 0) {
    BR_LOGW(kTag, "notify on handle %u failed: rc=%d", (unsigned)handle, rc);
    return false;
  }
  return true;
}

}  // namespace

namespace hid_gatt {

bool begin() {
  if (s_registered) {
    BR_LOGW(kTag, "HID GATT services already registered");
    return true;
  }

  int rc = ble_gatts_count_cfg(s_services);
  if (rc != 0) {
    BR_LOGE(kTag, "ble_gatts_count_cfg failed: rc=%d", rc);
    return false;
  }

  rc = ble_gatts_add_svcs(s_services);
  if (rc != 0) {
    BR_LOGE(kTag, "ble_gatts_add_svcs failed: rc=%d", rc);
    return false;
  }

  // Non-fatal: without the listener the reports are still sent, only the
  // subscription log lines are lost.
  rc = ble_gap_event_listener_register(&s_listener, gapEvent, nullptr);
  if (rc != 0) {
    BR_LOGW(kTag, "gap listener registration failed: rc=%d", rc);
  }

  s_registered = true;
  BR_LOGI(kTag, "HID services registered: report %u keyboard (%u B) + report %u consumer (%u B), map %u B",
          HID_REPORT_ID_KEYBOARD, HID_KB_REPORT_LEN, HID_REPORT_ID_CONSUMER, HID_CONSUMER_REPORT_LEN,
          (unsigned)s_reportMapLen);
  return true;
}

void setReportMap(const uint8_t *map, size_t len) {
  s_reportMap = map;
  s_reportMapLen = len;
}

void setManufacturer(const char *name) {
  if (name == nullptr) return;
  strncpy(s_manufacturer, name, sizeof(s_manufacturer) - 1);
  s_manufacturer[sizeof(s_manufacturer) - 1] = '\0';
}

void setPnpId(uint8_t vendorIdSource, uint16_t vendorId, uint16_t productId, uint16_t productVersion) {
  s_pnpId[0] = vendorIdSource;
  s_pnpId[1] = (uint8_t)(vendorId & 0xFF);
  s_pnpId[2] = (uint8_t)((vendorId >> 8) & 0xFF);
  s_pnpId[3] = (uint8_t)(productId & 0xFF);
  s_pnpId[4] = (uint8_t)((productId >> 8) & 0xFF);
  s_pnpId[5] = (uint8_t)(productVersion & 0xFF);
  s_pnpId[6] = (uint8_t)((productVersion >> 8) & 0xFF);
}

void setBatteryLevel(uint8_t level) {
  if (level > 100) level = 100;

  const bool changed = level != s_batteryLevel;
  s_batteryLevel = level;
  if (!changed) return;

  BR_LOGI(kTag, "battery level -> %u%%", (unsigned)level);

  // No mirror buffer: the read callback serves s_batteryLevel directly.
  notifyOn(s_handleBattery, s_subBattery, &s_batteryLevel, 1, nullptr);
}

bool notifyKeyboard(const uint8_t *data, size_t len) {
  return notifyOn(s_handleKeyboard, s_subKeyboard, data, len, s_lastKeyboard);
}

bool notifyConsumer(const uint8_t *data, size_t len) {
  return notifyOn(s_handleConsumer, s_subConsumer, data, len, s_lastConsumer);
}

bool keyboardSubscribed() { return s_subKeyboard; }
bool consumerSubscribed() { return s_subConsumer; }

void resetSubscriptions() {
  s_subKeyboard = false;
  s_subConsumer = false;
  s_subBattery = false;
  s_connHandle = BLE_HS_CONN_HANDLE_NONE;
}

}  // namespace hid_gatt
