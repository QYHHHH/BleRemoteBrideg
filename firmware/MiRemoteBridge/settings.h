/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * settings.h - persisted configuration (NVS)
 *
 * Wrapped in one small module so the NVS keys and their defaults live in a
 * single place instead of being sprinkled across the BLE code.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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

// Last charge the remote reported, persisted across reboots so the host reads a
// real number right after a cold boot instead of a placeholder. Returns -1
// until the remote has reported at least once; 0 is a real (flat) reading.
int batteryLevel();
void setBatteryLevel(uint8_t percent);

// Programmable key bindings (Web UI / `bind` console command), persisted in
// NVS and replayed into the keymap at boot. kind: 0 = remove the binding,
// 1 = keyboard (modifier + keycode), 2 = consumer (usage). Setting with
// kind = 0 clears.
void loadBindings();
// Returns true only after the complete binding snapshot is committed to NVS.
bool setBinding(uint8_t raw, uint8_t kind, uint8_t modifier, uint8_t keycode, uint16_t consumer);

// ---- Wi-Fi credentials for the config UI ---------------------------------
// The config page is served over the local network: the bridge joins the
// existing Wi-Fi (station mode) so no access point and no DHCP server are
// needed on the board. Empty ssid means "not configured yet".
String wifiSsid();
String wifiPassword();
void setWifi(const String &ssid, const String &password);
void clearWifi();

// Wipe every key this module owns. Used by `factory`.
void clearAll();

}  // namespace settings
