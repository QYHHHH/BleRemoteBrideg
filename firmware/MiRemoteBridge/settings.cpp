/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * settings.cpp - persisted configuration (NVS)
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings.h"

#include <Preferences.h>
#include <nimble/ble.h>

#include "config.h"
#include "keymap.h"
#include "log.h"

namespace {

static const char *kTag = "NVS";

Preferences s_prefs;

const char *kKeyRcAddr = "rc_addr";
const char *kKeyRcType = "rc_type";
const char *kKeyRcName = "rc_name";
const char *kKeyRaw = "log_raw";
const char *kKeyLat = "log_lat";
const char *kKeyLevel = "log_lvl";
const char *kKeyMapBack = "map_back";
const char *kKeyMapPower = "map_pow";
const char *kKeyMapVoice = "map_voice";
const char *kKeyBattery = "rc_batt";
const char *kKeyBatteryValid = "rc_batt_v";

bool s_ready = false;

}  // namespace

namespace settings {

void begin() {
  if (s_ready) return;
  s_ready = s_prefs.begin(BRIDGE_NVS_NAMESPACE, /*readOnly=*/false);
  if (!s_ready) {
    BR_LOGE(kTag, "failed to open NVS namespace \"%s\"", BRIDGE_NVS_NAMESPACE);
  }
}

bool hasRc003() { return s_ready && s_prefs.isKey(kKeyRcAddr); }

String rc003Address() { return s_ready ? s_prefs.getString(kKeyRcAddr, "") : String(""); }

uint8_t rc003AddrType() {
  return s_ready ? s_prefs.getUChar(kKeyRcType, BLE_ADDR_PUBLIC) : (uint8_t)BLE_ADDR_PUBLIC;
}

String rc003Name() { return s_ready ? s_prefs.getString(kKeyRcName, "") : String(""); }

void setRc003(const String &address, uint8_t addrType, const String &name) {
  if (!s_ready) return;
  s_prefs.putString(kKeyRcAddr, address);
  s_prefs.putUChar(kKeyRcType, addrType);
  s_prefs.putString(kKeyRcName, name);
  BR_LOGI(kTag, "saved remote %s (%s) type=%u", name.c_str(), address.c_str(), (unsigned)addrType);
}

void clearRc003() {
  if (!s_ready) return;
  s_prefs.remove(kKeyRcAddr);
  s_prefs.remove(kKeyRcType);
  s_prefs.remove(kKeyRcName);
}

void loadKeymapModes() {
  if (!s_ready) return;
  keymap_back_mode_t back = MAP_BACK_CONSUMER_BACK;
  keymap_power_mode_t power = MAP_POWER_ALT_F4;
  keymap_voice_mode_t voice = MAP_VOICE_RALT_COMMA;

  keymap_back_mode_parse(s_prefs.getString(kKeyMapBack, "consumer_back").c_str(), &back);
  keymap_power_mode_parse(s_prefs.getString(kKeyMapPower, "kb_alt_f4").c_str(), &power);
  keymap_voice_mode_parse(s_prefs.getString(kKeyMapVoice, "kb_ralt_comma").c_str(), &voice);

  keymap_set_back_mode(back);
  keymap_set_power_mode(power);
  keymap_set_voice_mode(voice);
}

void saveKeymapModes() {
  if (!s_ready) return;
  s_prefs.putString(kKeyMapBack, keymap_back_mode_name(keymap_get_back_mode()));
  s_prefs.putString(kKeyMapPower, keymap_power_mode_name(keymap_get_power_mode()));
  s_prefs.putString(kKeyMapVoice, keymap_voice_mode_name(keymap_get_voice_mode()));
}

bool rawLog() { return s_ready ? s_prefs.getBool(kKeyRaw, BRIDGE_LOG_RAW_DEFAULT) : false; }
void setRawLog(bool on) {
  if (s_ready) s_prefs.putBool(kKeyRaw, on);
}

bool latencyLog() {
  return s_ready ? s_prefs.getBool(kKeyLat, BRIDGE_LATENCY_LOG_DEFAULT) : BRIDGE_LATENCY_LOG_DEFAULT;
}
void setLatencyLog(bool on) {
  if (s_ready) s_prefs.putBool(kKeyLat, on);
}

uint8_t logLevel() { return s_ready ? s_prefs.getUChar(kKeyLevel, BR_LOG_INFO) : (uint8_t)BR_LOG_INFO; }
void setLogLevel(uint8_t level) {
  if (s_ready) s_prefs.putUChar(kKeyLevel, level);
}

// Last charge the remote reported, kept across reboots so the host never reads
// a fabricated placeholder after a cold boot. Windows reads the battery the
// moment it connects, which on a cold boot is before the remote has been
// re-discovered - without this it would see 100% every time until the next
// battery notification.
int batteryLevel() {
  if (!s_ready || !s_prefs.getBool(kKeyBatteryValid, false)) return -1;
  return (int)s_prefs.getUChar(kKeyBattery, 0);
}

void setBatteryLevel(uint8_t percent) {
  if (!s_ready) return;
  if (percent > 100) percent = 100;
  s_prefs.putUChar(kKeyBattery, percent);
  s_prefs.putBool(kKeyBatteryValid, true);
}

void clearAll() {
  if (!s_ready) return;
  s_prefs.clear();
}

}  // namespace settings
