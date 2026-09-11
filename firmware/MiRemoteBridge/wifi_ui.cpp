/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * wifi_ui.cpp - on-demand Wi-Fi AP + Web UI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "wifi_ui.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ble_bonds.h"
#include "config.h"
#include "hid_server.h"
#include "keymap.h"
#include "log.h"
#include "rc003_client.h"
#include "settings.h"
#include "web_page.h"

namespace {

const char *kTag = "WIFI";
const char *kApSsid = "MiRemoteBridge";

WebServer *s_server = nullptr;
bool s_enabled = false;

// ---------------------------------------------------------------------------
// JSON responses. Hand-rolled: the payloads are tiny and fixed-shape, and
// pulling in a JSON library for this would double the flash cost of the
// whole web module.
// ---------------------------------------------------------------------------

void sendJson(int code, const String &body) {
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->send(code, "application/json", body);
}

// Device names are remote-controlled input, not JSON source text.
String jsonEscape(const String &value) {
  String out;
  for (size_t i = 0; i < value.length(); ++i) {
    const unsigned char c = (unsigned char)value[i];
    if (c == '"' || c == '\\') {
      out += '\\';
      out += (char)c;
    } else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)c);
      out += escaped;
    } else {
      out += (char)c;
    }
  }
  return out;
}

void handleGetBindings() {
  uint8_t raws[KEYMAP_MAX_BINDINGS];
  hid_action_t acts[KEYMAP_MAX_BINDINGS];
  const size_t nb = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);

  String out = "{\"bindings\":[";
  for (size_t i = 0; i < nb; i++) {
    if (i) out += ',';
    out += "{\"raw\":";
    out += raws[i];
    out += ",\"kind\":";
    out += (acts[i].kind == HID_ACT_CONSUMER) ? 2 : 1;
    out += ",\"mod\":";
    out += acts[i].modifier;
    out += ",\"key\":";
    out += acts[i].keycode;
    out += ",\"cons\":";
    out += acts[i].consumer;
    out += '}';
  }
  out += "],\"defaults\":[";

  const keymap_entry_t *def = keymap_default_table();
  const size_t nd = keymap_default_count();
  for (size_t i = 0; i < nd; i++) {
    if (i) out += ',';
    out += "{\"raw\":";
    out += def[i].raw_code;
    out += ",\"kind\":";
    out += (def[i].press.kind == HID_ACT_CONSUMER) ? 2 : 1;
    out += ",\"mod\":";
    out += def[i].press.modifier;
    out += ",\"key\":";
    out += def[i].press.keycode;
    out += ",\"cons\":";
    out += def[i].press.consumer;
    out += '}';
  }
  // Report the actual action, including serial-selected runtime modes.
  // Read-only: the validated keymap and HID paths are not changed.
  out += "],\"effective\":[";
  for (size_t i = 0; i < nd; i++) {
    if (i) out += ',';
    const hid_action_t a = keymap_lookup(def[i].raw_code);
    out += "{\"raw\":";
    out += def[i].raw_code;
    out += ",\"kind\":";
    out += (a.kind == HID_ACT_NONE) ? 0 : ((a.kind == HID_ACT_CONSUMER) ? 2 : 1);
    out += ",\"mod\":";
    out += a.modifier;
    out += ",\"key\":";
    out += a.keycode;
    out += ",\"cons\":";
    out += a.consumer;
    out += '}';
  }
  out += "]}";

  sendJson(200, out);
}

void handleGetStatus() {
  String out = "{\"wifi\":true,\"ap\":\"";
  out += kApSsid;
  out += "\",\"hostConnected\":";
  out += (hid_server::hostConnected() ? "true" : "false");
  out += ",\"remoteConnected\":";
  out += (rc003_client::connected() ? "true" : "false");
  out += ",\"remoteName\":\"";
  out += jsonEscape(rc003_client::connectedName());
  out += "\",\"battery\":";
  out += settings::batteryLevel();
  out += ",\"bindings\":";
  out += keymap_binding_count();
  out += "}";
  sendJson(200, out);
}

// POST /api/set?raw=..&kind=..&mod=..&key=..&cons=..
// kind 1 = keyboard (mod+key), 2 = consumer (cons), 0 = remove the binding.
void handleSet() {
  if (!s_server->hasArg("raw") || !s_server->hasArg("kind")) {
    sendJson(400, "{\"error\":\"raw and kind are required\"}");
    return;
  }
  const long raw = strtol(s_server->arg("raw").c_str(), nullptr, 16);
  const long kind = s_server->arg("kind").toInt();
  if (raw <= 0 || raw > 255 || kind < 0 || kind > 2) {
    sendJson(400, "{\"error\":\"invalid raw or kind\"}");
    return;
  }
  const uint8_t mod = s_server->hasArg("mod") ? (uint8_t)s_server->arg("mod").toInt() : 0;
  const uint8_t key = s_server->hasArg("key") ? (uint8_t)s_server->arg("key").toInt() : 0;
  const uint16_t cons = s_server->hasArg("cons") ? (uint16_t)s_server->arg("cons").toInt() : 0;

  if (kind == 1 && key == 0 && mod == 0) {
    sendJson(400, "{\"error\":\"keyboard binding needs a key or a modifier\"}");
    return;
  }
  if (kind == 2 && cons == 0) {
    sendJson(400, "{\"error\":\"consumer binding needs a usage\"}");
    return;
  }

  settings::setBinding((uint8_t)raw, (uint8_t)kind, mod, key, cons);
  sendJson(200, "{\"ok\":true}");
}

// POST /api/reset - drop every programmable binding, back to the built-in map.
void handleReset() {
  uint8_t raws[KEYMAP_MAX_BINDINGS];
  hid_action_t acts[KEYMAP_MAX_BINDINGS];
  size_t n = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
  while (n > 0) {
    settings::setBinding(raws[0], 0, 0, 0, 0);
    n = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
  }
  sendJson(200, "{\"ok\":true}");
}

// GET / - the page is ~48 KB of PROGMEM.
//
// It must NOT go out through a single send_P(): that hands the whole buffer to
// lwIP at once, which needs a large contiguous block. With the AP up the free
// heap is only ~20 KB and the largest block ~7 KB, so the transfer dies half
// way and the server stops answering afterwards (measured on hardware
// 2026-09-11: page served 0 bytes, /api/bindings then silent, heap min 472 B).
// Stream it in 1 KB chunks with chunked transfer encoding instead, so the peak
// allocation is one chunk regardless of page size.
void handleRoot() {
  static const size_t kChunk = 1024;
  const size_t total = strlen_P(kIndexHtml);

  s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  s_server->send(200, "text/html", "");

  char buf[kChunk];
  for (size_t off = 0; off < total; off += kChunk) {
    if (!s_server->client().connected()) break;
    const size_t n = (total - off < kChunk) ? (total - off) : kChunk;
    memcpy_P(buf, kIndexHtml + off, n);
    s_server->sendContent(buf, n);
    // Give lwIP a chance to drain between chunks; without this the send queue
    // grows faster than the link and the heap goes with it.
    yield();
  }
  s_server->sendContent("", 0);  // terminating chunk
  // The library does not restore this itself (_finalizeResponse only closes
  // chunking), and leaving it UNKNOWN would make every later JSON response
  // chunked too.
  s_server->setContentLength(CONTENT_LENGTH_NOT_SET);
}

void handleNotFound() { s_server->send(404, "text/plain", "not found"); }

// AP-side visibility: without these lines "the phone cannot connect" is
// indistinguishable from "it associated but never got a lease". The IPASSIGNED
// event only fires when the AP's DHCP server actually hands out an address.
void onApEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      BR_LOGI(kTag, "station joined (aid %u)", (unsigned)info.wifi_ap_staconnected.aid);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      BR_LOGW(kTag, "station left (aid %u, reason %u)",
              (unsigned)info.wifi_ap_stadisconnected.aid, (unsigned)info.wifi_ap_stadisconnected.reason);
      break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      BR_LOGI(kTag, "station got an IP from the AP's DHCP server");
      break;
    default:
      break;
  }
}

void startServer() {
  s_server = new WebServer(80);
  s_server->on("/", HTTP_GET, handleRoot);
  s_server->on("/api/status", HTTP_GET, handleGetStatus);
  s_server->on("/api/bindings", HTTP_GET, handleGetBindings);
  s_server->on("/api/set", HTTP_POST, handleSet);
  s_server->on("/api/reset", HTTP_POST, handleReset);
  s_server->onNotFound(handleNotFound);
  s_server->begin();
}

}  // namespace

namespace wifi_ui {

void begin() {}

bool enable() {
  if (s_enabled) return true;

  BR_LOGI(kTag, "starting AP \"%s\" - config page at http://192.168.4.1/", kApSsid);
  // max_connection = 1: the page is used by one client at a time, and every
  // extra station slot costs RAM. With the AP up the free heap is only ~13 KB
  // (BLE has the rest), so keep the AP's own footprint as small as possible.
  // Register before softAP() so the association events are not missed. This
  // core's onEvent() takes no event base; the callback filters by id.
  WiFi.onEvent(onApEvent);
  if (!WiFi.softAP(kApSsid, nullptr, 1, /*ssid_hidden=*/0, /*max_connection=*/1)) {
    BR_LOGE(kTag, "softAP failed");
    return false;
  }
  BR_LOGI(kTag, "AP up, heap free %u B, AP IP %s", (unsigned)ESP.getFreeHeap(),
          WiFi.softAPIP().toString().c_str());

  startServer();
  s_enabled = true;
  const IPAddress ip = WiFi.softAPIP();
  BR_LOGI(kTag, "config page: http://%s/ (connect to the \"%s\" Wi-Fi network)",
          ip.toString().c_str(), kApSsid);
  return true;
}

bool disable() {
  if (!s_enabled) return true;
  if (s_server) {
    s_server->stop();
    delete s_server;
    s_server = nullptr;
  }
  const bool ok = WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_enabled = false;
  BR_LOGI(kTag, "AP stopped, heap back to %u B", (unsigned)ESP.getFreeHeap());
  return ok;
}

bool enabled() { return s_enabled; }

unsigned stationCount() { return s_enabled ? (unsigned)WiFi.softAPgetStationNum() : 0; }

const char *apIp() {
  static char buf[16];
  if (!s_enabled) return "-";
  const IPAddress ip = WiFi.softAPIP();
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  return buf;
}

void loop() {
  if (s_enabled && s_server) s_server->handleClient();
}

}  // namespace wifi_ui
