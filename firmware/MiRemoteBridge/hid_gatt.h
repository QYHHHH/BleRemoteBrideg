/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_gatt.h - the downstream HID service, built directly on NimBLE
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

// Why this file exists instead of BLEHIDDevice
// ---------------------------------------------------------------------------
// HOGP maps one report ID to one Report characteristic, so a keyboard with
// media keys needs TWO characteristics that both carry UUID 0x2A4D, told apart
// by their Report Reference descriptor (0x2908).
//
// Arduino-ESP32 3.3.11's BLE wrapper cannot express that: BLEService keys its
// characteristics by UUID in a std::map<std::string, ...>, and
// addCharacteristic() drops the second one with a matching UUID, so it never
// reaches the GATT table. There is no way around it from the public API -
// BLEUUID::toString() renders 16-bit, 32-bit and 128-bit forms of 0x2A4D as the
// same string, so the map key cannot be made to differ, and executeCreate() is
// private.
//
// NimBLE itself has no such rule: ble_gatt_svc_def simply holds an array of
// characteristics, and duplicates are fine. So this module builds the HID
// service (plus Device Information and Battery) straight from NimBLE and leaves
// the rest of the firmware on the wrapper.
//
// Evidence, including the Windows error this fixed, is in docs/TESTING.md
// sections 4.5 and 4.6.
namespace hid_gatt {

// Registers the services with the NimBLE GATT server. Must be called after
// BLEDevice::init() and before the servers are started (BLEServer::start()).

bool begin();

// ---------------------------------------------------------------------------
// Values served to the host
// ---------------------------------------------------------------------------
// The report map is used by reference, so it has to stay alive; kHidReportMap in
// hid_report_map.h is a static const array and qualifies.
void setReportMap(const uint8_t *map, size_t len);
void setManufacturer(const char *name);
void setPnpId(uint8_t vendorIdSource, uint16_t vendorId, uint16_t productId, uint16_t productVersion);
// Publish the remote's charge as our own. Values above 100 are clamped. Notifies
// subscribed hosts on change; the read callback always serves the current value.
void setBatteryLevel(uint8_t level);

// ---------------------------------------------------------------------------
// Notifications
// ---------------------------------------------------------------------------
// Each returns false when no host is subscribed to that report, which is the
// normal state before Windows finishes pairing. The caller does not have to
// care: dropping a report nobody is listening to is not an error.
bool notifyKeyboard(const uint8_t *data, size_t len);
bool notifyConsumer(const uint8_t *data, size_t len);

bool keyboardSubscribed();
bool consumerSubscribed();

// Forgets the subscription state. Called on disconnect: an abrupt disconnect
// does not reliably produce a final unsubscribe event, and stale subscription
// state would make notify() look successful when nobody is there.
void resetSubscriptions();

}  // namespace hid_gatt
