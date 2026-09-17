/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * improv_serial.cpp - Improv Wi-Fi provisioning over the USB serial console
 *
 * See improv_serial.h for why this exists and what it talks to.
 *
 * Two deliberate departures from a naive implementation, both because this
 * loop also forwards every remote key press to the host:
 *
 *   - The Wi-Fi scan is asynchronous. A blocking scan parks the loop for
 *     seconds, and a key released during that window would stay stuck down on
 *     the host until the scan finished.
 *   - Every reply is assembled in full and handed to Serial in a single
 *     write. Log lines are emitted from the BLE callbacks on other tasks, and
 *     a frame split by one of those would have to be resynchronised by the
 *     client. One write does not make it impossible, but it makes it rare.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "improv_serial.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "settings.h"
#include "wifi_ui.h"

namespace improv_serial {
namespace {

constexpr char kTag[] = "IMPROV";

// ------------------------------------------------------------------ protocol

constexpr char kMagic[] = {'I', 'M', 'P', 'R', 'O', 'V'};
constexpr uint8_t kMagicLen = 6;
constexpr uint8_t kProtocolVersion = 0x01;

// Packet types. A client only ever sends kPktRpcCommand.
constexpr uint8_t kPktCurrentState = 0x01;  // device -> client
constexpr uint8_t kPktErrorState = 0x02;    // device -> client
constexpr uint8_t kPktRpcCommand = 0x03;    // client -> device
constexpr uint8_t kPktRpcResult = 0x04;     // device -> client

// States. The serial dialect has no 0x01 (that is the BLE "authorization
// required" value) - a serial client can always hand over credentials.
constexpr uint8_t kStateAuthorized = 0x02;
constexpr uint8_t kStateProvisioning = 0x03;
constexpr uint8_t kStateProvisioned = 0x04;

// Errors. The serial dialect has no 0x04 (BLE "not authorized").
constexpr uint8_t kErrNone = 0x00;
constexpr uint8_t kErrInvalidRpc = 0x01;
constexpr uint8_t kErrUnknownCommand = 0x02;
constexpr uint8_t kErrUnableToConnect = 0x03;
constexpr uint8_t kErrBadHostname = 0x05;

// RPC commands.
constexpr uint8_t kCmdWifiSettings = 0x01;
constexpr uint8_t kCmdIdentify = 0x02;  // serial: "report your state"
constexpr uint8_t kCmdDeviceInfo = 0x03;
constexpr uint8_t kCmdScanWifi = 0x04;
constexpr uint8_t kCmdHostname = 0x05;
constexpr uint8_t kCmdDeviceName = 0x06;
constexpr uint8_t kCmdNetworkState = 0x07;

// The longest RPC command is Wi-Fi settings: 1 + 32 (SSID) + 1 + 63 (password)
// = 97 bytes. A declared length beyond this ceiling means the "IMPROV" we found
// was ordinary console text, not a frame.
constexpr uint8_t kMaxFrameData = 128;

// Scan results are the only large replies. Eight networks at
// 1+32 + 1+5 + 1+4 bytes each stays inside the buffer below, and the trailing
// empty result still fits - which matters, because the serial dialect uses that
// empty result to say "the list ends here".
constexpr size_t kMaxScanNetworks = 8;
constexpr size_t kMaxResultStrings = kMaxScanNetworks * 3;
constexpr size_t kMaxResultBytes = 512;

constexpr unsigned long kConnectTimeoutMs = 20000;
constexpr unsigned long kScanTimeoutMs = 12000;

// ------------------------------------------------------------------- parsing

// The parser is a byte-at-a-time state machine rather than a buffer scan, so it
// can say precisely which bytes it owns. Bytes it does not own go to the
// console untouched.
enum class Parse : uint8_t { Idle, Magic, Header, Data, Checksum };

Parse s_parse = Parse::Idle;
uint8_t s_cand[kMagicLen] = {};  // held magic candidate, released if it fails
uint8_t s_candLen = 0;
uint8_t s_header[3] = {};  // version, type, length
uint8_t s_headerLen = 0;
uint8_t s_data[kMaxFrameData] = {};
uint8_t s_dataLen = 0;  // declared
uint8_t s_dataGot = 0;
uint8_t s_sum = 0;  // running checksum over magic + header + data

// -------------------------------------------------------------------- state

uint8_t s_state = kStateAuthorized;
uint8_t s_error = kErrNone;
bool s_provisioning = false;
unsigned long s_provisionStartedAt = 0;

bool s_scanPending = false;
unsigned long s_scanStartedAt = 0;

// Improv's hostname and device-name commands are session scoped: the identity
// that matters is the compile-time one in config.h, and both values are only
// ever reported back to the client that set them.
char s_hostname[32] = "miremotebridge";
char s_deviceName[40] = BRIDGE_HID_DEVICE_NAME;

// ------------------------------------------------------------------ sending

// True once the config page is actually reachable, which is what the redirect
// URL needs. The fallback access point does not count: it is a different
// network, and a client told to open an address on it would be stranded.
bool stationOnline() {
  return wifi_ui::ready() && strcmp(wifi_ui::mode(), "sta") == 0;
}

bool deviceUrl(char *out, size_t cap) {
  if (!stationOnline()) return false;
  snprintf(out, cap, "http://%s/", wifi_ui::ip());
  return true;
}

void sendPacket(uint8_t type, const uint8_t *data, size_t len) {
  static uint8_t frame[kMagicLen + 3 + kMaxResultBytes + 1];
  if (len > kMaxResultBytes) len = kMaxResultBytes;

  size_t pos = 0;
  memcpy(frame + pos, kMagic, kMagicLen);
  pos += kMagicLen;
  frame[pos++] = kProtocolVersion;
  frame[pos++] = type;
  frame[pos++] = static_cast<uint8_t>(len);
  if (len > 0 && data != nullptr) {
    memcpy(frame + pos, data, len);
    pos += len;
  }
  uint8_t sum = 0;
  for (size_t i = 0; i < pos; ++i) sum += frame[i];
  frame[pos++] = sum;

  Serial.write(frame, pos);
  Serial.flush();
}

void sendState(uint8_t state) {
  const uint8_t payload[1] = {state};
  sendPacket(kPktCurrentState, payload, 1);
}

void sendError(uint8_t error) {
  const uint8_t payload[1] = {error};
  sendPacket(kPktErrorState, payload, 1);
}

void setState(uint8_t next) {
  if (s_state == next) return;
  s_state = next;
  sendState(s_state);
}

// The spec wants the error state cleared on every command so the client can
// tell "being processed" from "stuck", so this reports unconditionally.
void setError(uint8_t error) {
  s_error = error;
  sendError(s_error);
}

// Result payload: [command][rest length][string length][string]...
//
// The length field is a single byte, so a payload can never exceed 255 bytes.
// Callers that may produce more than that (the scan list) have to chunk - see
// pollScan(); the cap here is the backstop that keeps a long reply from
// silently truncating its own length field.
constexpr size_t kMaxResultPayload = 256;

void sendResult(uint8_t command, const char *const *strings, size_t count) {
  static uint8_t payload[kMaxResultBytes];
  size_t pos = 0;
  payload[pos++] = command;
  const size_t lengthSlot = pos++;

  for (size_t i = 0; i < count; ++i) {
    const char *value = (strings && strings[i]) ? strings[i] : "";
    size_t n = strlen(value);
    if (n > 255) n = 255;
    if (pos + 1 + n > kMaxResultPayload) break;
    payload[pos++] = static_cast<uint8_t>(n);
    memcpy(payload + pos, value, n);
    pos += n;
  }

  payload[lengthSlot] = static_cast<uint8_t>(pos - 2);
  sendPacket(kPktRpcResult, payload, pos);
}

// ----------------------------------------------------------------- handlers

void handleScanWifi() {
  if (s_scanPending) return;  // already running; its result is on the way

  // Something else got a scan going first. Adopt it rather than dropping the
  // request on the floor: pollScan() will report whatever it produces.
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
    s_scanPending = true;
    s_scanStartedAt = millis();
    return;
  }

  WiFi.scanDelete();
  // Asynchronous on purpose - see the file header. The result is collected in
  // pollScan().
  WiFi.scanNetworks(true /*async*/, false /*show hidden*/);
  s_scanPending = true;
  s_scanStartedAt = millis();
}

void pollScan() {
  if (!s_scanPending) return;

  const int16_t found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    if (millis() - s_scanStartedAt < kScanTimeoutMs) return;
    BR_LOGW(kTag, "Wi-Fi scan did not finish within %lu ms; reporting an empty list",
            (unsigned long)kScanTimeoutMs);
    WiFi.scanDelete();
    s_scanPending = false;
    sendResult(kCmdScanWifi, nullptr, 0);
    return;
  }

  s_scanPending = false;

  // The serial dialect differs from the BLE one in two ways, and both are
  // handled here rather than in the protocol core: the security field is
  // spelled YES/NO instead of WPA2/WPA3, and the list is terminated by an
  // extra EMPTY result instead of being one packet of a known length.
  static char values[kMaxResultStrings][40];
  static const char *ptrs[kMaxResultStrings];
  size_t count = 0;

  for (int16_t i = 0; i < found && count / 3 < kMaxScanNetworks; ++i) {
    const String name = WiFi.SSID(i);
    if (name.isEmpty()) continue;  // hidden network: nothing to pick

    snprintf(values[count], sizeof(values[count]), "%s", name.c_str());
    ptrs[count++] = values[count];
    snprintf(values[count], sizeof(values[count]), "%d", (int)WiFi.RSSI(i));
    ptrs[count++] = values[count];
    ptrs[count++] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "NO" : "YES";
  }
  WiFi.scanDelete();

  // Split into as many packets as the one-byte length field allows, always on a
  // whole-network boundary so the client never sees a network described by
  // half its fields. The trailing empty result says "the list ends here".
  size_t first = 0;
  while (first + 3 <= count) {
    size_t bytes = 2;  // command + length
    size_t next = first;
    while (next + 3 <= count) {
      size_t added = 0;
      for (size_t k = 0; k < 3; ++k) added += 1 + strlen(ptrs[next + k]);
      if (bytes + added > kMaxResultPayload) break;
      bytes += added;
      next += 3;
    }
    if (next == first) break;  // a single network does not fit: stop, do not spin
    sendResult(kCmdScanWifi, ptrs + first, next - first);
    first = next;
  }
  sendResult(kCmdScanWifi, nullptr, 0);  // "that is all"
}

void handleWifiSettings(const uint8_t *data, size_t len) {
  // Payload: [ssid length][ssid][password length][password]
  size_t pos = 0;
  if (len < 1) {
    setError(kErrInvalidRpc);
    return;
  }

  const uint8_t ssidLen = data[pos++];
  if (pos + ssidLen > len) {
    setError(kErrInvalidRpc);
    return;
  }
  const String ssid(reinterpret_cast<const char *>(data + pos), ssidLen);
  pos += ssidLen;

  if (pos >= len) {
    setError(kErrInvalidRpc);
    return;
  }
  const uint8_t passLen = data[pos++];
  if (pos + passLen > len) {
    setError(kErrInvalidRpc);
    return;
  }
  const String password(reinterpret_cast<const char *>(data + pos), passLen);

  if (ssid.isEmpty()) {
    setError(kErrInvalidRpc);
    return;
  }

  // Persist first, then join: the credentials survive a reboot even if the
  // association fails, and the always-on auto-start picks them up next boot.
  settings::setWifi(ssid, password);
  BR_LOGI(kTag, "credentials for \"%s\" received; joining", ssid.c_str());

  if (!wifi_ui::rejoin()) {
    setError(kErrUnableToConnect);
    setState(kStateAuthorized);
    sendResult(kCmdWifiSettings, nullptr, 0);
    return;
  }

  setError(kErrNone);
  setState(kStateProvisioning);
  s_provisioning = true;
  s_provisionStartedAt = millis();
}

void handleDeviceInfo() {
  const char *info[4] = {BRIDGE_FW_NAME, BRIDGE_FW_VERSION, "esp32c3/esp32-c3", s_deviceName};
  sendResult(kCmdDeviceInfo, info, 4);
}

void handleNetworkState() {
  // First string is the flag set in decimal: bit0 = on a network,
  // bit1 = this device speaks Wi-Fi.
  uint8_t flags = 0x02;
  if (stationOnline()) flags |= 0x01;

  char flagText[8];
  snprintf(flagText, sizeof(flagText), "%u", (unsigned)flags);

  char url[40];
  if (deviceUrl(url, sizeof(url))) {
    const char *out[2] = {flagText, url};
    sendResult(kCmdNetworkState, out, 2);
  } else {
    const char *out[1] = {flagText};
    sendResult(kCmdNetworkState, out, 1);
  }
}

void handleHostnameOrName(uint8_t command, const uint8_t *data, size_t len) {
  // Empty payload = read, anything else = write.
  if (len > 0) {
    const String value(reinterpret_cast<const char *>(data), len);
    if (command == kCmdHostname) {
      // RFC 1123: letters, digits and hyphens only.
      bool ok = value.length() > 0 && value.length() < sizeof(s_hostname);
      for (size_t i = 0; ok && i < value.length(); ++i) {
        const char c = value[i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '-';
      }
      if (!ok) {
        setError(kErrBadHostname);
        return;
      }
      snprintf(s_hostname, sizeof(s_hostname), "%s", value.c_str());
      WiFi.setHostname(s_hostname);
    } else {
      snprintf(s_deviceName, sizeof(s_deviceName), "%s", value.c_str());
    }
  }

  const char *out[1] = {(command == kCmdHostname) ? s_hostname : s_deviceName};
  sendResult(command, out, 1);
}

void handleCommand(uint8_t command, const uint8_t *data, size_t len) {
  setError(kErrNone);

  switch (command) {
    case kCmdWifiSettings:
      handleWifiSettings(data, len);
      break;

    case kCmdIdentify:
      // Serial dialect: "report your current state". (The BLE dialect uses the
      // same number for "flash an LED so the user can tell which device this
      // is"; this board's two LEDs carry link meaning, so nothing flashes.)
      //
      // The result that follows MUST carry this command's own number, not
      // 0x01. The spec calls byte 1 of an RPC result "the command being
      // responded to", and improv-wifi-serial-sdk enforces it: the client
      // keeps one pending-RPC slot and drops any result whose command byte
      // does not match, logging "Received result for command X but expected
      // Y". That matters here more than anywhere else, because this result is
      // the ONLY way a client learns nextUrl from an already-provisioned
      // device - requestCurrentState() awaits it and otherwise hangs until its
      // timeout, taking the whole handshake down with it. Sending 0x01 is what
      // made esp-web-tools lose its Wi-Fi entry the moment the board joined a
      // network: with the radio off the client returns early and never waits
      // for this packet, so the bug stayed invisible until the device was
      // actually provisioned.
      sendState(s_state);
      {
        char url[40];
        if (deviceUrl(url, sizeof(url))) {
          const char *out[1] = {url};
          sendResult(kCmdIdentify, out, 1);
        }
      }
      break;

    case kCmdDeviceInfo:
      handleDeviceInfo();
      break;

    case kCmdScanWifi:
      handleScanWifi();
      break;

    case kCmdHostname:
    case kCmdDeviceName:
      handleHostnameOrName(command, data, len);
      break;

    case kCmdNetworkState:
      handleNetworkState();
      break;

    default:
      BR_LOGW(kTag, "unknown RPC command 0x%02x", (unsigned)command);
      setError(kErrUnknownCommand);
      break;
  }
}

}  // namespace

// --------------------------------------------------------------------- API

void begin() {
  s_parse = Parse::Idle;
  s_candLen = 0;
  s_state = kStateAuthorized;
  s_provisioning = false;
  BR_LOGI(kTag, "serial provisioning ready (Improv v1) - ESPHome Web / esp-web-tools");
}

bool feedByte(uint8_t byte, ConsoleSink sink) {
  for (;;) {
    switch (s_parse) {
      case Parse::Idle:
        if (byte == (uint8_t)kMagic[0]) {
          s_cand[0] = byte;
          s_candLen = 1;
          s_sum = byte;
          s_parse = Parse::Magic;
          return true;
        }
        return false;

      case Parse::Magic:
        if (byte == (uint8_t)kMagic[s_candLen]) {
          s_cand[s_candLen++] = byte;
          s_sum += byte;
          if (s_candLen == kMagicLen) {
            s_headerLen = 0;
            s_parse = Parse::Header;
          }
          return true;
        }
        // Not a frame after all. Hand the held bytes back to the console and
        // re-test this one from the start, so a line that merely begins with
        // 'I' still reaches the console intact. "IMPROV" has no prefix that is
        // also a suffix, so this byte is the only possible new candidate.
        for (uint8_t i = 0; i < s_candLen; ++i) {
          if (sink) sink(static_cast<char>(s_cand[i]));
        }
        s_candLen = 0;
        s_parse = Parse::Idle;
        continue;

      case Parse::Header:
        s_header[s_headerLen++] = byte;
        s_sum += byte;
        if (s_headerLen < 3) return true;
        // A client only ever sends RPC commands, and the version has to match.
        // Anything else means the magic was a coincidence: drop the frame
        // rather than feeding binary noise to the console.
        if (s_header[0] != kProtocolVersion || s_header[1] != kPktRpcCommand ||
            s_header[2] > kMaxFrameData) {
          s_parse = Parse::Idle;
          return true;
        }
        s_dataLen = s_header[2];
        s_dataGot = 0;
        s_parse = (s_dataLen == 0) ? Parse::Checksum : Parse::Data;
        return true;

      case Parse::Data:
        s_data[s_dataGot++] = byte;
        s_sum += byte;
        if (s_dataGot == s_dataLen) s_parse = Parse::Checksum;
        return true;

      case Parse::Checksum:
        s_parse = Parse::Idle;
        if (byte != s_sum) {
          BR_LOGW(kTag, "bad checksum (got 0x%02x, expected 0x%02x)", (unsigned)byte,
                  (unsigned)s_sum);
          setError(kErrInvalidRpc);
          return true;
        }
        // Payload: [command][length][data...]
        if (s_dataLen < 2) {
          setError(kErrInvalidRpc);
          return true;
        }
        {
          const uint8_t command = s_data[0];
          const uint8_t commandLen = s_data[1];
          const size_t available = s_dataLen - 2;
          const size_t usable = (commandLen < available) ? commandLen : available;
          handleCommand(command, s_data + 2, usable);
        }
        return true;
    }
    return false;  // unreachable: every Parse value returns above
  }
}

void loop() {
  pollScan();

  const bool online = stationOnline();

  if (s_provisioning) {
    if (online) {
      s_provisioning = false;
      setState(kStateProvisioned);
      char url[40];
      if (deviceUrl(url, sizeof(url))) {
        // The redirect URL is the first string of the result, and it is the
        // entire source of the "open the device page" step on the client.
        const char *out[1] = {url};
        sendResult(kCmdWifiSettings, out, 1);
      }
    } else if (millis() - s_provisionStartedAt > kConnectTimeoutMs) {
      // Back to "ready for credentials" rather than staying in Provisioning,
      // otherwise the user has no way to correct a typo.
      s_provisioning = false;
      setError(kErrUnableToConnect);
      setState(kStateAuthorized);
      sendResult(kCmdWifiSettings, nullptr, 0);
    }
    return;
  }

  // Nobody is provisioning right now: keep the reported state in step with the
  // live link, so a client that connects later still learns the URL. setState()
  // only emits on an actual change.
  setState(online ? kStateProvisioned : kStateAuthorized);
}

}  // namespace improv_serial
