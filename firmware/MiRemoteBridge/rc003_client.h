/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * rc003_client.h - upstream BLE central role (Xiaomi Bluetooth Remote 2 Pro)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

namespace rc003_client {

// Starts the background central task. Assumes ble_core::begin() has run.
bool begin();
void handleNotification(uint16_t conn, uint16_t handle, uint8_t *data, size_t length);
bool restoreActiveSlot();
// Slot operation: 0 select, 1 enable discovery, 2 delete device. Loop services
// the NVS/map handoff only after the central task has disconnected and paused.
bool requestSlot(uint8_t slot, uint8_t action);
bool discardUnassigned(const String &address, uint8_t type);
bool slotBusy();
const char *slotError();
void serviceSlot();

// ----- repair state --------------------------------------------------------
// Entered only when a stored link key is confirmed dead (see ble_core). While
// it is set the central task stops reconnecting, stops matching advertisements
// and never pairs on its own: the slot's bond, learned keys and shortcuts are
// all left untouched, and nothing is deleted until the user picks a device
// again from the page. Scanning continues so that device list can be filled.
void enterRepairRequired();
bool repairRequired();


// ----- console API ---------------------------------------------------------
// All of these are safe to call from the Arduino loop / console task; they only
// set request flags that the central task picks up on its next tick.
void requestScanNow();
void requestReconnect();
void requestForget();              // forget bond + saved address, then rescan
// `replacingDeadPairing` may only be true for a device the user picked out of
// the nearby list while repairRequired() is set; it is what authorises dropping
// the slot's old bond. The shortcuts and learned keys are never touched.
bool requestConnect(const String &address, uint8_t addrType, const String &name,
                    bool replacingDeadPairing = false);

// ----- status --------------------------------------------------------------
const char *stateName();
bool connected();
bool notified();                   // notifications are subscribed and flowing
String boundAddress();
String connectedAddress();
String connectedName();
String connectedIdentity();
int lastRssi();
int lastReportAgeMs();
uint32_t notifyCount();

// Charge reported by the remote, or -1 when it is not known yet (the remote
// either was not connected or never answered a battery read). 0 is a real
// value for a flat battery, so "unknown" has to be distinguishable from it.
int batteryLevel();

// Dump the cached advertisement table collected during scanning. Used by the
// `scan` console command; never returns "everything nearby", only what the
// scanner actually saw while it was running.
size_t nearbyCount();
bool nearbyAt(size_t index, String *address, uint8_t *addrType, String *name, int *rssi);

// Was this address among the results of the last scan? Used to warn before a
// direct connect: a peer that is not advertising costs the library's full
// connect timeout (30 s) to give up on.
bool isNearby(const String &address);

// Statistics for the selftest/status output.
uint32_t scanStarts();
uint32_t connectAttempts();
uint32_t connectSuccesses();
uint32_t subscriptionFailures();

}  // namespace rc003_client
