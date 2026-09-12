/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * native_subscription.cpp - allocation-free NimBLE CCCD subscription
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "native_subscription.h"

#include <BLEUtils.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>

#include "log.h"

namespace {

static const char *kTag = "CCCD";
static const ble_uuid16_t kCccdUuid = BLE_UUID16_INIT(0x2902);

struct DiscoveryContext {
  BLETaskData task;
  uint16_t conn;
  uint16_t valueHandle;
  uint16_t cccdHandle;

  DiscoveryContext(uint16_t connection, uint16_t value)
      : task(this), conn(connection), valueHandle(value), cccdHandle(0) {}
};

struct WriteContext {
  BLETaskData task;
  uint16_t conn;

  explicit WriteContext(uint16_t connection) : task(this), conn(connection) {}
};

int descriptorCallback(uint16_t conn, const ble_gatt_error *error,
                       uint16_t chrValueHandle, const ble_gatt_dsc *descriptor,
                       void *arg) {
  DiscoveryContext *context = static_cast<DiscoveryContext *>(arg);
  if (!context) return BLE_HS_EINVAL;
  if (!error) {
    BLEUtils::taskRelease(context->task, BLE_HS_EINVAL);
    return BLE_HS_EINVAL;
  }

  if (conn != context->conn) {
    BLEUtils::taskRelease(context->task, BLE_HS_ENOTCONN);
    return BLE_HS_ENOTCONN;
  }

  if (error->status != 0) {
    BLEUtils::taskRelease(context->task, error->status);
    return error->status;
  }

  if(chrValueHandle!=context->valueHandle) {BLEUtils::taskRelease(context->task,BLE_HS_EDONE);return BLE_HS_EDONE;}

  if (descriptor && chrValueHandle == context->valueHandle &&
      ble_uuid_cmp(&descriptor->uuid.u, &kCccdUuid.u) == 0) {
    context->cccdHandle = descriptor->handle;
    BLEUtils::taskRelease(context->task, BLE_HS_EDONE);
    return BLE_HS_EDONE;
  }

  return 0;
}

int writeCallback(uint16_t conn, const ble_gatt_error *error,
                  ble_gatt_attr *attribute, void *arg) {
  (void)attribute;
  WriteContext *context = static_cast<WriteContext *>(arg);
  if (!context) return BLE_HS_EINVAL;
  if (!error) {
    BLEUtils::taskRelease(context->task, BLE_HS_EINVAL);
    return BLE_HS_EINVAL;
  }

  const int status = conn == context->conn ? error->status : BLE_HS_ENOTCONN;
  BLEUtils::taskRelease(context->task, status);
  return 0;
}

}  // namespace

bool subscribeNative(uint16_t conn, uint16_t valueHandle, uint16_t endHandle,
                     bool notify) {
  if (valueHandle == 0 || valueHandle >= endHandle) {
    BR_LOGE(kTag, "invalid descriptor range value=%u end=%u",
            (unsigned)valueHandle, (unsigned)endHandle);
    return false;
  }

  DiscoveryContext discovery(conn, valueHandle);
  int rc = ble_gattc_disc_all_dscs(conn, valueHandle, endHandle,
                                   descriptorCallback, &discovery);
  if (rc != 0) {
    BR_LOGE(kTag, "descriptor discovery start failed rc=%d", rc);
    return false;
  }

  if (!BLEUtils::taskWait(discovery.task, BLE_NPL_TIME_FOREVER)) {
    BR_LOGE(kTag, "descriptor discovery wait failed");
    return false;
  }
  rc = discovery.task.m_flags;
  if (rc != BLE_HS_EDONE || discovery.cccdHandle == 0) {
    BR_LOGE(kTag, "CCCD discovery failed rc=%d handle=%u", rc,
            (unsigned)discovery.cccdHandle);
    return false;
  }

  const uint16_t cccdValue = notify ? 0x0001 : 0x0002;
  WriteContext write(conn);
  rc = ble_gattc_write_flat(conn, discovery.cccdHandle, &cccdValue,
                            sizeof(cccdValue), writeCallback, &write);
  if (rc != 0) {
    BR_LOGE(kTag, "CCCD write start failed rc=%d", rc);
    return false;
  }

  if (!BLEUtils::taskWait(write.task, BLE_NPL_TIME_FOREVER)) {
    BR_LOGE(kTag, "CCCD write wait failed");
    return false;
  }
  rc = write.task.m_flags;
  if (rc != 0 && rc != BLE_HS_EDONE) {
    BR_LOGE(kTag, "CCCD write failed rc=%d", rc);
    return false;
  }

  BR_LOGD(kTag, "subscribed value=%u cccd=%u mode=%s", (unsigned)valueHandle,
          (unsigned)discovery.cccdHandle, notify ? "notify" : "indicate");
  return true;
}
