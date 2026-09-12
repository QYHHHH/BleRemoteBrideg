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

#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/sha1.h>
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
constexpr size_t kIoPerLoop = 1024;
constexpr uint32_t kProgressTimeoutMs = 4000;
// How long a finished keep-alive socket may hold the single exchange slot while
// it waits for the next request on itself. Short on purpose: the slot is not
// shareable, so a lingering idle socket delays every other client. Measured
// before this was added: with five sockets open a browser's upgrade request got
// no reply at all within 8 s, because it queued behind four idle ones.
constexpr uint32_t kKeepAliveIdleMs = 300;
// How long one loop() pass may spend pushing a response out. Short enough to
// keep BLE key dispatch prompt, long enough that a 6 KB script leaves in a few
// passes instead of sixteen.
constexpr uint32_t kSendBudgetMs = 5;
constexpr uint32_t kRequestTimeoutMs = 15000;
struct Exchange {
  int fd = -1;
  bool sending = false;
  bool idle = false;           // response done, socket parked for one more request
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

// Long-poll hold. The page parks one request here and the board answers the
// instant a key is forwarded, or when the window expires. Deliberately not
// SSE or WebSocket: this server serves ONE exchange at a time, so a streaming
// socket held open for minutes would block every other request - including the
// page's own saves. A parked request costs nothing extra and still gives
// event-driven latency instead of polling.

// ---------------------------------------------------------------------------
// WebSocket (RFC 6455, the small subset a browser needs)
//
// One long-lived connection replaces every polling experiment: the board
// pushes a frame the instant a key is forwarded, and the page sends its
// binding commands back over the same socket. Nothing asks "did anything
// happen" any more, and - because the page's only connection is this one -
// nothing can be blocked behind a parked request either.
//
// Supported: text frames (unfragmented), ping->pong, close. The browser always
// masks its frames; we never mask ours.
// ---------------------------------------------------------------------------
constexpr size_t kWsIn = 512;
constexpr size_t kWsOut = 768;
constexpr uint32_t kWsStatusEveryMs = 5000;  // state heartbeat on the same socket

bool s_ws = false;
int s_wsFd = -1;            // frame mode (101 already delivered)
bool s_pendingUpgrade = false; // 101 still draining through the HTTP path
uint8_t s_wsInBuf[kWsIn];
size_t s_wsInLen = 0;
uint8_t s_wsOutBuf[kWsOut];
size_t s_wsOutLen = 0;
size_t s_wsOutSent = 0;
uint32_t s_wsLastKeyCount = 0;
uint32_t s_wsLastStatusMs = 0;
// Keepalive. The socket owns the single service slot, so a peer that vanishes
// without a FIN (laptop sleeping, script killed, Wi-Fi drop) would otherwise
// wedge the whole config server until lwip's own keepalive gives up hours
// later. We ping every 10 s and drop the socket if nothing at all arrives -
// a browser answers pings automatically - for 30 s.
uint32_t s_wsLastRecvMs = 0;
uint32_t s_wsLastPingMs = 0;
constexpr uint32_t kWsPingEveryMs = 10000;
constexpr uint32_t kWsDeadAfterMs = 30000;

void wsQueue(const uint8_t *payload, size_t len);
void wsConsume();
void wsClose();

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
  s_http.idle = false;
  s_pendingUpgrade = false;
  // Deliberately does NOT touch the websocket any more. Since the upgrade moved
  // the frame socket to its own fd, an HTTP slot teardown - a stale request, an
  // idle timeout, even stopHttp - must not take the live page down with it.
  // That coupling was one of the two reasons the page reconnected forever.
}

void stopHttp() {
  closeExchange(false);
  if (s_ws) wsClose();
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
  if (!nonblocking(fd) || bind(fd, (sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 8) != 0) {
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
      "Connection: keep-alive\r\nKeep-Alive: timeout=1, max=100\r\n"
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
  // keyPresses is a monotonic counter: the page flashes the key whenever it
  // changes, so a press shorter than the poll interval is still shown.
  out.add(",\"keyPresses\":%lu,\"lastKey\":%u", (unsigned long)bridge::keyPresses(),
      bridge::lastKeyRaw());
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

// --- Console authentication --------------------------------------------------
//
// HTTP gets HTTP Basic, so the browser shows its own dialog and offers to save
// the password. The websocket cannot use Basic - a browser will not attach an
// Authorization header to WebSocket() - so the page first fetches a derived
// token over an authenticated request and passes it in the URL.
//
// With no password stored the device is in setup mode: the first visit is
// challenged with a realm that says so, and whatever the user types becomes
// the password. BOOT held for 5 s clears the namespace, which is the way back
// in when the password is forgotten.

const char *kConsoleUser = "admin";  // fixed: the user name never changes

bool authDecodeBasic(const char *header, String &user, String &pass) {
  if (!header || strncasecmp(header, "Basic ", 6) != 0) return false;
  unsigned char raw[160];
  size_t rawLen = 0;
  if (mbedtls_base64_decode(raw, sizeof(raw) - 1, &rawLen,
                            (const unsigned char *)(header + 6), strlen(header + 6)) != 0) {
    return false;
  }
  raw[rawLen] = 0;
  const char *colon = strchr((const char *)raw, ':');
  if (!colon) return false;
  user = String((const char *)raw).substring(0, colon - (const char *)raw);
  pass = String(colon + 1);
  return true;
}

// Shown while no password is stored. Chrome shows only "Sign in" for a Basic
// challenge - the realm text never reaches the user - so the explanation has
// to live in a real page.
const char kSetupPage[] =
    "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>设置访问密码</title><style>"
    "body{font-family:system-ui,-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;"
    "background:#f4f5fa;color:#1c2333;margin:0;padding:24px;display:flex;justify-content:center}"
    ".c{background:#fff;border:1px solid #e6e9f2;border-radius:16px;padding:26px;max-width:420px;width:100%}"
    "h1{font-size:19px;margin:0 0 10px}p{font-size:13px;color:#5a627a;line-height:1.7;margin:0 0 18px}"
    "label{display:block;font-size:12px;color:#8a93a6;margin:14px 0 6px}"
    "input{width:100%;box-sizing:border-box;padding:11px;border:1px solid #e6e9f2;border-radius:9px;font-size:14px}"
    "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;background:#3b6ef6;"
    "color:#fff;font-size:14px;cursor:pointer}"
    ".hint{font-size:12px;color:#8a93a6;margin-top:16px;line-height:1.7}"
    "</style></head><body><div class=\"c\">"
    "<h1>首次使用：设置访问密码</h1>"
    "<p>这台设备还没有访问密码。设置之后浏览器会提示保存，以后自动填入。</p>"
    "<form method=\"get\" action=\"/setup\">"
    "<label>访问密码（至少 4 位）</label>"
    "<input type=\"password\" name=\"password\" required minlength=\"4\" autofocus>"
    "<label>再输入一次</label>"
    "<input type=\"password\" name=\"confirm\" required minlength=\"4\">"
    "<button type=\"submit\">设置密码</button></form>"
    "<p class=\"hint\">忘记密码时：按住板上 BOOT 键 5 秒，即可清除全部设置与配对"
    "（Wi-Fi 密码、访问密码、按键映射、蓝牙配对）恢复出厂。</p>"
    "</div></body></html>";

const char kSetupDonePage[] =
    "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>密码已设置</title><style>"
    "body{font-family:system-ui,-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;"
    "background:#f4f5fa;color:#1c2333;margin:0;padding:24px;display:flex;justify-content:center}"
    ".c{background:#fff;border:1px solid #e6e9f2;border-radius:16px;padding:26px;max-width:420px;width:100%}"
    "h1{font-size:19px;margin:0 0 10px}p{font-size:13px;color:#5a627a;line-height:1.7}"
    "a{display:inline-block;margin-top:18px;padding:11px 20px;border-radius:10px;background:#3b6ef6;"
    "color:#fff;text-decoration:none;font-size:14px}</style></head><body><div class=\"c\">"
    "<h1>密码已设置</h1>"
    "<p>请重新打开页面，浏览器会要求输入用户名和密码——<b>密码已设置，正在进入配置界面</b>。"
    "浏览器会提示保存它。</p><a href=\"/\">打开配置页面</a>"
    "</div></body></html>";

const char kSetupMismatchPage[] =
    "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>两次输入不一致</title></head><body style=\"font-family:system-ui,sans-serif;padding:24px\">"
    "<h1 style=\"font-size:18px\">两次输入的密码不一致</h1>"
    "<p><a href=\"/\">返回重试</a></p></body></html>";

// Pull one query parameter out of a raw query string ("a=1&b=2").
bool queryValue(const char *query, const char *name, char *out, size_t cap) {
  if (!query || !name || !out || cap == 0) return false;
  const size_t nameLen = strlen(name);
  const char *at = query;
  while (at && *at) {
    if (strncmp(at, name, nameLen) == 0 && at[nameLen] == '=') {
      const char *v = at + nameLen + 1;
      const char *end = strchr(v, '&');
      size_t len = end ? (size_t)(end - v) : strlen(v);
      if (len >= cap) len = cap - 1;
      memcpy(out, v, len);
      out[len] = 0;
      return true;
    }
    at = strchr(at, '&');
    if (at) ++at;
  }
  return false;
}

// --- Login page + HMAC-signed session cookie -------------------------------
//
// The board never stores the plaintext password. Only sha1(password) lives in
// NVS, and the cookie is HMAC_SHA1(sha1(password), timestamp). A leaked cookie
// therefore proves knowledge of the hash - not the password - and cannot be
// regenerated by anyone who does not already have the hash.
//
// All page bodies are PROGMEM constants, so serving them costs no heap.

const char *kCookieName = "mrb_sess";
constexpr uint32_t kSessionSeconds = 7 * 24 * 3600;

bool base64Encode(const uint8_t *in, size_t inLen, char *out, size_t outCap) {
  size_t olen = 0;
  if (mbedtls_base64_encode((unsigned char *)out, outCap, &olen, in, inLen) != 0) return false;
  out[olen] = 0;
  return true;
}

bool readSessionCookie(const char *headers, char *out, size_t outCap) {
  if (!headers) return false;
  const size_t nameLen = strlen(kCookieName);
  for (const char *p = strcasestr(headers, "\r\nCookie:"); p;
       p = strcasestr(p + 1, "\r\nCookie:")) {
    p += 2 + 7;
    while (*p == ' ' || *p == '\t') ++p;
    if (strncmp(p, kCookieName, nameLen) != 0 || p[nameLen] != '=') continue;
    const char *v = p + nameLen + 1;
    const char *end = strchr(v, ';');
    size_t len = end ? (size_t)(end - v) : strlen(v);
    if (len >= outCap) len = outCap - 1;
    memcpy(out, v, len);
    out[len] = 0;
    return true;
  }
  return false;
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// The cookie is base64(timestamp(4) || HMAC_SHA1(stored_hash, timestamp)).
// We re-derive both halves and compare. Lifetime is enforced by the timestamp
// window alone.
bool sessionMatches(const char *cookie) {
  if (!cookie || !*cookie) return false;
  unsigned char blob[4 + 20];
  size_t blobLen = 0;
  if (mbedtls_base64_decode(blob, sizeof(blob), &blobLen,
                            (const unsigned char *)cookie, strlen(cookie)) != 0
      || blobLen != sizeof(blob)) {
    return false;
  }
  const uint32_t t = ((uint32_t)blob[0] << 24) | ((uint32_t)blob[1] << 16)
                    | ((uint32_t)blob[2] << 8) |  (uint32_t)blob[3];
  const uint32_t now = (uint32_t)(millis() / 1000);
  const uint32_t skew = (t > now) ? (t - now) : (now - t);
  if (skew > kSessionSeconds) return false;
  const String stored = settings::webPassHash();
  if (stored.length() != 40) return false;
  uint8_t keyBytes[20];
  for (int i = 0; i < 20; ++i) {
    const int hi = hexNibble(stored[i * 2]);
    const int lo = hexNibble(stored[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    keyBytes[i] = (uint8_t)((hi << 4) | lo);
  }
  uint8_t expected[20];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info) return false;
  if (mbedtls_md_hmac(info, keyBytes, sizeof(keyBytes), blob, 4, expected) != 0) return false;
  return memcmp(expected, blob + 4, 20) == 0;
}

// Issue Set-Cookie + 302 to /. The token is HMAC(stored_hash, now).
void issueSessionCookie() {
  const uint32_t t = (uint32_t)(millis() / 1000);
  uint8_t t4[4];
  t4[0] = (uint8_t)(t >> 24); t4[1] = (uint8_t)(t >> 16);
  t4[2] = (uint8_t)(t >> 8);  t4[3] = (uint8_t)t;
  const String stored = settings::webPassHash();
  uint8_t keyBytes[20];
  for (int i = 0; i < 20; ++i) {
    const int hi = hexNibble(stored[i * 2]);
    const int lo = hexNibble(stored[i * 2 + 1]);
    keyBytes[i] = (uint8_t)((hi << 4) | lo);
  }
  uint8_t mac[20];
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
  if (!info || mbedtls_md_hmac(info, keyBytes, sizeof(keyBytes), t4, 4, mac) != 0) {
    errorResponse(500, "Internal Server Error", "session token error");
    return;
  }
  uint8_t blob[24];
  memcpy(blob, t4, 4); memcpy(blob + 4, mac, 20);
  char token[64];
  if (!base64Encode(blob, sizeof(blob), token, sizeof(token))) {
    errorResponse(500, "Internal Server Error", "session token error");
    return;
  }
  const int n = snprintf(s_http.header, sizeof(s_http.header),
      "HTTP/1.1 302 Found\r\n"
      "Set-Cookie: %s=%s; Path=/; HttpOnly; Max-Age=604800; SameSite=Lax\r\n"
      "Location: /\r\nCache-Control: no-store\r\n"
      "Content-Length: 0\r\nConnection: close\r\n\r\n",
      kCookieName, token);
  if (n < 0 || (size_t)n >= sizeof(s_http.header)) { closeExchange(false); return; }
  s_http.headerLen = (size_t)n; s_http.headerSent = 0;
  s_http.body = ""; s_http.bodyLen = 0; s_http.bodySent = 0;
  s_http.sending = true; s_http.progress = millis();
}

// One password field, no user name.
const char kLoginPage[] =
    "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>登录 - MiRemoteBridge</title><style>"
    "body{font-family:system-ui,-apple-system,'PingFang SC','Microsoft YaHei',sans-serif;"
    "background:#f4f5fa;color:#1c2333;margin:0;padding:24px;display:flex;justify-content:center}"
    ".c{background:#fff;border:1px solid #e6e9f2;border-radius:16px;padding:26px;max-width:380px;width:100%}"
    "h1{font-size:18px;margin:0 0 8px}p{font-size:13px;color:#5a627a;line-height:1.7;margin:0 0 18px}"
    "label{display:block;font-size:12px;color:#8a93a6;margin:14px 0 6px}"
    "input{width:100%;box-sizing:border-box;padding:11px;border:1px solid #e6e9f2;border-radius:9px;font-size:14px}"
    "button{margin-top:20px;width:100%;padding:12px;border:0;border-radius:10px;background:#3b6ef6;"
    "color:#fff;font-size:14px;cursor:pointer}"
    "</style></head><body><div class=\"c\">"
    "<h1>登录</h1><p>输入访问密码即可进入配置界面。浏览器会记住会话，下次自动登录。</p>"
    "<form method=\"get\" action=\"/login\">"
    "<label>访问密码</label>"
    "<input type=\"password\" name=\"password\" required autofocus>"
    "<button type=\"submit\">登录</button></form>"
    "</div></body></html>";

const char kLoginFailPage[] =
    "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<meta http-equiv=\"refresh\" content=\"0;url=/login\">"
    "<title>密码错误</title></head><body style=\"font-family:system-ui,sans-serif;padding:24px\">"
    "<p>密码错误，正在返回登录页...</p></body></html>";

void authChallenge(bool setupMode) {
  const char *realm = setupMode ? "MiRemoteBridge setup - choose a console password"
                                : "MiRemoteBridge (user name: admin)";
  const char *body = setupMode
      ? "Setup mode: enter a user name (any) and the password you want to use.\n"
        "It is stored hashed; hold BOOT for 5 seconds to wipe it again.\n"
      : "Password required.\n";
  const int n = snprintf(s_http.header, sizeof(s_http.header),
      "HTTP/1.1 401 Unauthorized\r\n"
      "WWW-Authenticate: Basic realm=\"%s\", charset=\"UTF-8\"\r\n"
      "Content-Type: text/plain; charset=utf-8\r\n"
      "Content-Length: %u\r\n"
      "Cache-Control: no-store\r\n"
      "Connection: close\r\n\r\n",
      realm, (unsigned)strlen(body));
  if (n < 0 || (size_t)n >= sizeof(s_http.header)) {
    closeExchange(false);
    return;
  }
  s_http.headerLen = (size_t)n;
  s_http.headerSent = 0;
  s_http.body = body;
  s_http.bodyLen = strlen(body);
  s_http.bodySent = 0;
  s_http.sending = true;
  s_http.progress = millis();
}

// --- WebSocket plumbing -----------------------------------------------------

void wsClose() {
  if (s_ws) {
    BR_LOGI(kTag, "websocket closed (fd %d), heap %u B", s_wsFd, (unsigned)ESP.getFreeHeap());
  }
  s_ws = false;
  if (s_wsFd >= 0) ::close(s_wsFd);
  s_wsFd = -1;
  s_wsInLen = 0;
  s_wsOutLen = s_wsOutSent = 0;
}

// Queue one unmasked text frame. Refuses while the previous frame is still
// draining: callers are event handlers, not queues.
bool wsQueueRaw(const uint8_t *payload, size_t len, uint8_t opcode) {
  // A frame that is refused here is gone: the caller has no way to retry and the
  // page will simply never see that event. Stay loud about it - a silently
  // dropped frame is what made the page sit on "waiting to read" for a whole
  // session (the binding snapshot was built into 1024 bytes and handed to a
  // 768-byte frame buffer, so every snapshot was dropped without a log line).
  if (s_wsOutSent < s_wsOutLen) {
    BR_LOGW(kTag, "ws frame refused: previous frame still draining (%u/%u B)",
            (unsigned)s_wsOutSent, (unsigned)s_wsOutLen);
    return false;
  }
  const size_t need = len + 10;
  if (need > sizeof(s_wsOutBuf)) {
    BR_LOGW(kTag, "ws frame refused: %u B payload does not fit the %u B frame "
                  "buffer - send it over HTTP instead (opcode %u)",
            (unsigned)len, (unsigned)sizeof(s_wsOutBuf), (unsigned)opcode);
    return false;
  }
  size_t n = 0;
  s_wsOutBuf[n++] = (uint8_t)(0x80 | opcode);
  if (len < 126) {
    s_wsOutBuf[n++] = (uint8_t)len;
  } else if (len < 65536) {
    s_wsOutBuf[n++] = 126;
    s_wsOutBuf[n++] = (uint8_t)(len >> 8);
    s_wsOutBuf[n++] = (uint8_t)(len & 0xFF);
  } else {
    return false;  // nothing we send is that big
  }
  memcpy(s_wsOutBuf + n, payload, len);
  n += len;
  s_wsOutLen = n;
  s_wsOutSent = 0;
  return true;
}

void wsQueue(const char *payload) { wsQueueRaw((const uint8_t *)payload, strlen(payload), 0x1); }

void wsSendStatus() {
  char buf[320];
  Json out(buf, sizeof(buf));
  out.add("{\"type\":\"status\",\"keyPresses\":%lu,\"lastKey\":%u,\"activeKey\":%u,"
          "\"remoteConnected\":%s,\"hostConnected\":%s,\"battery\":%d,\"bindings\":%u}",
      (unsigned long)bridge::keyPresses(), bridge::lastKeyRaw(), bridge::activeRawCode(),
      boolean(rc003_client::connected()), boolean(hid_server::hostConnected()),
      settings::batteryLevel(), (unsigned)keymap_binding_count());
  if (out.ok()) wsQueueRaw((const uint8_t *)buf, out.size(), 0x1);
}

// Console-style command arriving over the socket: "get", "set?raw=..&kind=..",
// "reset" - the same query parsing the HTTP endpoints use.
//
// Every reply here MUST be a small frame. The binding table grows with the user's
// configuration and does not fit a single websocket frame, and a frame that does
// not fit is dropped - that is precisely how this page once spent a whole session
// stuck on "waiting to read". The table itself is therefore served only over
// HTTP, where Content-Length frames it whatever its size; the socket just
// acknowledges and lets the caller re-read. Do not add a "send the bindings"
// command back here.
void wsCommand(char *msg) {
  char *query = strchr(msg, '?');
  if (query) *query++ = 0;
  if (!strcmp(msg, "get")) {
    wsQueue("{\"type\":\"error\",\"error\":\"bindings are served over HTTP\"}");
    return;
  }
  if (!strcmp(msg, "status")) {
    wsSendStatus();
    return;
  }
  if (!strcmp(msg, "set") && query) {
    uint8_t raw = 0, kind = 0, mod = 0, key = 0;
    uint16_t cons = 0;
    if (!bindingArgs(query, raw, kind, mod, key, cons)) {
      wsQueue("{\"type\":\"error\",\"error\":\"invalid binding arguments\"}");
      return;
    }
    settings::setBinding(raw, kind, mod, key, cons);
    wsQueue("{\"type\":\"saved\"}");
    return;
  }
  if (!strcmp(msg, "reset")) {
    uint8_t raws[KEYMAP_MAX_BINDINGS];
    hid_action_t acts[KEYMAP_MAX_BINDINGS];
    size_t n = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
    while (n > 0) {
      settings::setBinding(raws[0], 0, 0, 0, 0);
      n = keymap_get_bindings(raws, acts, KEYMAP_MAX_BINDINGS);
    }
    wsQueue("{\"type\":\"reset\"}");
    return;
  }
  wsQueue("{\"type\":\"error\",\"error\":\"unknown command\"}");
}

// Consume every complete frame sitting in s_wsInBuf.
void wsConsume() {
  size_t used = 0;
  while (s_wsInLen - used >= 2) {
    const uint8_t *f = s_wsInBuf + used;
    const size_t avail = s_wsInLen - used;
    const uint8_t opcode = (uint8_t)(f[0] & 0x0F);
    const bool masked = (f[1] & 0x80) != 0;
    size_t plen = (size_t)(f[1] & 0x7F);
    size_t off = 2;
    if (plen == 126) {
      if (avail < 4) break;
      plen = ((size_t)f[2] << 8) | f[3];
      off = 4;
    } else if (plen == 127) {
      wsClose();  // not supported; a browser never sends one of these
      return;
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
      if (avail < off + 4) break;
      memcpy(mask, f + off, 4);
      off += 4;
    }
    if (avail < off + plen) break;  // frame still arriving
    uint8_t *payload = s_wsInBuf + used + off;
    if (masked) {
      for (size_t i = 0; i < plen; ++i) payload[i] ^= mask[i & 3];
    }
    used += off + plen;

    if (opcode == 0x8) {  // close
      wsQueueRaw(payload, plen, 0x8);
      wsClose();
      return;
    }
    if (opcode == 0x9) {  // ping -> pong
      wsQueueRaw(payload, plen, 0xA);
      continue;
    }
    if (opcode == 0x1 && plen > 0 && plen < (kWsIn - 1)) {
      payload[plen] = 0;  // the command parser wants a C string
      wsCommand((char *)payload);
    }
  }
  if (used) {
    memmove(s_wsInBuf, s_wsInBuf + used, s_wsInLen - used);
    s_wsInLen -= used;
  }
}

void wsHandshake(const char *key) {
  // Sec-WebSocket-Accept = base64(sha1(key + GUID))
  static const char *kGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  char joined[128];
  snprintf(joined, sizeof(joined), "%s%s", key, kGuid);
  unsigned char digest[20];
  mbedtls_sha1_context sha;
  mbedtls_sha1_init(&sha);
  const int shaRc = mbedtls_sha1_update(&sha, (const unsigned char *)joined, strlen(joined)) |
                    mbedtls_sha1_finish(&sha, digest);
  mbedtls_sha1_free(&sha);
  if (shaRc != 0) {
    errorResponse(500, "Internal Server Error", "sha1 failed");
    return;
  }
  unsigned char accept[40];
  size_t acceptLen = 0;
  if (mbedtls_base64_encode(accept, sizeof(accept), &acceptLen, digest, sizeof(digest)) != 0) {
    errorResponse(500, "Internal Server Error", "base64 failed");
    return;
  }
  accept[acceptLen] = 0;

  const int n = snprintf(s_http.header, sizeof(s_http.header),
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: %s\r\n\r\n",
      (const char *)accept);
  if (n < 0 || (size_t)n >= sizeof(s_http.header)) {
    errorResponse(500, "Internal Server Error", "handshake header too large");
    return;
  }
  s_http.headerLen = (size_t)n;
  s_http.headerSent = 0;
  s_http.body = nullptr;
  s_http.bodyLen = 0;
  s_http.bodySent = 0;
  s_http.sending = true;
  s_http.progress = millis();
  // Not a websocket yet: the 101 has to drain through the normal HTTP send
  // path first. Switching here would make the frame branch below swallow the
  // response and the client would see the connection die instead of a 101.
  s_pendingUpgrade = true;
  s_ws = false;
  s_wsInLen = s_wsOutLen = s_wsOutSent = 0;
  s_wsLastKeyCount = bridge::keyPresses();
  s_wsLastStatusMs = millis();
  s_wsLastRecvMs = millis();
  s_wsLastPingMs = s_wsLastRecvMs;
}

// The whole event payload: the key counter (so nothing is ever missed), the
// last key for the flash, the live key for as long as it is held, plus the
// link states so a disconnect shows up here too.
void wsSendKeyEvent() {
  Json out(s_http.io, sizeof(s_http.io));
  out.add("{\"keyPresses\":%lu,\"lastKey\":%u,\"activeKey\":%u,\"remoteConnected\":%s,"
          "\"hostConnected\":%s,\"battery\":%d}",
      (unsigned long)bridge::keyPresses(), bridge::lastKeyRaw(), bridge::activeRawCode(),
      boolean(rc003_client::connected()), boolean(hid_server::hostConnected()),
      settings::batteryLevel());
  jsonResult(out);
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
  char host[96] = {}, origin[128] = {}, auth[200] = {};
  for (char *line = strstr(s_http.io, "\r\n"); line && line[2];) {
    line += 2;
    char *end = strstr(line, "\r\n");
    if (!end || end == line) break;
    const char saved = *end; *end = 0;
    if (strncasecmp(line, "Host:", 5) == 0) sscanf(line + 5, "%95s", host);
    if (strncasecmp(line, "Authorization:", 14) == 0) {
      // Skip the space after the colon, then take the rest of the value:
      // %s would stop at that space and hand us just "Basic".
      const char *v = line + 14;
      while (*v == ' ' || *v == '\t') ++v;
      sscanf(v, "%199[^\r\n]", auth);
    }
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

  // Authentication. The cookie is HMAC(sha1(password), timestamp) - no plaintext
  // is ever on the device. The websocket still exchanges its token in the
  // query string because browsers do not attach cookies to secure handshakes
  // the way they do for ordinary fetches.
  const bool isWs = strcmp(target, "/ws") == 0;
  const bool isLogin = strcmp(target, "/login") == 0;
  const bool isLogout = strcmp(target, "/logout") == 0;
  const bool isSetup = strcmp(target, "/setup") == 0;
  char session[64] = {};
  const bool hasSession = readSessionCookie(s_http.io, session, sizeof(session));

  // Setup mode: force the visitor to define a password before anything else
  // is served. (Without this, an unset password meant "wide open", which read
  // as a cache bug to the user: the page just opened with no prompt at all.)
  if (!settings::hasWebPassword() && !isSetup && !isLogin && !isLogout && !isWs
      && strcmp(target, "/api/token") != 0) {
    const int n = snprintf(s_http.header, sizeof(s_http.header),
        "HTTP/1.1 302 Found\r\nLocation: /setup\r\nCache-Control: no-store\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n");
    if (n < 0 || (size_t)n >= sizeof(s_http.header)) { closeExchange(false); return; }
    s_http.headerLen = (size_t)n; s_http.headerSent = 0;
    s_http.body = ""; s_http.bodyLen = 0; s_http.bodySent = 0;
    s_http.sending = true; s_http.progress = millis();
    return;
  }

  if (isWs) {
    if (settings::hasWebPassword()) {
      const char *tok = query ? strstr(query, "token=") : nullptr;
      const String expected = settings::webToken();
      if (!tok || expected.length() == 0 || expected != String(tok + 6)) {
        errorResponse(401, "Unauthorized", "websocket token required");
        return;
      }
    }
  } else if (settings::hasWebPassword()) {
    if (!hasSession || !sessionMatches(session)) {
      if (strcmp(target, "/api/token") == 0) {
        // A JSON 401 rather than a redirect: the page checks for exactly this
        // and sends the user to /login. A 302 would look like valid HTML to
        // fetch(), which is what kept the websocket retrying forever.
        errorResponse(401, "Unauthorized", "session expired - sign in again");
        return;
      }
        BR_LOGI(kTag, "no/bad session for %s -> /login", target);
      if (!isLogin && !isLogout && !isSetup) {
        const int n = snprintf(s_http.header, sizeof(s_http.header),
            "HTTP/1.1 302 Found\r\nLocation: /login\r\nCache-Control: no-store\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n");
        if (n < 0 || (size_t)n >= sizeof(s_http.header)) { closeExchange(false); return; }
        s_http.headerLen = (size_t)n; s_http.headerSent = 0;
        s_http.body = ""; s_http.bodyLen = 0; s_http.bodySent = 0;
        s_http.sending = true; s_http.progress = millis();
        return;
      }
    }
  }
  // Setup mode (no password yet), and /login, /logout, /setup themselves are open.

  if (isLogin) {
    // The form submits with GET (the server refuses non-empty POST bodies), so
    // a password may ride in the query string. Present -> verify and set the
    // cookie; absent -> show the form.
    if (get || head || post) {
      char pw[64] = {};
      const bool havePw = query && queryValue(query, "password", pw, sizeof(pw)) && strlen(pw) > 0;
      if (havePw) {
        if (!settings::checkWebPassword(String(pw))) {
          respond(200, "OK", "text/html; charset=utf-8", kLoginFailPage, strlen(kLoginFailPage));
        } else {
          issueSessionCookie();
        }
        return;
      }
      respond(200, "OK", "text/html; charset=utf-8", kLoginPage, strlen(kLoginPage));
      return;
    }
  }

  if (isLogout) {
    if (get || head) {
      const int n = snprintf(s_http.header, sizeof(s_http.header),
          "HTTP/1.1 302 Found\r\n"
          "Set-Cookie: %s=; Path=/; Max-Age=0; SameSite=Lax\r\n"
          "Location: /login\r\nCache-Control: no-store\r\n"
          "Content-Length: 0\r\nConnection: close\r\n\r\n",
          kCookieName);
      if (n < 0 || (size_t)n >= sizeof(s_http.header)) { closeExchange(false); return; }
      s_http.headerLen = (size_t)n; s_http.headerSent = 0;
      s_http.body = ""; s_http.bodyLen = 0; s_http.bodySent = 0;
      s_http.sending = true; s_http.progress = millis();
      return;
    }
  }

  if (isSetup) {
    if (get || head) {
      char pw[64] = {}, confirm[64] = {};
      const bool havePw = queryValue(query, "password", pw, sizeof(pw));
      const bool haveConfirm = queryValue(query, "confirm", confirm, sizeof(confirm));
      if (!havePw || strlen(pw) < 4) {
        respond(200, "OK", "text/html; charset=utf-8", kSetupPage, strlen(kSetupPage));
      } else if (!haveConfirm || strcmp(pw, confirm) != 0) {
        respond(200, "OK", "text/html; charset=utf-8", kSetupMismatchPage, strlen(kSetupMismatchPage));
      } else {
        settings::setWebPassword(String(pw));
        BR_LOGI(kTag, "setup page stored the password and signed the user in");
        issueSessionCookie();
      }
      return;
    }
  }

  if (get || head) {
    if (strcmp(target, "/") == 0) respond(200, "OK", "text/html; charset=utf-8", kIndexHtmlGz, kIndexHtmlGzLen, true, head);
    else if (strcmp(target, "/app.css") == 0) respond(200, "OK", "text/css; charset=utf-8", kIndexCssGz, kIndexCssGzLen, true, head);
    else if (strcmp(target, "/app.js") == 0) respond(200, "OK", "application/javascript; charset=utf-8", kIndexJsGz, kIndexJsGzLen, true, head);
    else if (strcmp(target, "/api/status") == 0) handleStatus(head);
    else if (strcmp(target, "/api/bindings") == 0) handleBindings(head);
    else if (strcmp(target, "/api/token") == 0) {
      // In setup mode (no password yet) there is no derived token - hand back
      // a placeholder so the page can still open the websocket (the /ws gate
      // allows setup mode regardless of the token value).
      const String tok = settings::hasWebPassword() ? settings::webToken()
                                                    : String("setup");
      const int n = snprintf(s_http.io, sizeof(s_http.io), "{\"token\":\"%s\"}",
                             tok.c_str());
      respond(200, "OK", "application/json", s_http.io, n > 0 ? (size_t)n : 0);
    }
    else if (strcmp(target, "/ws") == 0 && get) {
      // Upgrade only: a plain GET here is a mistake, not a page request.
      char key[64] = {};
      for (char *line = strstr(s_http.io, "\r\n"); line && line[2];) {
        line += 2;
        char *end = strstr(line, "\r\n");
        if (!end || end == line) break;
        const char saved = *end; *end = 0;
        if (strncasecmp(line, "Sec-WebSocket-Key:", 18) == 0) sscanf(line + 18, "%63s", key);
        *end = saved; line = end;
      }
      if (!key[0]) errorResponse(400, "Bad Request", "missing Sec-WebSocket-Key");
      else wsHandshake(key);
    }
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
  // The single exchange slot. A socket that has already been answered only keeps
  // it for one more request, and only for kKeepAliveIdleMs: browsers open 4-6
  // parallel connections (page, css, js, upgrade) and every parked socket used
  // to hold the slot for the full 4 s progress timeout, so the upgrade request -
  // always last in line - never got served at all. Measured before the fix: five
  // sockets open, upgrade request unanswered for 8+ seconds.
  if (s_http.fd < 0) {
    const int fd = accept(s_listener, nullptr, nullptr);
    if (fd >= 0) {
      if (!nonblocking(fd)) {
        ::close(fd);
      } else {
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        // Linger 0: close() sends RST instead of starting a graceful shutdown.
        // The response bytes are already gone by then, so the client sees a
        // complete reply; what it avoids is the server-side TIME_WAIT pile-up.
        // With a 150 ms poll and a 300 ms keep-alive idle window the board
        // churns through connections fast, and every lingering pcb holds a TCP
        // control block - that pile-up is what drove the free heap down to a few
        // hundred bytes.
        struct linger lg {};
        lg.l_onoff = 1;
        lg.l_linger = 0;
        setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        s_http.fd = fd;
        s_http.used = 0;
        s_http.sending = false;
        s_http.idle = false;
        s_http.started = s_http.progress = millis();
      }
    }
    // No `return` on a failed accept: after the upgrade hands its fd to the
    // websocket there is usually no HTTP connection at all, and bailing out here
    // would stop servicing the live websocket - frames, pings and key events
    // would never leave the board.
  }
  const uint32_t now = millis();
  if (s_ws) {
    // 1) Drain the frame being sent.
    if (s_wsOutSent < s_wsOutLen) {
      const int n = send(s_wsFd, s_wsOutBuf + s_wsOutSent, s_wsOutLen - s_wsOutSent, MSG_DONTWAIT);
      if (n > 0) {
        s_wsOutSent += (size_t)n;
        s_http.progress = now;
        return;
      }
      if (n < 0 && retryable(errno)) {
        /* window full: try again next pass */
      } else {
        wsClose();   /* only the websocket: the HTTP slot is a separate socket */
      }
    }
    // 2) Read whatever the page sent. Any byte counts as liveness, and the
    //    browser answers our pings without the page doing anything.
    if (s_wsInLen < sizeof(s_wsInBuf)) {
      const int n = recv(s_wsFd, s_wsInBuf + s_wsInLen, sizeof(s_wsInBuf) - s_wsInLen, MSG_DONTWAIT);
      if (n > 0) {
        s_wsInLen += (size_t)n;
        s_http.progress = now;
        s_wsLastRecvMs = now;
        wsConsume();
      } else if (n == 0) {
        wsClose();
      } else if (!retryable(errno)) {
        wsClose();
      }
    }
    // If the peer just closed, skip the rest of the websocket work but DO NOT
    // return: the HTTP slot is a different socket and still needs servicing.
    if (s_ws) {
    if (now - s_wsLastRecvMs > kWsDeadAfterMs) {
      BR_LOGW(kTag, "websocket peer silent for %lu ms - dropping it so the server stays usable",
              (unsigned long)(now - s_wsLastRecvMs));
      wsClose();
    }
    if (now - s_wsLastPingMs >= kWsPingEveryMs) {
      s_wsLastPingMs = now;
      static const uint8_t kNoPayload[1] = {0};
      wsQueueRaw(kNoPayload, 0, 0x9);  // ping; a live browser answers with pong
    }
    // 3) Push events: a key the instant it is forwarded, plus a slow status
    //    heartbeat so the page sees link changes and knows we are alive.
    if (s_wsOutSent >= s_wsOutLen) {
      if (bridge::keyPresses() != s_wsLastKeyCount) {
        s_wsLastKeyCount = bridge::keyPresses();
        s_wsLastStatusMs = now;
        char buf[256];
        Json out(buf, sizeof(buf));
        out.add("{\"type\":\"key\",\"keyPresses\":%lu,\"lastKey\":%u,\"activeKey\":%u}",
            (unsigned long)bridge::keyPresses(), bridge::lastKeyRaw(), bridge::activeRawCode());
        if (out.ok()) wsQueueRaw((const uint8_t *)buf, out.size(), 0x1);
      } else if (now - s_wsLastStatusMs >= kWsStatusEveryMs) {
        s_wsLastStatusMs = now;
        wsSendStatus();
      }
    }
    }
  }
  // Everything below services the HTTP exchange slot, so it must not run when
  // there is no HTTP socket: after the upgrade hands its fd to the websocket,
  // s_http.fd is -1. Running anyway is what let the request timeout fire four
  // seconds after a successful upgrade and tear the live page down - and what
  // made the recv() below fail on fd -1 and close the exchange (and, in the old
  // code, the websocket with it).
  if (s_http.fd < 0) return;
  const uint32_t idleLimit = s_http.idle ? kKeepAliveIdleMs : kProgressTimeoutMs;
  if (now - s_http.progress > idleLimit || now - s_http.started > kRequestTimeoutMs) {
    // A parked keep-alive socket expiring is routine, not a stall - log only the
    // real thing.
    if (!s_http.idle) {
      BR_LOGW(kTag, "HTTP timeout (header %u/%u, body %u/%u)", (unsigned)s_http.headerSent,
          (unsigned)s_http.headerLen, (unsigned)s_http.bodySent, (unsigned)s_http.bodyLen);
    }
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
  const size_t left = header ? s_http.headerLen - s_http.headerSent : s_http.bodyLen - s_http.bodySent;
  if (!left) {
    if (s_pendingUpgrade) {
      // Newest connection wins: a stale tab must not lock everyone else out,
      // so a fresh upgrade force-closes the previous socket. The old page sees
      // the close and reconnects - becoming the newest itself.
      if (s_ws) {
        BR_LOGI(kTag, "new websocket replaces the previous one");
        wsClose();
      }
      s_pendingUpgrade = false;
      s_ws = true;  // the 101 is out; from here on this socket speaks frames
      s_wsFd = s_http.fd;   // dedicated fd: HTTP keeps accepting other requests
      s_http.fd = -1;
      s_http.sending = false;
      s_http.idle = false;
      s_http.used = 0;
      s_http.headerLen = s_http.headerSent = 0;
      s_http.bodyLen = s_http.bodySent = 0;
      BR_LOGI(kTag, "websocket open on fd %d, heap %u B", s_wsFd, (unsigned)ESP.getFreeHeap());
    } else {
      closeExchange(true);
    }
    return;
  }
  // Push the response out in a tight loop for a few milliseconds instead of
  // one block per loop() pass. The old shape sent 384 bytes per pass, so a
  // 6 KB script took sixteen passes - and every pass is a window where a BLE
  // coexistence hiccup can stall the transfer past the 4 s progress timeout,
  // killing the response. That is exactly how the page ended up rendering its
  // HTML while /app.js never arrived, leaving a blank screen with no script.
  // The budget keeps the loop responsive: key dispatch is delayed by at most
  // kSendBudgetMs, and only while a response is actually draining.
  const uint32_t budgetEnd = now + kSendBudgetMs;
  for (;;) {
    const bool headerNow = s_http.headerSent < s_http.headerLen;
    const char *chunkSrc = headerNow ? s_http.header + s_http.headerSent
                                     : s_http.body + s_http.bodySent;
    const size_t remain = headerNow ? s_http.headerLen - s_http.headerSent
                                    : s_http.bodyLen - s_http.bodySent;
    if (remain == 0) break;
    const size_t chunk = remain < kIoPerLoop ? remain : kIoPerLoop;
    const int n = send(s_http.fd, chunkSrc, chunk, MSG_DONTWAIT);
    if (n < 0) {
      if (retryable(errno)) {
        ++s_backPressure;
        s_http.progress = millis();   // waiting on the peer window is progress, not a stall
        break;
      }
      BR_LOGW(kTag, "HTTP send failed, errno %d", errno);
      closeExchange(false);
      return;
    }
    if (n == 0) break;
    if (headerNow) s_http.headerSent += (size_t)n;
    else s_http.bodySent += (size_t)n;
    s_http.progress = millis();
    if (millis() - budgetEnd < 0x80000000u && (int32_t)(millis() - budgetEnd) >= 0) break;
  }
  if (s_http.headerSent == s_http.headerLen && s_http.bodySent == s_http.bodyLen) {
    if (s_pendingUpgrade) {
      // The 101 is fully out. Do NOT reset the exchange here: the upgrade is
      // acted on by the `!left` branch above, which needs s_http.sending to
      // still be true on the next pass. Resetting first made that branch
      // unreachable, so the socket sent 101 and then just sat there until the
      // idle timeout closed it - which the page saw as "connected, dropped,
      // reconnecting" on an endless loop.
      return;
    }
    // Keep the connection open for one more request, but park it as `idle` so a
    // client that is actually waiting can take the slot away immediately. The
    // page's only long-lived connection is the websocket, so this window exists
    // purely to let / reuse the socket for /app.css and /app.js.
    s_http.sending = false;
    s_http.idle = true;
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
