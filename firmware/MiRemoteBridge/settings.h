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
uint8_t activeSlot();
bool selectSlot(uint8_t slot);
bool slotInfo(uint8_t slot, String &address, String &name, uint8_t &type);
bool pairingEnabled();
void setPairingEnabled(bool enabled);
size_t learnedKeys(uint8_t *out, size_t cap);
bool learnKey(uint8_t raw);
String keyName(uint8_t raw);
bool renameKey(uint8_t raw, const String &name);
bool deleteKey(uint8_t raw);


// ---- RC003 (upstream) identity -------------------------------------------
bool hasRc003();
String rc003Address();
uint8_t rc003AddrType();
String rc003Name();
bool setRc003(const String &address, uint8_t addrType, const String &name);
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

// --- Web console password -------------------------------------------------
// Stored as SHA1(password) in NVS, never the plaintext. An empty store means
// "not set yet": the first visit is allowed to define it. `factory` (BOOT key
// held 5 s) clears the whole namespace, which is the documented way back in
// when the password is forgotten.
bool hasWebPassword();
bool checkWebPassword(const String &plain);
void setWebPassword(const String &plain);
void clearWebPassword();
// Opaque token handed to the page and required on the websocket URL (the
// browser cannot attach an Authorization header to a WebSocket).
String webToken();
String webPassHash();

// Wipe every key this module owns. Used by `factory`.
void clearAll();

}  // namespace settings
