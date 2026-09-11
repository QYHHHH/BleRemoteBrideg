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
#include "web_page_gz.h"

namespace {

const char *kTag = "WIFI";
const char *kApSsid = "MiRemoteBridge";

// How the config page is reachable. Station mode is the default: the router
// owns DHCP and the client stays on its own network.
enum class Mode { Off, Station, Ap };

WebServer *s_server = nullptr;
bool s_enabled = false;
Mode s_mode = Mode::Off;

// How long `wifi on` waits for the association before giving up. The BLE link
// keeps running throughout, so this only paces the console command.
const uint32_t kJoinTimeoutMs = 15000;

// Short, log-friendly name for a Wi-Fi status code (the library's enum prints
// as a bare number otherwise).
const char *statusName(wl_status_t status) {
  switch (status) {
    case WL_NO_SSID_AVAIL:   return "network not found";
    case WL_CONNECT_FAILED:  return "wrong password?";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    case WL_IDLE_STATUS:     return "idle";
    default:                 return "unknown";
  }
}

// The network the page is currently reachable on.
String networkName() {
  return (s_mode == Mode::Ap) ? String(kApSsid) : settings::wifiSsid();
}

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
  out += jsonEscape(networkName());
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
  out += ",\"mode\":\"";
  out += wifi_ui::mode();
  out += "\",\"ip\":\"";
  out += wifi_ui::ip();
  out += "\"}";
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

// GET / - the page lives in PROGMEM.
//
// Serve the gzip-compressed copy when the client advertises gzip (browsers
// always do) and keep the plain page as a fallback. Regenerate the compressed
// copy with tests/tools/gen_web_page.py.
//
// KNOWN LIMITATION - the response is truncated (docs/TESTING.md 4.13.3).
// Measured on hardware: this handler returns the correct headers
// (Content-Encoding: gzip, Content-Length: 8257) but the body stops at
// exactly 5612 B on every attempt (5/5), i.e. 14259 of 21578 bytes decoded.
// The cause is in the write path, not the link: a 1252 B JSON response
// completes in 60 ms and /api/status in ~15 ms, while NetworkClient::write()
// gives up after WIFI_CLIENT_MAX_WRITE_RETRY and returns a PARTIAL count that
// send_P() ignores.
//
// Two hand-rolled flow-controlled replacements (retry the remainder, and
// retry in 512 B pieces under a wall-clock cap) were tried on hardware and
// BOTH made it worse - the client got no response at all. They were removed
// rather than left in the tree. Fixing this needs a proper investigation of
// the write path, not another guess.
void handleRoot() {
  const bool wantsGzip = s_server->hasHeader("Accept-Encoding") &&
                         s_server->header("Accept-Encoding").indexOf("gzip") >= 0;
  if (wantsGzip) {
    s_server->sendHeader("Content-Encoding", "gzip");
    s_server->sendHeader("Vary", "Accept-Encoding");
    s_server->send_P(200, "text/html", kIndexHtmlGz, kIndexHtmlGzLen);
    return;
  }
  s_server->send_P(200, "text/html", kIndexHtml);
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
  // The WebServer only captures request headers that were declared here
  // (its _headerKeysCount starts at 0 and nothing else is recorded). Without
  // this, header("Accept-Encoding") always returns "" and the page silently
  // falls back to the uncompressed copy, which is too big to send.
  static const char *kCollectedHeaders[] = {"Accept-Encoding"};
  s_server->collectHeaders(kCollectedHeaders, 1);
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

  const String ssid = settings::wifiSsid();
  if (ssid.isEmpty()) {
    BR_LOGE(kTag, "no Wi-Fi configured yet - run \"wifi join <ssid> <password>\" once, "
                  "or \"wifi ap on\" for the fallback access point");
    return false;
  }
  const String pass = settings::wifiPassword();

  const uint32_t heapBefore = ESP.getFreeHeap();
  BR_LOGI(kTag, "joining \"%s\" - the config page will be served on the LAN", ssid.c_str());
  BR_LOGI(kTag, "before Wi-Fi: heap %u (largest %u), host %d, rc003 %d, bonds %d",
          (unsigned)heapBefore, (unsigned)ESP.getMaxAllocHeap(),
          (int)hid_server::hostConnected(), (int)rc003_client::connected(), ble_bonds::count());

  // Station mode: the router runs DHCP, so the board needs no DHCP server and
  // the client never has to change networks. See wifi_ui.h for why.
  WiFi.mode(WIFI_STA);
  // Do NOT call WiFi.setSleep(false) here. It made the link more responsive,
  // but it also cost ~9 KB of heap (19.6 KB -> 10.5 KB free), and at 10.5 KB
  // the page stops being served at all. Heap is the scarce resource; latency
  // is not.
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  const uint32_t deadline = millis() + kJoinTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && (int32_t)(millis() - deadline) < 0) {
    delay(100);  // BLE keeps running; this only paces the console command
  }
  if (WiFi.status() != WL_CONNECTED) {
    BR_LOGE(kTag, "could not join \"%s\" (%s) - check the credentials, move closer, "
                  "or use \"wifi ap on\"",
            ssid.c_str(), statusName(WiFi.status()));
    WiFi.mode(WIFI_OFF);
    return false;
  }

  startServer();
  s_enabled = true;
  s_mode = Mode::Station;
  BR_LOGI(kTag, "config page: http://%s/ (heap %u B, Wi-Fi cost %u B)",
          WiFi.localIP().toString().c_str(), (unsigned)ESP.getFreeHeap(),
          (unsigned)(heapBefore - ESP.getFreeHeap()));
  return true;
}

// Fallback for when the router is out of range or its credentials are unknown.
// Kept deliberately small (one station slot) and documented as the slow path:
// the AP costs 36-55 KB and its DHCP server only answers above ~13-20 KB free.
bool enableAp() {
  if (s_enabled) return true;

  const uint32_t heapBefore = ESP.getFreeHeap();
  BR_LOGI(kTag, "starting fallback AP \"%s\" (prefer \"wifi on\" - the AP also has to "
                "run DHCP and the heap is tight while BLE is up)",
          kApSsid);
  BR_LOGI(kTag, "before AP: heap %u (largest %u), host %d, rc003 %d, bonds %d",
          (unsigned)heapBefore, (unsigned)ESP.getMaxAllocHeap(),
          (int)hid_server::hostConnected(), (int)rc003_client::connected(), ble_bonds::count());
  // Register before softAP() so the association events are not missed. This
  // core's onEvent() takes no event base; the callback filters by id.
  WiFi.onEvent(onApEvent);
  if (!WiFi.softAP(kApSsid, nullptr, 1, /*ssid_hidden=*/0, /*max_connection=*/1)) {
    BR_LOGE(kTag, "softAP failed");
    return false;
  }
  BR_LOGI(kTag, "AP up, heap free %u B (AP cost %u B), AP IP %s", (unsigned)ESP.getFreeHeap(),
          (unsigned)(heapBefore - ESP.getFreeHeap()), WiFi.softAPIP().toString().c_str());

  startServer();
  s_enabled = true;
  s_mode = Mode::Ap;
  BR_LOGI(kTag, "config page: http://%s/ (connect to the \"%s\" Wi-Fi network)",
          WiFi.softAPIP().toString().c_str(), kApSsid);
  return true;
}

bool disable() {
  if (!s_enabled) return true;
  if (s_server) {
    s_server->stop();
    delete s_server;
    s_server = nullptr;
  }
  const bool ok = (s_mode == Mode::Ap) ? WiFi.softAPdisconnect(true) : WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  s_enabled = false;
  s_mode = Mode::Off;
  BR_LOGI(kTag, "config UI stopped, heap back to %u B", (unsigned)ESP.getFreeHeap());
  return ok;
}

bool enabled() { return s_enabled; }

const char *mode() {
  switch (s_mode) {
    case Mode::Station: return "sta";
    case Mode::Ap:      return "ap";
    default:            return "off";
  }
}

unsigned stationCount() { return s_mode == Mode::Ap ? (unsigned)WiFi.softAPgetStationNum() : 0; }

const char *ip() {
  static char buf[16];
  if (!s_enabled) return "-";
  const IPAddress addr = (s_mode == Mode::Ap) ? WiFi.softAPIP() : WiFi.localIP();
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
  return buf;
}

void loop() {
  if (s_enabled && s_server) s_server->handleClient();
}

}  // namespace wifi_ui
