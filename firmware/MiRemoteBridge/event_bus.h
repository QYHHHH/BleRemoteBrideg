/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * event_bus.h - the process-wide event queue shared by all BLE callbacks
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>

#include "bridge_events.h"

namespace event_bus {

// Called from BLE callback context (NimBLE host task). Never blocks.
bool post(uint8_t type, uint8_t code = 0, bool pressed = false);

// Called from the Arduino loop task only.
bool pop(bridge_event_t &out);

// Diagnostics.
uint32_t dropped();
size_t pending();

}  // namespace event_bus
