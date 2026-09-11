/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * settings.cpp - persisted configuration (NVS)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
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
const char *kKeyBindKeys = "bd_keys";  // legacy manifest, read for migration only
const char *kKeyBindingsV2 = "bd_v2";  // one atomic, versioned snapshot
const char *kKeyWifiSsid = "wf_ssid";
const char *kKeyWifiPass = "wf_pass";

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

// --- programmable key bindings -------------------------------------------
// Snapshot: version byte, count byte, then {raw, kind, mod, key, consLo,
// consHi} records. One checked NVS commit avoids a torn record/manifest pair.
// Legacy bd_keys/bd_XX remain readable until the first successful v2 write.

void loadBindings() {
  if (!s_ready) return;
  if (s_prefs.isKey(kKeyBindingsV2)) {
    uint8_t snapshot[2 + KEYMAP_MAX_BINDINGS * 6];
    const size_t size = s_prefs.getBytesLength(kKeyBindingsV2);
    if (size < 2 || size > sizeof(snapshot) ||
        s_prefs.getBytes(kKeyBindingsV2, snapshot, sizeof(snapshot)) != size ||
        snapshot[0] != 1 || snapshot[1] > KEYMAP_MAX_BINDINGS ||
        size != 2u + 6u * snapshot[1]) {
      BR_LOGE(kTag, "invalid binding snapshot, not loading stale legacy records");
      return;
    }
    for (size_t i = 0; i < snapshot[1]; ++i) {
      const uint8_t *r = snapshot + 2 + 6 * i;
      bool valid = r[0] && (r[1] == 1 ? (r[2] || r[3]) : (r[1] == 2 && (r[4] || r[5])));
      for (size_t j = 0; j < i; ++j) if (snapshot[2 + 6 * j] == r[0]) valid = false;
      if (!valid) { BR_LOGE(kTag, "invalid binding record; snapshot not applied"); return; }
    }
    for (size_t i = 0; i < snapshot[1]; ++i) {
      const uint8_t *r = snapshot + 2 + 6 * i;
      keymap_set_binding(r[0], r[1], r[2], r[3], (uint16_t)(r[4] | ((uint16_t)r[5] << 8)));
    }
    BR_LOGI(kTag, "loaded %u persisted binding(s)", (unsigned)snapshot[1]);
    return;
  }
  const size_t len = s_prefs.getBytesLength(kKeyBindKeys);
  if (len == 0) return;

  uint8_t raws[KEYMAP_MAX_BINDINGS];
  const size_t got = s_prefs.getBytes(kKeyBindKeys, raws, sizeof(raws));
  if (got == 0 || got > sizeof(raws)) {
    BR_LOGW(kTag, "binding manifest unreadable (%u bytes), ignoring", (unsigned)got);
    return;
  }

  for (size_t i = 0; i < got; i++) {
    char key[8];
    snprintf(key, sizeof(key), "bd_%02x", raws[i]);
    uint8_t rec[5] = {0};
    if (s_prefs.getBytes(key, rec, sizeof(rec)) != sizeof(rec)) continue;
    if (rec[0] == 0) continue;
    const uint16_t consumer = (uint16_t)(rec[3] | ((uint16_t)rec[4] << 8));
    keymap_set_binding(raws[i], rec[0], rec[1], rec[2], consumer);
  }
  BR_LOGI(kTag, "loaded %u programmable binding(s)", (unsigned)got);
}

bool setBinding(uint8_t raw, uint8_t kind, uint8_t modifier, uint8_t keycode, uint16_t consumer) {
  if (!s_ready || raw == 0 || kind > 2 ||
      (kind == 1 && modifier == 0 && keycode == 0) || (kind == 2 && consumer == 0)) return false;
  if (kind != 1) modifier = keycode = 0;
  if (kind != 2) consumer = 0;

  // Stage without touching the live map. The Arduino loop owns both mapping
  // changes and HID dispatch, so the final memory-only apply cannot race it.
  uint8_t raws[KEYMAP_MAX_BINDINGS];
  hid_action_t acts[KEYMAP_MAX_BINDINGS];
  size_t count = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
  size_t index = 0;
  while (index < count && raws[index] != raw) ++index;
  if (kind == 0) {
    if (index < count) { --count; raws[index] = raws[count]; acts[index] = acts[count]; }
  } else {
    if (index == count) {
      if (count == KEYMAP_MAX_BINDINGS) return false;
      ++count;
    }
    raws[index] = raw;
    acts[index] = {kind == 1 ? HID_ACT_KEYBOARD : HID_ACT_CONSUMER, modifier, keycode, consumer};
  }
  uint8_t snapshot[2 + KEYMAP_MAX_BINDINGS * 6] = {1, (uint8_t)count};
  for (size_t i = 0; i < count; ++i) {
    uint8_t *r = snapshot + 2 + 6 * i;
    r[0] = raws[i]; r[1] = (uint8_t)acts[i].kind; r[2] = acts[i].modifier;
    r[3] = acts[i].keycode; r[4] = (uint8_t)acts[i].consumer; r[5] = (uint8_t)(acts[i].consumer >> 8);
  }
  const size_t size = 2 + 6 * count;
  // Preferences::putBytes includes nvs_commit. A failure is never reported as
  // success, and never updates the runtime binding ahead of persistence.
  if (s_prefs.putBytes(kKeyBindingsV2, snapshot, size) != size) {
    BR_LOGE(kTag, "binding snapshot commit failed; live map unchanged");
    return false;
  }
  return keymap_set_binding(raw, kind, modifier, keycode, consumer);
}

String wifiSsid() { return s_ready ? s_prefs.getString(kKeyWifiSsid, "") : String(""); }

String wifiPassword() { return s_ready ? s_prefs.getString(kKeyWifiPass, "") : String(""); }

void setWifi(const String &ssid, const String &password) {
  if (!s_ready) return;
  s_prefs.putString(kKeyWifiSsid, ssid);
  s_prefs.putString(kKeyWifiPass, password);
  BR_LOGI(kTag, "saved Wi-Fi credentials for \"%s\"", ssid.c_str());
}

void clearWifi() {
  if (!s_ready) return;
  s_prefs.remove(kKeyWifiSsid);
  s_prefs.remove(kKeyWifiPass);
}

void clearAll() {
  if (!s_ready) return;
  s_prefs.clear();
}

}  // namespace settings
