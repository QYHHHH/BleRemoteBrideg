/*
 * MiRemoteBridge - on-demand LAN configuration, cooperative HTTP transport.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * All configuration mutations and HID dispatch remain on the Arduino loop.
 * No HTTP handler may wait for TCP or association: the same loop must deliver
 * key releases. Assets remain flash-resident; neither a whole-asset malloc nor
 * an extra String copy is needed. A bounded socket state machine handles back
 * pressure and short writes across loop iterations.
 */
#include "wifi_ui.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_netif.h>
#include <lwip/sockets.h>
#include <lwip/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "ble_bonds.h"
#include "bridge.h"
#include "config.h"
#include "event_bus.h"
#include "hid_gatt.h"
#include "hid_server.h"
#include "keymap.h"
#include "log.h"
#include "rc003_client.h"
#include "settings.h"
#include "web_page_gz.h"

namespace {
const char *kTag = "WIFI";
const char *kApSsid = "MiRemoteBridge";
enum class Mode { Off, Station, Ap };
Mode s_mode = Mode::Off;
bool s_enabled = false;
bool s_hadAddress = false;
bool s_eventsRegistered = false;
uint32_t s_joinStarted = 0;
uint32_t s_retryAt = 0;
int s_listener = -1;
// Always-on policy (user requirement): with credentials stored, the config UI
// comes up by itself after boot and cannot be turned off. The start is
// deferred a few seconds so the BLE links - the product's actual job - are
// established before the Wi-Fi stack takes its share of heap and airtime.
bool s_autoStartPending = false;
constexpr uint32_t kAutoStartDelayMs = 8000;

// One active HTTP exchange, bounded accept backlog. Browsers may queue asset
// requests; they do not allocate unbounded application buffers on the MCU.
constexpr size_t kHeaderLimit = 1536;
constexpr size_t kIoCapacity = 3072;
constexpr size_t kIoPerLoop = 384;
constexpr uint32_t kProgressTimeoutMs = 4000;
constexpr uint32_t kRequestTimeoutMs = 15000;
struct Exchange {
  int fd = -1;
  bool sending = false;
  char io[kIoCapacity];        // request headers, then JSON response (reused)
  char header[320];
  const char *body = nullptr;  // flash asset, literal, or io[]
  size_t used = 0;
  size_t headerLen = 0;
  size_t headerSent = 0;
  size_t bodyLen = 0;
  size_t bodySent = 0;
  uint32_t started = 0;
  uint32_t progress = 0;
};
Exchange s_http;
uint32_t s_requests = 0;
uint32_t s_completed = 0;
uint32_t s_aborted = 0;
uint32_t s_backPressure = 0;

class Json {
 public:
  explicit Json(char *buf, size_t capacity) : buf_(buf), cap_(capacity) { buf_[0] = 0; }
  void add(const char *fmt, ...) {
    if (!ok_) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf_ + len_, cap_ - len_, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap_ - len_) { ok_ = false; return; }
    len_ += (size_t)n;
  }
  void quoted(const char *value) {
    add("\"");
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
      if (*p == '"' || *p == '\\') add("\\%c", *p);
      else if (*p < 0x20) add("\\u%04x", (unsigned)*p);
      else add("%c", *p);
    }
    add("\"");
  }
  bool ok() const { return ok_; }
  size_t size() const { return len_; }
 private:
  char *buf_;
  size_t cap_;
  size_t len_ = 0;
  bool ok_ = true;
};

const char *boolean(bool v) { return v ? "true" : "false"; }

void closeExchange(bool complete) {
  if (s_http.fd >= 0) {
    ::close(s_http.fd);
    if (complete) ++s_completed;
    else ++s_aborted;
  }
  s_http.fd = -1;
  s_http.body = nullptr;
  s_http.used = 0;
  s_http.sending = false;
}

void stopHttp() {
  closeExchange(false);
  if (s_listener >= 0) ::close(s_listener);
  s_listener = -1;
}

bool nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool startHttp() {
  if (s_listener >= 0) return true;
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return false;
  const int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(80);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (!nonblocking(fd) || bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 1) != 0) {
    BR_LOGE(kTag, "HTTP listen failed, errno %d", errno);
    ::close(fd);
    return false;
  }
  s_listener = fd;
  BR_LOGI(kTag, "config page: http://%s/ (mode %s, heap %u B, largest %u B)",
          wifi_ui::ip(), wifi_ui::mode(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return true;
}

void respond(int code, const char *reason, const char *type, const char *body, size_t len,
             bool gzip = false, bool head = false) {
  const int n = snprintf(s_http.header, sizeof(s_http.header),
      "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\n"
      "Connection: keep-alive\r\nKeep-Alive: timeout=5, max=100\r\n"
      "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n%s\r\n",
      code, reason, type, (unsigned)len, gzip ? "Content-Encoding: gzip\r\nVary: Accept-Encoding\r\n" : "");
  if (n < 0 || (size_t)n >= sizeof(s_http.header)) { closeExchange(false); return; }
  s_http.headerLen = (size_t)n;
  s_http.headerSent = 0;
  s_http.body = body;
  s_http.bodyLen = head ? 0 : len;
  s_http.bodySent = 0;
  s_http.sending = true;
  s_http.progress = millis();
}

void jsonResult(Json &out, bool head = false) {
  if (!out.ok()) {
    const char *err = "{\"error\":\"response buffer exhausted\"}";
    respond(503, "Service Unavailable", "application/json", err, strlen(err));
    return;
  }
  respond(200, "OK", "application/json", s_http.io, out.size(), false, head);
}

void errorResponse(int code, const char *reason, const char *error) {
  Json out(s_http.io, sizeof(s_http.io));
  out.add("{\"error\":"); out.quoted(error); out.add("}");
  respond(code, reason, "application/json", s_http.io, out.size());
}

void actionJson(Json &out, uint8_t raw, const hid_action_t &a) {
  out.add("{\"raw\":%u,\"kind\":%u,\"mod\":%u,\"key\":%u,\"cons\":%u}",
      raw, (unsigned)a.kind, a.modifier, a.keycode, a.consumer);
}

void handleBindings(bool head) {
  uint8_t raws[KEYMAP_MAX_BINDINGS];
  hid_action_t acts[KEYMAP_MAX_BINDINGS];
  const size_t count = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
  Json out(s_http.io, sizeof(s_http.io));
  out.add("{\"bindings\":[");
  for (size_t i = 0; i < count; ++i) {
    if (i) out.add(",");
    actionJson(out, raws[i], acts[i]);
  }
  const keymap_entry_t *defs = keymap_default_table();
  const size_t n = keymap_default_count();
  out.add("],\"defaults\":[");
  for (size_t i = 0; i < n; ++i) {
    if (i) out.add(",");
    actionJson(out, defs[i].raw_code, defs[i].press);
  }
  out.add("],\"effective\":[");
  for (size_t i = 0; i < n; ++i) {
    if (i) out.add(",");
    actionJson(out, defs[i].raw_code, keymap_lookup(defs[i].raw_code));
  }
  out.add("]}");
  jsonResult(out, head);
}

void handleStatus(bool head) {
  Json out(s_http.io, sizeof(s_http.io));
  out.add("{\"wifi\":true,\"ap\":");
  const String network = s_mode == Mode::Ap ? String(kApSsid) : settings::wifiSsid();
  out.quoted(network.c_str());
  out.add(",\"hostConnected\":%s,\"hostCount\":%u,\"keyboardSubscribed\":%s,\"consumerSubscribed\":%s",
      boolean(hid_server::hostConnected()), hid_server::hostCount(),
      boolean(hid_gatt::keyboardSubscribed()), boolean(hid_gatt::consumerSubscribed()));
  out.add(",\"remoteConnected\":%s,\"remoteState\":", boolean(rc003_client::connected()));
  out.quoted(rc003_client::stateName());
  out.add(",\"remoteName\":");
  const String remote = rc003_client::connectedName();
  out.quoted(remote.c_str());
  out.add(",\"battery\":%d,\"bindings\":%u,\"mode\":\"%s\",\"ip\":\"%s\",\"uptimeMs\":%lu",
      settings::batteryLevel(), (unsigned)keymap_binding_count(), wifi_ui::mode(), wifi_ui::ip(),
      (unsigned long)millis());
  out.add(",\"heapFree\":%u,\"heapMin\":%u,\"heapLargest\":%u,\"activeKey\":%u",
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
      (unsigned)ESP.getMaxAllocHeap(), bridge::activeRawCode());
  out.add(",\"notifications\":%lu,\"events\":%lu,\"queuePending\":%u,\"queueDropped\":%lu",
      (unsigned long)rc003_client::notifyCount(), (unsigned long)bridge::eventsHandled(),
      (unsigned)event_bus::pending(), (unsigned long)event_bus::dropped());
  out.add(",\"httpRequests\":%lu,\"httpCompleted\":%lu,\"httpAborted\":%lu,\"httpBackPressure\":%lu}",
      (unsigned long)s_requests, (unsigned long)s_completed, (unsigned long)s_aborted,
      (unsigned long)s_backPressure);
  jsonResult(out, head);
}

// Every numeric field is parsed in full, before narrowing to HID-sized types.
// raw uses hex; all other fields use decimal. Duplicate/unknown fields fail.
bool bindingArgs(char *query, uint8_t &raw, uint8_t &kind, uint8_t &mod, uint8_t &key, uint16_t &cons) {
  unsigned mask = 0;
  raw = kind = mod = key = 0; cons = 0;
  char *save = nullptr;
  for (char *item = strtok_r(query, "&", &save); item; item = strtok_r(nullptr, "&", &save)) {
    char *eq = strchr(item, '=');
    if (!eq) return false;
    *eq++ = 0;
    int field = -1;
    const char *names[] = {"raw", "kind", "mod", "key", "cons"};
    for (int i = 0; i < 5; ++i) if (strcmp(item, names[i]) == 0) field = i;
    if (field < 0 || (mask & (1u << field)) || !*eq || *eq == '-' || *eq == '+') return false;
    char *end = nullptr;
    errno = 0;
    const unsigned long value = strtoul(eq, &end, field == 0 ? 16 : 10);
    const unsigned max = field == 1 ? 2 : (field == 4 ? 65535 : 255);
    if (errno || end == eq || *end || value > max) return false;
    mask |= 1u << field;
    switch (field) {
      case 0: raw = (uint8_t)value; break;
      case 1: kind = (uint8_t)value; break;
      case 2: mod = (uint8_t)value; break;
      case 3: key = (uint8_t)value; break;
      case 4: cons = (uint16_t)value; break;
    }
  }
  if ((mask & 3) != 3 || raw == 0) return false;
  bool known = false;
  const keymap_entry_t *defs = keymap_default_table();
  for (size_t i = 0; i < keymap_default_count(); ++i) if (defs[i].raw_code == raw) known = true;
  if (!known) return false;
  if (kind == 1) { cons = 0; return key || mod; }
  if (kind == 2) { mod = key = 0; return cons != 0; }
  mod = key = 0; cons = 0;
  return true;
}

void handleSet(char *query) {
  uint8_t raw, kind, mod, key;
  uint16_t cons;
  if (!bindingArgs(query, raw, kind, mod, key, cons)) {
    errorResponse(400, "Bad Request", "invalid binding (raw is hex; numeric fields must be in range)");
    return;
  }
  if (!keymap_has_binding(raw) && kind && keymap_binding_count() >= KEYMAP_MAX_BINDINGS) {
    errorResponse(409, "Conflict", "binding table is full"); return;
  }
  if (!settings::setBinding(raw, kind, mod, key, cons)) {
    errorResponse(500, "Internal Server Error", "binding was not persisted"); return;
  }
  respond(200, "OK", "application/json", "{\"ok\":true}", 11);
}

void handleReset() {
  uint8_t raws[KEYMAP_MAX_BINDINGS];
  hid_action_t acts[KEYMAP_MAX_BINDINGS];
  const size_t n = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
  for (size_t i = 0; i < n; ++i) {
    if (!settings::setBinding(raws[i], 0, 0, 0, 0)) {
      errorResponse(500, "Internal Server Error", "could not persist reset; refresh to inspect remaining bindings");
      return;
    }
  }
  respond(200, "OK", "application/json", "{\"ok\":true}", 11);
}

void dispatch() {
  ++s_requests;
  // Save the small request target before reusing the input buffer for JSON.
  char method[8], target[384], version[12];
  if (sscanf(s_http.io, "%7s %383s %11s", method, target, version) != 3 ||
      (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0)) {
    errorResponse(400, "Bad Request", "invalid request line"); return;
  }
  bool crossSite = false, nonemptyBody = false;
  char host[96] = {}, origin[128] = {};
  for (char *line = strstr(s_http.io, "\r\n"); line && line[2];) {
    line += 2;
    char *end = strstr(line, "\r\n");
    if (!end || end == line) break;
    const char saved = *end; *end = 0;
    if (strncasecmp(line, "Host:", 5) == 0) sscanf(line + 5, "%95s", host);
    if (strncasecmp(line, "Origin:", 7) == 0) sscanf(line + 7, "%127s", origin);
    if (strncasecmp(line, "Sec-Fetch-Site:", 15) == 0 && strstr(line + 15, "cross-site")) crossSite = true;
    if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) nonemptyBody = true;
    if (strncasecmp(line, "Content-Length:", 15) == 0) {
      const char *v = line + 15; while (*v == ' ' || *v == '\t') ++v;
      if (strcmp(v, "0") != 0) nonemptyBody = true;
    }
    *end = saved; line = end;
  }
  const bool get = strcmp(method, "GET") == 0;
  const bool head = strcmp(method, "HEAD") == 0;
  const bool post = strcmp(method, "POST") == 0;
  if (!get && !head && !post) { errorResponse(405, "Method Not Allowed", "use GET, HEAD or POST"); return; }
  if (post) {
    char expected[104]; snprintf(expected, sizeof(expected), "http://%s", host);
    if (crossSite || (origin[0] && strcmp(origin, expected) != 0)) {
      errorResponse(403, "Forbidden", "cross-origin writes are not allowed"); return;
    }
    if (nonemptyBody) { errorResponse(400, "Bad Request", "use URL query parameters with an empty POST body"); return; }
  }
  char *query = strchr(target, '?');
  if (query) *query++ = 0;
  if (get || head) {
    if (strcmp(target, "/") == 0) respond(200, "OK", "text/html; charset=utf-8", kIndexHtmlGz, kIndexHtmlGzLen, true, head);
    else if (strcmp(target, "/app.css") == 0) respond(200, "OK", "text/css; charset=utf-8", kIndexCssGz, kIndexCssGzLen, true, head);
    else if (strcmp(target, "/app.js") == 0) respond(200, "OK", "application/javascript; charset=utf-8", kIndexJsGz, kIndexJsGzLen, true, head);
    else if (strcmp(target, "/api/status") == 0) handleStatus(head);
    else if (strcmp(target, "/api/bindings") == 0) handleBindings(head);
    else if (strcmp(target, "/favicon.ico") == 0) respond(204, "No Content", "image/x-icon", "", 0);
    else if (strcmp(target, "/api/set") == 0 || strcmp(target, "/api/reset") == 0)
      errorResponse(405, "Method Not Allowed", "writes require POST");
    else errorResponse(404, "Not Found", "not found");
  } else if (strcmp(target, "/api/set") == 0) {
    if (!query) errorResponse(400, "Bad Request", "binding parameters required");
    else handleSet(query);
  } else if (strcmp(target, "/api/reset") == 0) handleReset();
  else errorResponse(404, "Not Found", "not found");
}

bool retryable(int error) {
  return error == EAGAIN || error == EWOULDBLOCK || error == EINTR || error == ENOMEM || error == ENOBUFS;
}

void pollHttp() {
  if (s_listener < 0) return;
  if (s_http.fd < 0) {
    const int fd = accept(s_listener, nullptr, nullptr);
    if (fd < 0) return;
    if (!nonblocking(fd)) { ::close(fd); return; }
    const int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // Linger 0: close() sends RST instead of starting a graceful shutdown.
    // The response bytes are already gone by then, so the client sees a
    // complete reply; what it avoids is the server-side TIME_WAIT pile-up.
    // With a 150 ms poll and a 5 s keep-alive window the board churns through
    // connections fast, and every lingering pcb holds a TCP control block -
    // that pile-up is what drove the free heap down to a few hundred bytes.
    struct linger lg {};
    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
    s_http.fd = fd;
    s_http.used = 0;
    s_http.sending = false;
    s_http.started = s_http.progress = millis();
  }
  const uint32_t now = millis();
  if (now - s_http.progress > kProgressTimeoutMs || now - s_http.started > kRequestTimeoutMs) {
    BR_LOGW(kTag, "HTTP timeout (header %u/%u, body %u/%u)", (unsigned)s_http.headerSent,
        (unsigned)s_http.headerLen, (unsigned)s_http.bodySent, (unsigned)s_http.bodyLen);
    closeExchange(false); return;
  }
  if (!s_http.sending) {
    const size_t room = kHeaderLimit - 1 - s_http.used;
    if (!room) { errorResponse(431, "Request Header Fields Too Large", "request headers too large"); return; }
    const int n = recv(s_http.fd, s_http.io + s_http.used, room < kIoPerLoop ? room : kIoPerLoop, MSG_DONTWAIT);
    if (n == 0) { closeExchange(false); return; }
    if (n < 0) { if (!retryable(errno)) closeExchange(false); return; }
    s_http.used += (size_t)n;
    s_http.io[s_http.used] = 0;
    s_http.progress = now;
    if (strstr(s_http.io, "\r\n\r\n")) dispatch();
    return;
  }
  const bool header = s_http.headerSent < s_http.headerLen;
  const char *src = header ? s_http.header + s_http.headerSent : s_http.body + s_http.bodySent;
  const size_t left = header ? s_http.headerLen - s_http.headerSent : s_http.bodyLen - s_http.bodySent;
  if (!left) { closeExchange(true); return; }
  // Exactly one nonblocking send per loop. Never wait or retry here: returning
  // to loop delivers queued BLE key releases before the next network attempt.
  const int n = send(s_http.fd, src, left < kIoPerLoop ? left : kIoPerLoop, MSG_DONTWAIT);
  if (n < 0) {
    if (retryable(errno)) ++s_backPressure;
    else { BR_LOGW(kTag, "HTTP send failed, errno %d", errno); closeExchange(false); }
    return;
  }
  if (n == 0) return;
  if (header) s_http.headerSent += (size_t)n;
  else s_http.bodySent += (size_t)n;
  s_http.progress = now;
  if (s_http.headerSent == s_http.headerLen && s_http.bodySent == s_http.bodyLen) {
    // Keep the connection open. The page polls for live key state, and opening
    // a fresh socket per poll would pile up TIME_WAIT entries (TCP_MSL is 60 s)
    // until lwIP ran out of sockets. Idle connections are dropped by the
    // progress timeout above.
    s_http.sending = false;
    s_http.used = 0;
    s_http.body = nullptr;
    s_http.headerSent = s_http.headerLen = 0;
    s_http.bodySent = s_http.bodyLen = 0;
    s_http.started = s_http.progress = now;
    ++s_completed;
  }
}

void onApEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      BR_LOGI(kTag, "AP station joined (aid %u)", (unsigned)info.wifi_ap_staconnected.aid); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      BR_LOGI(kTag, "AP station left (reason %u)", (unsigned)info.wifi_ap_stadisconnected.reason); break;
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      BR_LOGI(kTag, "AP assigned a DHCP address"); break;
    default: break;
  }
}
}  // namespace

namespace wifi_ui {
void begin() {
  // Always-on policy: if a network is already configured, arm the automatic
  // start. The actual enable() happens in loop() once the BLE side has had a
  // few seconds to connect, and it is non-blocking there.
  if (!settings::wifiSsid().isEmpty()) {
    s_autoStartPending = true;
    BR_LOGI(kTag, "config UI armed to auto-start %u ms after boot (network \"%s\")",
            (unsigned)kAutoStartDelayMs, settings::wifiSsid().c_str());
  }
}

bool enable() {
  if (s_enabled) return s_mode == Mode::Station;
  const String ssid = settings::wifiSsid();
  if (ssid.isEmpty()) { BR_LOGE(kTag, "configure a network with wifi join <ssid> <password> first"); return false; }
  const String pass = settings::wifiPassword();
  BR_LOGI(kTag, "joining saved network; heap %u B, largest %u B, host %d, remote %d",
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
      (int)hid_server::hostConnected(), (int)rc003_client::connected());
  WiFi.persistent(false);
  // No modem sleep. This is the single biggest lever on how "live" the page
  // feels: with the default sleep enabled, every status poll waits for the
  // next beacon window and answers 100-300 ms late, which is exactly what
  // makes the key highlight trail the physical press. Disabled, requests
  // answer in single-digit milliseconds. The cost is a higher receive duty
  // cycle, acceptable here because the board is USB-powered and the poll is
  // small; it is re-enabled during the join only if that ever proves too
  // greedy for Bluetooth coexistence.
  WiFi.setSleep(false);
  if (!WiFi.mode(WIFI_STA)) return false;
  WiFi.begin(ssid.c_str(), pass.c_str());
  s_enabled = true;
  s_mode = Mode::Station;
  s_hadAddress = false;
  s_joinStarted = s_retryAt = millis();
  // Association completes in loop(). Do NOT delay here: the same loop owns
  // HID press/release dispatch, even though BLE radio tasks run separately.
  return true;
}

bool enableAp() {
  if (s_enabled) return s_mode == Mode::Ap;
  WiFi.persistent(false);
  if (!s_eventsRegistered) { WiFi.onEvent(onApEvent); s_eventsRegistered = true; }
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(kApSsid, nullptr, 1, 0, 1)) {
    WiFi.mode(WIFI_OFF); return false;
  }
  s_enabled = true;
  s_mode = Mode::Ap;
  s_hadAddress = true;
  s_retryAt = 0;
  if (!startHttp()) { disable(); return false; }
  return true;
}

bool disable() {
  // Always-on policy (user requirement): the config UI must stay reachable, so
  // stopping it is refused outright rather than obeyed. boot without stored
  // credentials is the one case where it never came up in the first place.
  BR_LOGW(kTag, "config UI is always on - refusing to stop Wi-Fi (wifi status to see it)");
  return false;
}

bool enabled() { return s_enabled; }
bool ready() { return s_enabled && s_listener >= 0 && (s_mode == Mode::Ap || WiFi.status() == WL_CONNECTED); }
const char *mode() { return s_mode == Mode::Station ? "sta" : s_mode == Mode::Ap ? "ap" : "off"; }
unsigned stationCount() { return s_mode == Mode::Ap ? WiFi.softAPgetStationNum() : 0; }
const char *ip() {
  static char buf[16];
  if (!s_enabled) return "-";
  const IPAddress addr = s_mode == Mode::Ap ? WiFi.softAPIP() : WiFi.localIP();
  snprintf(buf, sizeof(buf), "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
  return buf;
}

void loop() {
  const uint32_t now = millis();

  // Always-on: bring the UI up by itself after boot (BLE first - the delay
  // above), and keep trying if credentials are not usable yet, because
  // `wifi join` may set them at any time during this session.
  if (s_autoStartPending && !s_enabled && now > kAutoStartDelayMs && now - s_retryAt > 5000) {
    s_retryAt = now;
    if (enable()) {
      s_autoStartPending = false;
      BR_LOGI(kTag, "always-on config UI starting; BLE links remain up");
    }
    return;
  }

  if (!s_enabled) return;

  if (s_mode == Mode::Station) {
    if (WiFi.status() != WL_CONNECTED || (uint32_t)WiFi.localIP() == 0) {
      if (s_listener >= 0) { stopHttp(); BR_LOGW(kTag, "Wi-Fi lost; BLE continues, reconnecting"); }
      // Always-on: a failing join is retried forever instead of stopping the
      // UI. Router reboots and corrected credentials then heal on their own.
      if (!s_hadAddress && now - s_joinStarted > 15000) {
        BR_LOGW(kTag, "Wi-Fi join still failing (status %d); retrying with saved credentials",
                (int)WiFi.status());
        s_joinStarted = now;
        const String ssid = settings::wifiSsid();
        const String pass = settings::wifiPassword();
        if (!ssid.isEmpty()) {
          WiFi.disconnect(false);  // keep credentials stored
          WiFi.begin(ssid.c_str(), pass.c_str());
        }
        return;
      }
      if (s_hadAddress && now - s_retryAt > 15000) { s_retryAt = now; WiFi.reconnect(); }
      return;
    }
    s_hadAddress = true;
  }
  if (s_listener < 0 && now - s_retryAt > 500) { s_retryAt = now; startHttp(); }
  pollHttp();
}
}  // namespace wifi_ui
