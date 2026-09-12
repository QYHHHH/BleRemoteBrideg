/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * native_subscription.h - allocation-free NimBLE CCCD subscription
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

// Find this characteristic's 0x2902 descriptor and enable notifications or
// indications with a write response. Call from a normal FreeRTOS task, not a
// NimBLE host callback: this function waits for both GATT procedures.
bool subscribeNative(uint16_t conn, uint16_t valueHandle, uint16_t endHandle, bool notify);
