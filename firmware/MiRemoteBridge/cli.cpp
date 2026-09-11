/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * cli.cpp - line based serial console
 *
 * The console exists so that every step of the bring-up (scan, pair, connect,
 * subscribe, recover) can be driven and observed over the same serial port the
 * firmware logs to - no GUI, no guessing.
 *
 * SPDX-License-Identifier: MIT
 */

#include "cli.h"

#include <Arduino.h>
#include <string.h>
#include <strings.h>

#include "ble_bonds.h"
#include "reset_button.h"
#include "bridge.h"
#include "config.h"
#include "event_bus.h"
#include "hid_server.h"
#include "key_definitions.h"
#include "keymap.h"
#include "log.h"
#include "rc003_client.h"
#include "selftest.h"
#include "settings.h"

namespace {

static const char *kTag = "CLI";

char s_line[BRIDGE_CONSOLE_LINE_MAX];
size_t s_len = 0;
bool s_overflow = false;

constexpr int kMaxTokens = 5;

int tokenize(char *line, char *argv[], int maxTokens) {
  int count = 0;
  char *p = line;
  while (*p && count < maxTokens) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '\0') break;
    argv[count++] = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
    if (*p) *p++ = '\0';
  }
  return count;
}

bool parseBool(const char *s, bool *out) {
  if (!s) return false;
  if (!strcasecmp(s, "on") || !strcasecmp(s, "1") || !strcasecmp(s, "true")) {
    *out = true;
    return true;
  }
  if (!strcasecmp(s, "off") || !strcasecmp(s, "0") || !strcasecmp(s, "false")) {
    *out = false;
    return true;
  }
  return false;
}

uint8_t parseAddrType(const char *s) {
  if (s && !strcasecmp(s, "random")) return BLE_ADDR_RANDOM;
  return BLE_ADDR_PUBLIC;
}

void printHelp() {
  Serial.println();
  Serial.println("MiRemoteBridge console commands");
  Serial.println("  help | ?                     this help");
  Serial.println("  status                       full state dump");
  Serial.println("  scan                         list devices seen by the last scan");
  Serial.println("  scan now                     start a fresh scan");
  Serial.println("  connect <index|mac> [public|random]  connect; index comes from `scan`");
  Serial.println("  reconnect                    drop the RC003 link and reconnect");
  Serial.println("  forget rc                    delete the RC003 bond + saved address");
  Serial.println("  forget win                   delete the host bond and re-advertise");
  Serial.println("  bond list                    list bonds held by the NimBLE store");
  Serial.println("  key <hex> press|release      inject a synthetic key (HID path test)");
  Serial.println("  channel <report|consumer> <hex>  raw report write test");
  Serial.println("  map                          show the runtime key selections");
  Serial.println("  map back  <consumer_back|kb_esc|kb_alt_left>");
  Serial.println("  map power <kb_alt_f4|consumer_sleep|consumer_power|kb_esc>");
  Serial.println("  map voice <kb_ralt_comma|consumer_mute|disabled>");
  Serial.println("  raw on|off                   raw report logging");
  Serial.println("  lat on|off                   latency logging");
  Serial.println("  log <0-4>                    log level (0 off .. 4 debug)");
  Serial.println("  selftest                     run the on-device vector + dispatch tests");
  Serial.println("  sim                          run only the dispatch simulation");
  Serial.println("  reboot | factory             restart / wipe bonds and settings");
  Serial.println();
}

void printMap() {
  Serial.printf("back  = %s\n", keymap_back_mode_name(keymap_get_back_mode()));
  Serial.printf("power = %s\n", keymap_power_mode_name(keymap_get_power_mode()));
  Serial.printf("voice = %s\n", keymap_voice_mode_name(keymap_get_voice_mode()));
}

void printScan() {
  const size_t n = rc003_client::nearbyCount();
  Serial.printf("%u device(s) seen by the scanner:\n", (unsigned)n);
  for (size_t i = 0; i < n; i++) {
    String addr;
    String name;
    uint8_t type = 0;
    int rssi = 0;
    if (!rc003_client::nearbyAt(i, &addr, &type, &name, &rssi)) continue;
    Serial.printf("  [%2u] %-17s  %-4s  %4d dBm  %s\n", (unsigned)i, addr.c_str(),
                  type == BLE_ADDR_RANDOM ? "rand" : "pub", rssi, name.length() ? name.c_str() : "(no name)");
  }
  if (n) {
    Serial.println("  pick one:  connect <index>     (or connect <mac> [public|random])");
  }
}

void printBonds() {
  BLEAddress peers[16];
  const int n = ble_bonds::list(peers, 16);
  if (n < 0) {
    Serial.println("bond list failed");
    return;
  }
  Serial.printf("%d bond(s):\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("  %s (type %u)\n", peers[i].toString().c_str(), (unsigned)peers[i].getType());
  }
}

void doKey(const char *hex, const char *verb) {
  if (!hex || !verb) {
    Serial.println("usage: key <hex> press|release");
    return;
  }
  const long value = strtol(hex, nullptr, 16);
  const bool pressed = (strcasecmp(verb, "press") == 0 || strcasecmp(verb, "down") == 0);
  event_bus::post(BR_EV_RC_KEY, (uint8_t)value, pressed);
  bridge::loop();
  Serial.printf("injected 0x%02X %s\n", (unsigned)value, pressed ? "down" : "up");
}

void doChannel(const char *which, const char *hex) {
  if (!which || !hex) {
    Serial.println("usage: channel <report|consumer> <hex>");
    return;
  }
  if (!strcasecmp(which, "consumer")) {
    hid_action_t a{};
    a.kind = HID_ACT_CONSUMER;
    a.consumer = (uint16_t)strtol(hex, nullptr, 16);
    hid_server::pressAction(a);
    delay(20);
    hid_server::releaseAction(a);
    Serial.printf("consumer 0x%04X tapped\n", (unsigned)a.consumer);
    return;
  }
  hid_action_t a{};
  a.kind = HID_ACT_KEYBOARD;
  a.keycode = (uint8_t)strtol(hex, nullptr, 16);
  hid_server::pressAction(a);
  delay(20);
  hid_server::releaseAction(a);
  Serial.printf("keyboard 0x%02X tapped\n", (unsigned)a.keycode);
}

void execute(char *line) {
  char *argv[kMaxTokens];
  const int argc = tokenize(line, argv, kMaxTokens);
  if (argc == 0) return;

  const char *cmd = argv[0];

  if (!strcasecmp(cmd, "help") || !strcasecmp(cmd, "?")) {
    printHelp();
    return;
  }

  if (!strcasecmp(cmd, "status")) {
    bridge::printStatus();
    return;
  }

  if (!strcasecmp(cmd, "scan")) {
    if (argc >= 2 && !strcasecmp(argv[1], "now")) {
      rc003_client::requestScanNow();
      Serial.println("scan requested");
    } else {
      printScan();
    }
    return;
  }

  if (!strcasecmp(cmd, "connect")) {
    if (argc < 2) {
      Serial.println("usage: connect <index|mac> [public|random]");
      Serial.println("       run `scan` first to get the index");
      return;
    }

    // `connect <index>` is the intended way in: read the list, type the number.
    // It carries the address type across too, so a random-address peer does not
    // have to be spelt out.
    String target(argv[1]);
    uint8_t type = (argc >= 3) ? parseAddrType(argv[2]) : BLE_ADDR_PUBLIC;
    String name;
    bool fromList = false;

    const char *raw = target.c_str();
    const bool allDigits = target.length() > 0 && target.length() <= 3 && raw[strspn(raw, "0123456789")] == '\0';
    if (allDigits) {
      const size_t index = (size_t)atoi(raw);
      if (!rc003_client::nearbyAt(index, &target, &type, &name, nullptr)) {
        Serial.printf("no scan entry [%u] - run `scan` first\n", (unsigned)index);
        return;
      }
      fromList = true;
    }

    if (target.length() != 17) {
      Serial.printf("\"%s\" is not a BLE address\n", argv[1]);
      return;
    }

    // A peer that is not advertising costs the library's full connect timeout to
    // give up on. Say so before the operator sits through the silence.
    if (!fromList && !rc003_client::isNearby(target)) {
      Serial.println("warning: this address was not seen in the last scan.");
      Serial.println("         if it is not advertising, the attempt blocks for ~30 s");
      Serial.println("         and then falls back to scanning.");
    }

    if (rc003_client::requestConnect(target, type, name)) {
      Serial.printf("connect requested: %s (type %u)\n", target.c_str(), (unsigned)type);
    } else {
      Serial.println("could not queue the connect request");
    }
    return;
  }

  if (!strcasecmp(cmd, "reconnect")) {
    rc003_client::requestReconnect();
    Serial.println("reconnect requested");
    return;
  }

  if (!strcasecmp(cmd, "forget")) {
    if (argc < 2) {
      Serial.println("usage: forget rc|win");
      return;
    }
    if (!strcasecmp(argv[1], "rc")) {
      rc003_client::requestForget();
      Serial.println("RC003 bond + saved address will be dropped");
    } else if (!strcasecmp(argv[1], "win")) {
      const int removed = hid_server::forgetHostBonds();
      hid_server::forceReAdvertise();
      Serial.printf("host bond records removed: %d, advertising restarted\n", removed);
      if (removed > 0) {
        Serial.println("Now delete the device in Windows (Settings > Bluetooth) and pair again.");
      }
    } else {
      Serial.println("usage: forget rc|win");
    }
    return;
  }

  if (!strcasecmp(cmd, "bond")) {
    if (argc >= 2 && !strcasecmp(argv[1], "list")) {
      printBonds();
    } else {
      Serial.println("usage: bond list");
    }
    return;
  }

  if (!strcasecmp(cmd, "key")) {
    doKey(argc >= 2 ? argv[1] : nullptr, argc >= 3 ? argv[2] : nullptr);
    return;
  }

  if (!strcasecmp(cmd, "channel")) {
    doChannel(argc >= 2 ? argv[1] : nullptr, argc >= 3 ? argv[2] : nullptr);
    return;
  }

  if (!strcasecmp(cmd, "map")) {
    if (argc < 3) {
      printMap();
      return;
    }
    const char *axis = argv[1];
    const char *value = argv[2];
    if (!strcasecmp(axis, "back")) {
      keymap_back_mode_t m;
      if (!keymap_back_mode_parse(value, &m)) {
        Serial.println("unknown back mode");
        return;
      }
      keymap_set_back_mode(m);
    } else if (!strcasecmp(axis, "power")) {
      keymap_power_mode_t m;
      if (!keymap_power_mode_parse(value, &m)) {
        Serial.println("unknown power mode");
        return;
      }
      keymap_set_power_mode(m);
    } else if (!strcasecmp(axis, "voice")) {
      keymap_voice_mode_t m;
      if (!keymap_voice_mode_parse(value, &m)) {
        Serial.println("unknown voice mode");
        return;
      }
      keymap_set_voice_mode(m);
    } else {
      Serial.println("usage: map back|power|voice <mode>");
      return;
    }
    settings::saveKeymapModes();
    printMap();
    return;
  }

  if (!strcasecmp(cmd, "raw")) {
    bool on = false;
    if (argc >= 2 && parseBool(argv[1], &on)) {
      brlog::setRawEnabled(on);
      settings::setRawLog(on);
      Serial.printf("raw logging %s\n", on ? "on" : "off");
    } else {
      Serial.println("usage: raw on|off");
    }
    return;
  }

  if (!strcasecmp(cmd, "lat")) {
    bool on = false;
    if (argc >= 2 && parseBool(argv[1], &on)) {
      brlog::setLatencyEnabled(on);
      settings::setLatencyLog(on);
      Serial.printf("latency logging %s\n", on ? "on" : "off");
    } else {
      Serial.println("usage: lat on|off");
    }
    return;
  }

  if (!strcasecmp(cmd, "log")) {
    if (argc >= 2) {
      const int lvl = atoi(argv[1]);
      brlog::setLevel((uint8_t)lvl);
      settings::setLogLevel((uint8_t)lvl);
      Serial.printf("log level %d\n", lvl);
    } else {
      Serial.printf("log level %u\n", (unsigned)brlog::level());
    }
    return;
  }

  if (!strcasecmp(cmd, "selftest")) {
    selftest::runAll();
    return;
  }

  if (!strcasecmp(cmd, "sim")) {
    selftest::runSimulation();
    return;
  }

  if (!strcasecmp(cmd, "factory")) {
    Serial.println("factory reset requested from console");
    reset_button::factoryResetAndReboot();  // wipes bonds + settings, reboots
    return;
  }

  if (!strcasecmp(cmd, "reboot")) {
    Serial.println("rebooting");
    delay(200);
    ESP.restart();
    return;
  }

  Serial.printf("unknown command \"%s\" - try `help`\n", cmd);
}

}  // namespace

namespace cli {

void begin() {
  s_len = 0;
  s_overflow = false;
  Serial.println();
  Serial.println("MiRemoteBridge console ready. Type `help` for the command list.");
}

void poll() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;

    if (c == '\r' || c == '\n') {
      if (s_overflow) {
        Serial.println("line too long, discarded");
        s_overflow = false;
        s_len = 0;
        continue;
      }
      if (s_len == 0) continue;
      s_line[s_len] = '\0';
      execute(s_line);
      s_len = 0;
      continue;
    }

    if (s_len + 1 >= sizeof(s_line)) {
      s_overflow = true;
      s_len = 0;
      continue;
    }
    s_line[s_len++] = (char)c;
  }

  if (s_overflow && Serial.available() == 0) {
    // Swallow the rest of the overlong line before reporting.
    s_overflow = false;
    s_len = 0;
    Serial.println("line too long, discarded");
  }
}

}  // namespace cli
