/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * cli.h - line based serial console
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace cli {

void begin();

// Non-blocking: consumes whatever bytes have arrived and runs one command per
// complete line. Call from loop().
void poll();

}  // namespace cli
