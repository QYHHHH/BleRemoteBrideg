/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_server.h - downstream BLE HID peripheral role (what Windows sees)
 *
 * SPDX-License-Identifier: MIT
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

// Stop advertising, drop the host connection (if any) and start advertising
// again. Used after the Windows bond is deleted so the host is forced to pair
// again, and to recover from a wedged advertising state.
void forceReAdvertise();

// Advertise again if we are not currently advertising and nobody is connected.
void ensureAdvertising();

// Delete the bond for the currently connected host so the next connection has
// to go through pairing again. Returns the number of bond records removed.
int forgetBondForConnectedHost();

const char *deviceName();

}  // namespace hid_server
