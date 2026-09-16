/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_server.h - downstream BLE HID peripheral role (what Windows sees)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

#include "keymap.h"

namespace hid_server {

// Brings up the HID GATT server and starts advertising. Must be called after
// BLEDevice::init() but it does not itself initialise the BLE stack.
bool begin();

// Send a HID report for a key-down / key-up of the given action.
// Both calls are idempotent: pressing an already-pressed action or releasing an
// already-released one emits nothing, which is what stops duplicated RC003
// reports from turning into key repeats on Windows.
void pressAction(const hid_action_t &action);
void releaseAction(const hid_action_t &action);

// Clear every key and push the all-zero reports. Safe to call at any time;
// emits nothing when nothing is down. This is the single most important
// function in the firmware for "no stuck key on Windows".
void releaseAll();

// True while at least one host is connected to our HID server.
bool hostConnected();
uint8_t hostCount();
bool slotSwitchSafe();

// Stop advertising, drop the host connection (if any) and start advertising
// again. Used after the Windows bond is deleted so the host is forced to pair
// again, and to recover from a wedged advertising state.
void forceReAdvertise();

// Advertise again if we are not currently advertising and nobody is connected.
void ensureAdvertising();

// Stop the stale-key reconnect loop until the user explicitly starts re-pairing.
void pauseHostPairing();
void resumeHostPairing();
bool hostPairingPaused();

// Forward the RC003's battery level to the host. Called from the bridge loop
// when the remote reports its charge; clamped to 0-100 and notified on change.
void setBatteryLevel(uint8_t percent);

// Delete the host bond so the next connection has to go through pairing again.
//
// When a host is connected its own record is removed. When none is connected
// every bond except the RC003's is removed -- the peripheral role only ever
// bonds with hosts, and "nothing is connected" is exactly the situation a host
// needing to be forgotten is usually in (it is wedged, or its own pairing was
// already deleted). Returns the number of bond records removed.
int forgetHostBonds();

const char *deviceName();

}  // namespace hid_server
