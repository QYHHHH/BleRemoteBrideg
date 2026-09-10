/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * settings.h - persisted configuration (NVS)
 *
 * Wrapped in one small module so the NVS keys and their defaults live in a
 * single place instead of being sprinkled across the BLE code.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <Arduino.h>
#include <stdbool.h>
#include <stdint.h>

namespace settings {

void begin();

// ---- RC003 (upstream) identity -------------------------------------------
bool hasRc003();
String rc003Address();
uint8_t rc003AddrType();
String rc003Name();
void setRc003(const String &address, uint8_t addrType, const String &name);
void clearRc003();

// ---- keymap modes ---------------------------------------------------------
void loadKeymapModes();
void saveKeymapModes();

// ---- console / logging ----------------------------------------------------
bool rawLog();
void setRawLog(bool on);

bool latencyLog();
void setLatencyLog(bool on);

uint8_t logLevel();
void setLogLevel(uint8_t level);

// Wipe every key this module owns. Used by `factory`.
void clearAll();

}  // namespace settings
