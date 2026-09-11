/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_core.h - single point of BLE stack initialisation
 *
 * The ESP32-C3 build of Arduino-ESP32 3.3.11 ships exactly one BLE host stack
 * (NimBLE: CONFIG_BT_NIMBLE_ENABLED=y, Bluedroid is not built at all). The BLE
 * wrapper in the core therefore serves both roles from that one stack, and
 * BLEDevice::init() must be called exactly once for the whole firmware.
 *
 * Everything that needs the stack (the HID peripheral and the RC003 central)
 * assumes ble_core::begin() has already run.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>

namespace ble_core {

// Initialises the NimBLE host and configures Just-Works bonding.
// Safe to call more than once: subsequent calls are no-ops.
bool begin(const char *deviceName);

bool started();

// "nimble" on this target; reported on the console so the stack in use is
// never in doubt.
const char *stackName();

}  // namespace ble_core
