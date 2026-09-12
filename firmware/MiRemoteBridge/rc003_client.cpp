/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * rc003_client.cpp - upstream BLE central role
 *
 * Design notes
 * -----------
 * * A dedicated FreeRTOS task owns every blocking BLE central operation
 *   (scanning, connecting, service discovery, subscribing). The Arduino loop
 *   stays free to drain the event queue and push HID reports, so a slow scan or
 *   an 8-second connect attempt can never delay a keystroke.
 * * GATT notification callbacks run in the NimBLE host task. They do the bare
 *   minimum: parse the report into a normalised press/release and push it onto
 *   the SPSC event queue. They never touch the GATT server.
 * * Reconnection prefers the saved identity address and the stored bond; it
 *   only falls back to scanning when a direct connect fails. After every
 *   successful reconnect the services are discovered again and the
 *   notifications are re-subscribed.
 *
 * Address handling (important)
 * ---------------------------
 * Every address in this module is kept in the standard textual form
 * "aa:bb:cc:dd:ee:ff" - the one BLEAddress::toString() prints and the one
 * Windows shows. That is also the form BLEAddress(const String&, type) parses.
 *
 * NimBLE stores address bytes in the reverse order internally, so building the
 * text form from BLEAddress::getNative() yields a byte-reversed address, and
 * feeding that back into BLEAddress() would page the wrong device. Everything
 * below therefore goes through toString() / the String constructor and never
 * uses getNative() for the upstream peer.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rc003_client.h"
#include "native_subscription.h"

#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLERemoteCharacteristic.h>
#include <BLERemoteService.h>
#include <BLEScan.h>

#include <host/ble_gap.h>
#include <host/ble_store.h>
#include <host/ble_gatt.h>
#include <os/os_mbuf.h>

#include <ctype.h>
#include <string.h>

#include "ble_bonds.h"
#include "config.h"
#include "event_bus.h"
#include "key_definitions.h"
#include "keymap.h"
#include "log.h"
#include "rc003_report.h"
#include "settings.h"
#include "bridge.h"
#include "hid_server.h"

namespace {

static const char *kTag = "RC";
static const char *kTagScan = "SCAN";
static const char *kTagGatt = "GATT";
static const char *kTagSec = "SEC";

// A standard BLE address text form is always 17 characters; +1 for the NUL.
constexpr size_t kAddrStrLen = 18;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
enum class St : uint8_t {
  IDLE = 0,
  DIRECT_CONNECT,   // try the saved address first
  SCANNING,
  CONNECTING,
  DISCOVERING,
  READY,            // connected + subscribed
  BACKOFF,          // idle between scan bursts
};

St s_state = St::IDLE;
volatile bool s_slotBusy = false;
volatile bool s_slotPaused = false;
uint8_t s_slotTarget = 0, s_slotAction = 0;
const char *s_slotError = "";


BLEClient *s_client = nullptr;
BLERemoteCharacteristic *s_charReport = nullptr;
BLERemoteCharacteristic *s_charAtvvCtl = nullptr;
BLERemoteCharacteristic *s_charBattery = nullptr;

struct NotifyHandle {uint16_t handle;bool atvv;};
NotifyHandle s_notifyHandles[8];
volatile uint8_t s_notifyHandleCount=0;
bool s_refreshServices=false;
String s_connectedAddr;
String s_newPeer;
uint8_t s_newPeerType=0;
String s_connectedName;
int s_lastRssi = 0;
uint32_t s_lastReportMs = 0;
uint32_t s_notifyCount = 0;
bool s_subscribed = false;

// Charge last reported by the remote. Stays invalid until a read or a
// notification succeeds, which is what distinguishes "not known yet" from a
// genuine 0%.
uint8_t s_remoteBattery = 0;
bool s_remoteBatteryValid = false;
uint32_t s_lastBatteryReadMs = 0;

rc003_tracker_t s_tracker;

// Timing / supervision
uint32_t s_lastDirectAttemptMs = 0;
uint32_t s_scanBurstStartMs = 0;
uint32_t s_idleUntilMs = 0;
int s_directAttempts = 0;

// Statistics
uint32_t s_scanStarts = 0;
uint32_t s_connectAttempts = 0;
uint32_t s_connectSuccesses = 0;
uint32_t s_subscriptionFailures = 0;

TaskHandle_t s_taskHandle = nullptr;

// ---------------------------------------------------------------------------
// Requests from the console (loop task) to the central task
// ---------------------------------------------------------------------------
portMUX_TYPE s_reqMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_reqScanNow = false;
volatile bool s_reqReconnect = false;
volatile bool s_reqForget = false;
volatile bool s_reqConnect = false;
char s_reqAddr[kAddrStrLen] = {0};
uint8_t s_reqAddrType = BLE_ADDR_PUBLIC;
char s_reqName[32] = {0};

// ---------------------------------------------------------------------------
// Pending target discovered by the scanner
// ---------------------------------------------------------------------------
portMUX_TYPE s_pendMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_pendValid = false;
char s_pendAddr[kAddrStrLen] = {0};
uint8_t s_pendAddrType = BLE_ADDR_PUBLIC;
char s_pendName[48] = {0};
int s_pendRssi = 0;

// ---------------------------------------------------------------------------
// Advertisement cache for the `scan` console command
// ---------------------------------------------------------------------------
constexpr size_t kNearbyMax = 16;

struct NearbyEntry {
  bool used;
  char addr[kAddrStrLen];
  uint8_t addrType;
  // Sized for a localised name: "小米蓝牙语音遥控器" is 27 UTF-8 bytes, and a
  // shorter buffer cut it mid-character, printing a broken glyph tail in `scan`.
  char name[48];
  int rssi;
  uint32_t lastSeenMs;
};

NearbyEntry s_nearby[kNearbyMax];
portMUX_TYPE s_nearbyMux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
uint32_t nowMs() { return millis(); }

void setState(St next, const char *why) {
  if (s_state == next) return;
  s_state = next;
  BR_LOGI(kTag, "state -> %s (%s)", rc003_client::stateName(), why ? why : "");
}

// Case-insensitive substring search. String::indexOf() is case sensitive, and
// advertised names arrive in whatever case the vendor chose ("MI RC" vs "Mi
// Rc"). Bytes >= 0x80 - the Chinese name hints - are compared verbatim because
// tolower() leaves them alone in the C locale, so a UTF-8 fragment still has to
// match exactly.
bool containsIgnoreCase(const String &haystack, const char *needle) {
  const size_t needleLen = strlen(needle);
  if (needleLen == 0) return false;
  const size_t hayLen = haystack.length();
  if (hayLen < needleLen) return false;
  const char *hay = haystack.c_str();
  for (size_t i = 0; i + needleLen <= hayLen; i++) {
    size_t j = 0;
    while (j < needleLen && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
    if (j == needleLen) return true;
  }
  return false;
}

// Returns the hint that identified the name, or nullptr. Returning the hint
// rather than a bool lets the log name the evidence, which is the first thing
// needed when a future remote turns out to be named differently again.
const char *matchRemoteNameHint(const String &name) {
  if (name.length() == 0) return nullptr;
  static const char *const kHints[] = {
      RC003_NAME_HINT_1, RC003_NAME_HINT_2, RC003_NAME_HINT_3,
      RC003_NAME_HINT_4, RC003_NAME_HINT_5, RC003_NAME_HINT_6,
  };
  for (const char *hint : kHints) {
    if (containsIgnoreCase(name, hint)) return hint;
  }
  return nullptr;
}

void copyFixed(char *dst, size_t dstLen, const char *src) {
  if (!dst || dstLen == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';
}

void rememberNearby(const String &addr, uint8_t addrType, const String &name, int rssi) {
  if (addr.length() == 0) return;

  portENTER_CRITICAL(&s_nearbyMux);
  int freeSlot = -1;
  int oldest = 0;
  uint32_t oldestAge = 0;
  for (size_t i = 0; i < kNearbyMax; i++) {
    if (s_nearby[i].used && addr.equalsIgnoreCase(s_nearby[i].addr)) {
      if (name.length() > 0) copyFixed(s_nearby[i].name, sizeof(s_nearby[i].name), name.c_str());
      s_nearby[i].addrType = addrType;
      s_nearby[i].rssi = rssi;
      s_nearby[i].lastSeenMs = nowMs();
      portEXIT_CRITICAL(&s_nearbyMux);
      return;
    }
    if (!s_nearby[i].used && freeSlot < 0) freeSlot = (int)i;
    if (s_nearby[i].used) {
      const uint32_t age = nowMs() - s_nearby[i].lastSeenMs;
      if (age >= oldestAge) {
        oldestAge = age;
        oldest = (int)i;
      }
    }
  }
  const int slot = (freeSlot >= 0) ? freeSlot : oldest;
  memset(&s_nearby[slot], 0, sizeof(NearbyEntry));
  s_nearby[slot].used = true;
  copyFixed(s_nearby[slot].addr, sizeof(s_nearby[slot].addr), addr.c_str());
  s_nearby[slot].addrType = addrType;
  s_nearby[slot].rssi = rssi;
  s_nearby[slot].lastSeenMs = nowMs();
  if (name.length() > 0) copyFixed(s_nearby[slot].name, sizeof(s_nearby[slot].name), name.c_str());
  portEXIT_CRITICAL(&s_nearbyMux);
}

// ---------------------------------------------------------------------------
// Notification callbacks - NimBLE host task context.
//
// Anything that sends HID reports MUST NOT happen here.
// ---------------------------------------------------------------------------
void feedEvents(const rc003_key_event_t *raw, size_t rawCount) {
  if (s_slotBusy) return;
  rc003_key_event_t norm[3];
  for (size_t i = 0; i < rawCount; i++) {
    const size_t n = rc003_tracker_apply(&s_tracker, &raw[i], norm, 3);
    for (size_t k = 0; k < n; k++) {
      event_bus::post(BR_EV_RC_KEY, norm[k].raw_code, norm[k].pressed);
    }
  }
}

void onHogpNotify(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify) {
  (void)isNotify;
  s_lastReportMs = nowMs();
  s_notifyCount++;

  char hex[RC003_KEY_REPORT_MAX_LEN * 3 + 1];
  rc003_hex_dump(data, length, hex, sizeof(hex));

  const rc003_frame_kind_t kind = rc003_classify(data, length);
  if (kind == RC003_FRAME_AUDIO) {
    // Not expected: the audio characteristic is never subscribed. Seeing this
    // means the remote multiplexed audio onto the report characteristic.
    BR_LOGRAW(kTagScan, "audio-sized payload len=%u dropped", (unsigned)length);
    return;
  }

  BR_LOGRAW(kTagScan, "report len=%u [%s] from %s", (unsigned)length, hex,
            characteristic ? characteristic->getUUID().toString().c_str() : "?");

  rc003_key_event_t raw[2];
  const size_t n = rc003_parse_hid_report(data, length, raw, 2);
  if (n == 0) return;

  BR_LOGRAW(kTagScan, "parsed raw=0x%02X (%s) %s", raw[0].raw_code, keymap_raw_name(raw[0].raw_code),
            raw[0].pressed ? "DOWN" : "UP");

  feedEvents(raw, n);
}

void onAtvvCtlNotify(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify) {
  (void)characteristic;
  (void)isNotify;
  s_lastReportMs = nowMs();

  rc003_key_event_t raw[1];
  const size_t n = rc003_parse_atvv_ctl(data, length, raw, 1);
  if (n == 0) return;

  BR_LOGRAW(kTagScan, "atvv ctl op=0x%02X -> VOICE %s", (unsigned)data[0], raw[0].pressed ? "DOWN" : "UP");
  feedEvents(raw, n);
}

// Battery level from the remote's own 0x180F/0x2A19, forwarded to our battery
// service so the host shows the remote's charge rather than a constant.
//
// Callback context: parse and post only, exactly like the key path.
void onBatteryNotify(BLERemoteCharacteristic *characteristic, uint8_t *data, size_t length, bool isNotify) {
  (void)characteristic;
  (void)isNotify;
  if (length < 1) return;

  const uint8_t level = data[0];
  if (level > 100) {
    // Not a percentage. Some devices put a status byte first; rather than guess
    // at the layout, ignore it and say so - a wrong battery reading is worse
    // than none.
    BR_LOGW(kTagGatt, "battery value 0x%02X is not a percentage, ignored", (unsigned)level);
    return;
  }
  // Unchanged: the remote pushes the current level right after subscribing, and
  // some keep re-announcing it. Dropping repeats keeps the console readable -
  // the value is already published from the initial read.
  if (s_remoteBatteryValid && s_remoteBattery == level) return;

  s_remoteBattery = level;
  s_remoteBatteryValid = true;
  event_bus::post(BR_EV_RC_BATTERY, level, false);
}

// Read Battery Level directly, without retaining GATT wrapper objects or blocking keys.
bool s_batteryReadPending=false;
int batteryReadCallback(uint16_t conn,const ble_gatt_error *error,ble_gatt_attr *attr,void*) {
  if(!s_client || !s_client->isConnected() || s_client->getConnId()!=conn) {s_batteryReadPending=false;return 0;}
  if(error->status) {
    s_batteryReadPending=false;
    if(error->status!=BLE_HS_EDONE) BR_LOGW(kTagGatt,"battery read status=%d",error->status);
    return 0;
  }
  uint8_t level;
  if(attr && attr->om && OS_MBUF_PKTLEN(attr->om)==1 && !os_mbuf_copydata(attr->om,0,1,&level) && level<=100) {
    BR_LOGI(kTagGatt,"battery live read=%u%%",level);
    onBatteryNotify(nullptr,&level,1,false);
  }
  return 0;
}
void requestBatteryRead() {
  if(s_batteryReadPending || !s_client || !s_client->isConnected()) return;
  s_lastBatteryReadMs=nowMs();
  static const ble_uuid16_t uuid=BLE_UUID16_INIT(0x2a19);
  s_batteryReadPending=true;
  int rc=ble_gattc_read_by_uuid(s_client->getConnId(),1,0xffff,&uuid.u,batteryReadCallback,nullptr);
  if(rc) {s_batteryReadPending=false;BR_LOGW(kTagGatt,"battery request rc=%d",rc);}
}

// ---------------------------------------------------------------------------
// Client callbacks - NimBLE host task context. Post events only.
// ---------------------------------------------------------------------------
class ClientCallbacks : public BLEClientCallbacks {
 public:
  void onConnect(BLEClient *pClient) override {
    (void)pClient;
    BR_LOGI(kTag, "gatt link established");
    event_bus::post(BR_EV_RC_LINK_UP);
  }

  void onDisconnect(BLEClient *pClient) override {
    (void)pClient;
    BR_LOGW(kTag, "gatt link lost");
    s_charReport = nullptr;
    s_charAtvvCtl = nullptr;
    s_charBattery = nullptr;
    s_batteryReadPending=false;
    s_remoteBatteryValid = false;
    s_subscribed = false;
    s_notifyHandleCount=0;
    rc003_tracker_reset(&s_tracker);
    // The charge is no longer known. It is deliberately not cleared to a
    // default: the host keeps showing the last real value instead of jumping to
    // a fabricated one, and it is re-read on the next reconnect.
    event_bus::post(BR_EV_RC_LINK_DOWN);
  }

#if defined(CONFIG_NIMBLE_ENABLED)
  bool onConnParamsUpdateRequest(BLEClient *pClient, const ble_gap_upd_params *params) override {
    (void)pClient;
    (void)params;
    return true;  // accept whatever the remote asks for
  }
#endif
};

// Read only bounded AD records; malformed broadcasts must never expose adjacent memory.
String advertisedName(BLEAdvertisedDevice &dev) {
  const uint8_t *data=dev.getPayload();const size_t n=dev.getPayloadLength();
  String name;
  for(size_t i=0;data && i<n;) {
    const size_t len=data[i++];
    if(!len) continue;
    if(len>n-i) break;
    const uint8_t type=data[i];
    if(type==8 || type==9) {
      size_t size=len-1;
      if(size>39) size=39;
      // Validate complete UTF-8 sequences and omit control bytes.
      size_t valid=0;
      while(valid<size) {
        uint8_t c=data[i+1+valid];
        size_t width=c<128 ? 1 : c>=0xc2 && c<=0xdf ? 2 : c>=0xe0 && c<=0xef ? 3 : c>=0xf0 && c<=0xf4 ? 4 : 0;
        if(!width || valid+width>size || c<32 || c==127) break;
        bool ok=true;for(size_t j=1;j<width;j++) if((data[i+1+valid+j]&0xc0)!=0x80) ok=false;
        if(!ok) break;
        valid+=width;
      }
      name=String((const char*)data+i+1,valid);
      if(type==9 && name.length()) return name;
    }
    i+=len;
  }
  return name;
}

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
 public:
  void onResult(BLEAdvertisedDevice dev) override {
    // toString() gives the standard text form, which is also what we persist
    // and what the operator sees in Windows. Never use getNative() here.
    const String addr = dev.getAddress().toString();
    const String name = advertisedName(dev);
    const uint8_t addrType = dev.getAddressType();
    const int rssi = dev.getRSSI();

    rememberNearby(addr, addrType, name, rssi);

    if (name.length() > 0) {
      BR_LOGD(kTagScan, "%s %s rssi=%d type=%u", name.c_str(), addr.c_str(), rssi, (unsigned)addrType);
    }

    portENTER_CRITICAL(&s_pendMux);
    const bool alreadyPending = s_pendValid;
    portEXIT_CRITICAL(&s_pendMux);
    if (alreadyPending) return;

    if (!settings::hasRc003()) return; // Every empty slot requires explicit device selection.
    bool match = false;
    const char *why = nullptr;

    if (settings::hasRc003()) {
      // Bound: strict matching. The identity address is stable when the remote
      // uses a public address; when it rotates its resolvable private address
      // the stored name is the only thing left to match on.
      if (settings::rc003Address().equalsIgnoreCase(addr)) {
        match = true;
        why = "saved address";
      }
    } else {
      // Unbound: only accept an obvious remote. A bare 0x1812 HID service is
      // deliberately NOT enough - that would latch onto a neighbour's keyboard.
      if (const char *hint = matchRemoteNameHint(name)) {
        match = true;
        why = hint;
      } else if (dev.haveServiceUUID() && dev.isAdvertisingService(BLEUUID(RC003_ATVV_SVC_UUID))) {
        match = true;
        why = "ATVV service uuid";
      }
    }

    for (uint8_t i=0;i<3;i++) if(i!=settings::activeSlot()) {
      String saved, label; uint8_t type;
      settings::slotInfo(i,saved,label,type);
      if(saved.length() && saved.equalsIgnoreCase(addr)) return;
    }
    if (!match) return;

    BR_LOGI(kTagScan, "target found: \"%s\" %s rssi=%d type=%u (via %s)", name.c_str(), addr.c_str(), rssi,
            (unsigned)addrType, why ? why : "?");

    portENTER_CRITICAL(&s_pendMux);
    copyFixed(s_pendAddr, sizeof(s_pendAddr), addr.c_str());
    s_pendAddrType = addrType;
    s_pendRssi = rssi;
    copyFixed(s_pendName, sizeof(s_pendName), name.c_str());
    s_pendValid = true;
    portEXIT_CRITICAL(&s_pendMux);

    // Stop the scan so the central task wakes up promptly instead of waiting
    // out the rest of the chunk. BLEScan::stop() releases the task blocked in
    // start(); NimBLE allows cancelling a discovery from inside its own
    // callback.
    BLEScan *scan = BLEDevice::getScan();
    if (scan->isScanning()) {
      scan->stop();
    }
  }
};

ClientCallbacks s_clientCallbacks;
ScanCallbacks s_scanCallbacks;

// ---------------------------------------------------------------------------
// Connection / discovery
// ---------------------------------------------------------------------------
int startSecurity(uint16_t connHandle) {
  const int rc = ble_gap_security_initiate(connHandle);
  BR_LOGI(kTagSec,"security initiate handle=%u rc=%d",connHandle,rc);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    BR_LOGW(kTagSec, "ble_gap_security_initiate rc=%d", rc);
    return rc;
  }
  return 0;
}

bool storedPeerKey(const ble_gap_conn_desc &desc) {
  ble_store_key_sec key{};key.peer_addr=desc.peer_id_addr;
  ble_store_value_sec value{};
  return ble_store_read_peer_sec(&key,&value)==0 && value.ltk_present;
}

bool waitForSecurity(uint16_t connHandle, uint32_t timeoutMs) {
  const uint32_t deadline = nowMs() + timeoutMs;
  while (nowMs() < deadline) {
    ble_gap_conn_desc desc;
    if (ble_gap_conn_find(connHandle, &desc) != 0) return false;
    if(s_slotBusy)return false;
    if (desc.sec_state.encrypted && (desc.sec_state.bonded || storedPeerKey(desc))) {
      BR_LOGI(kTagSec, "link encrypted (bonded=%d, authenticated=%d)", (int)desc.sec_state.bonded,
              (int)desc.sec_state.authenticated);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  ble_gap_conn_desc last{};ble_gap_conn_find(connHandle,&last);
  BR_LOGW(kTagSec, "security timeout %ums encrypted=%u bonded=%u stored=%u", (unsigned)timeoutMs,last.sec_state.encrypted,last.sec_state.bonded,storedPeerKey(last));
  return false;
}

// Subscribe to every notification source we care about and (re)apply the HID
// settings the remote expects. Called after every connect, so a reconnected
// remote is always re-armed.
bool discoverAndSubscribe() {
  s_slotError="";
  if (!s_client || !s_client->isConnected()) return false;

  const uint16_t connId = s_client->getConnId();

  // The remote's HID service is unusable until the link is encrypted, and the
  // bond is what makes the next reconnect instant. ble_gap_security_initiate()
  // is used instead of BLEClient::secureConnection(), which waits forever.
  ble_gap_conn_desc before{};
  if(ble_gap_conn_find(connId,&before))return false;
  BR_LOGI(kTagSec,"security begin encrypted=%u bonded=%u stored=%u",before.sec_state.encrypted,before.sec_state.bonded,storedPeerKey(before));
  if(!before.sec_state.encrypted && startSecurity(connId))return false;
  ble_gap_conn_desc identityInfo{};
  if(s_newPeer.length() && !ble_gap_conn_find(connId,&identityInfo)) {BLEAddress identity(identityInfo.peer_id_addr);s_newPeer=identity.toString();s_newPeerType=identity.getType();}
  if (!waitForSecurity(connId, 10000)) {s_slotError="蓝牙配对未完成，请让遥控器重新进入配对模式";return false;}

  // Ask for the fast connection parameters immediately. Everything below -
  // service discovery, characteristic discovery, CCCD writes - is ATT
  // traffic that runs at the current connection interval, so the earlier the
  // interval shrinks the earlier the whole sequence finishes. This used to
  // happen at the very end of the function, after all the slow work.
  //
  // supervision_timeout = 100 (1 s), not the library default 400 (4 s). This
  // is what makes reboot recovery fast: a hard reset cannot send BLE's
  // disconnect, so the remote only learns the host vanished when the
  // supervision timeout expires - at 4 s it sits silent for exactly that long
  // before it starts advertising again, which measured as the entire gap
  // between "C3 rebooted" and "link re-established". At 1 s the remote is
  // back on air within the second and the reconnection starts immediately.
  // 1 s tolerates losing 60+ consecutive connection events at a 15 ms
  // interval, so ordinary packet loss can never trip it.
  s_client->updateConnParams(12, 12, 0, 100);

  // The client owns these objects across disconnects. Reuse handles only for
  // the same saved peer; switching devices or failed subscription rediscover.
  static std::map<std::string, BLERemoteService *> *cached=nullptr;
  static String cachedPeer;
  const bool reuse=cached && cachedPeer==s_connectedAddr && !s_newPeer.length() && !s_refreshServices;
  std::map<std::string, BLERemoteService *> *services = reuse ? cached : s_client->getServices();
  cached=services;cachedPeer=s_connectedAddr;s_refreshServices=false;
  BR_LOGI(kTagGatt,"service cache %s",reuse?"reused":"refreshed");
  if (!services || services->empty()) {
    BR_LOGE(kTagGatt, "service discovery returned nothing");
    return false;
  }
  BR_LOGI(kTagGatt, "discovered %u service(s), heap before characteristics %u B",
          (unsigned)services->size(), (unsigned)ESP.getFreeHeap());

  int subscribed = 0;
  s_notifyHandleCount=0;

  // Final compatibility probe for the CMCC voice remote: its link drops with
  // BLE_ERR_CONN_TERM_MIC after several CCCD writes. Keep Xiaomi and other
  // remotes on the normal multi-report path; this probe subscribes only its
  // first HID input report so we can isolate the trigger without changing the
  // security or key-mapping paths.
  const bool cmccSingleReportProbe = s_connectedName.indexOf("CMCC_Voice_Remote") >= 0;
  if (cmccSingleReportProbe) BR_LOGW(kTagGatt, "compat probe: CMCC single HID report");

  // Two passes over the discovered services, and the order is not cosmetic:
  // the service map is a std::map keyed by the UUID *string*, which sorts
  // 0x180F (battery) BEFORE 0x1812 (HID). A single in-order pass spends ~2 s
  // on the battery read + subscribe before it ever reaches the HID report,
  // and on every reboot that exact delay pushes back the moment keys start
  // working. Pass 0 handles only the HID service (the key path); pass 1
  // sweeps everything else, battery included.
  for (int pass = 0; pass < 2; pass++) {
    for (auto &kv : *services) {
      BLERemoteService *svc = kv.second;
      if (!svc) continue;

      const String svcUuid = svc->getUUID().toString();

      // The pass filter MUST run before getCharacteristics(): that call is
      // lazy and performs the ATT discovery of the service's characteristics
      // on first touch. Filtering after it would still pay the discovery cost
      // for every service pass 0 skips - measured at ~3.2 s on this remote.
      const bool isHidService = svc->getUUID().equals(BLEUUID(RC003_HOGP_SVC_UUID));
      const bool isAtvvService = BRIDGE_ATVV_CTL_ENABLE &&
          svc->getUUID().equals(BLEUUID(RC003_ATVV_SVC_UUID));
      if ((pass == 0) != isHidService) continue;
      // Do NOT discover characteristics of Device Information, vendor OTA,
      // GAP, etc. The BLE wrapper retains a heap object and semaphores for
      // every discovered characteristic, although we never use these services.
      // Filtering must precede the lazy getCharacteristics() call.
      if (!isHidService && !isAtvvService) continue; // Battery is read asynchronously by UUID.

      BR_LOGI(kTagGatt, "service %s", svcUuid.c_str());
      auto *chars = svc->getCharacteristicsByHandle();
      if (!chars) continue;

      for (auto &kc : *chars) {
        BLERemoteCharacteristic *ch = kc.second;
        if (!ch) continue;

        auto next=chars->upper_bound(ch->getHandle());
        const uint16_t endHandle=next==chars->end()?0xffff:next->first-1;
        const String uuid = ch->getUUID().toString();
        BR_LOGD(kTagGatt, "  char %s n=%d i=%d w=%d", uuid.c_str(), ch->canNotify() ? 1 : 0,
                ch->canIndicate() ? 1 : 0, (ch->canWrite() || ch->canWriteNoResponse()) ? 1 : 0);

        // ---- HOGP report characteristic (0x2A4D) -------------------------
        if (isHidService && uuid.indexOf(RC003_HID_REPORT_UUID) >= 0) {
          if (cmccSingleReportProbe && s_notifyHandleCount > 0) {
            BR_LOGW(kTagGatt, "compat probe: skip HID report value=%u", (unsigned)ch->getHandle());
            continue;
          }
          if (ch->canNotify() || ch->canIndicate()) {
            if (s_notifyHandleCount<8 && subscribeNative(s_client->getConnId(),ch->getHandle(),endHandle,ch->canNotify())) {
              s_notifyHandles[s_notifyHandleCount++]={ch->getHandle(),false};
              s_charReport = ch;
              subscribed++;
              BR_LOGI(kTagGatt, "subscribed to HID report %s", uuid.c_str());
            } else {
              s_subscriptionFailures++;
              BR_LOGE(kTagGatt, "subscribe failed on HID report %s", uuid.c_str());
            }
          }
          continue;
        }

        // ---- HOGP protocol mode (0x2A4E): force Report Protocol ----------
        if (isHidService && uuid.indexOf(RC003_PROTOCOL_MODE_UUID) >= 0) {
          if (ch->canWrite() || ch->canWriteNoResponse()) {
            uint8_t reportMode = 0x01;
            if (ch->writeValue(&reportMode, 1, false)) {
              BR_LOGI(kTagGatt, "protocol mode set to Report (0x01)");
            }
          }
          continue;
        }

        // ---- HOGP control point (0x2A4C): exit suspend --------------------
        if (isHidService && uuid.indexOf(RC003_HID_CTRL_POINT_UUID) >= 0) {
          if (ch->canWrite() || ch->canWriteNoResponse()) {
            uint8_t exitSuspend = 0x01; // HOGP: 0x00 suspends, 0x01 exits suspend.
            ch->writeValue(&exitSuspend, 1, false);
            BR_LOGD(kTagGatt, "hid control point: exit suspend");
          }
          continue;
        }

        // ---- ATVV control channel: voice button only ---------------------
        if (BRIDGE_ATVV_CTL_ENABLE && isAtvvService && uuid.indexOf("ab5e0004") >= 0) {
          if (ch->canNotify() || ch->canIndicate()) {
            if (s_notifyHandleCount<8 && subscribeNative(s_client->getConnId(),ch->getHandle(),endHandle,ch->canNotify())) {
              s_notifyHandles[s_notifyHandleCount++]={ch->getHandle(),true};
              s_charAtvvCtl = ch;
              subscribed++;
              BR_LOGI(kTagGatt, "subscribed to ATVV control (voice button only, no audio)");
            } else {
              s_subscriptionFailures++;
              BR_LOGW(kTagGatt, "subscribe failed on ATVV control");
            }
          }
          continue;
        }



        // The ATVV audio characteristic (ab5e0003) is intentionally NOT
        // subscribed: this firmware never touches the microphone path.
      }
    }
  }

  if (subscribed == 0) {
    cached=nullptr;
    BR_LOGE(kTagGatt, "no notification source found - is this an RC003?");
    return false;
  }

  s_subscribed = true;
  s_notifyCount = 0;
  BR_LOGI(kTagGatt, "ready: %d subscription(s), notifications live, heap %u B", subscribed,
          (unsigned)ESP.getFreeHeap());
  return true;
}

bool ensureClient() {
  if (s_client) return true;
  s_client = BLEDevice::createClient();
  if (!s_client) return false;
  s_client->setClientCallbacks(&s_clientCallbacks);
  return true;
}

// `addrText` must be in the "aa:bb:cc:dd:ee:ff" form; never pass a text form
// built from getNative().
bool connectToAddress(const String &addrText, uint8_t addrType) {
  if (!ensureClient()) return false;
  if (addrText.length() != 17) {
    BR_LOGE(kTag, "\"%s\" is not a valid BLE address", addrText.c_str());
    return false;
  }

  if (s_client->isConnected()) {
    s_client->disconnect();
    vTaskDelay(pdMS_TO_TICKS(60));
  }

  if(!settings::hasRc003()) {s_newPeer=addrText;s_newPeerType=addrType;}
  s_connectAttempts++;

  // The timeout argument of BLEClient::connect() is ignored by this library
  // version (see BRIDGE_CONNECT_LIB_TIMEOUT_MS), so the attempt below runs on
  // the library's own 30 s clock. Say so up front: on the bench the resulting
  // silence was mistaken for a deadlock.
  BR_LOGI(kTag, "connecting to %s (type %u), may take up to %u s", addrText.c_str(), (unsigned)addrType,
          (unsigned)(BRIDGE_CONNECT_LIB_TIMEOUT_MS / 1000));
  bool ok = s_client->connect(BLEAddress(addrText, addrType), addrType);

  // Retry with the opposite address type only for a random address. A device
  // with a public address never switches type, and since every attempt costs the
  // library's full timeout, an unconditional retry would just double the wait
  // before the caller gets its answer.
  if (!ok && addrType == BLE_ADDR_RANDOM) {
    BR_LOGW(kTag, "random-address attempt failed, retrying as public");
    if (s_client->connect(BLEAddress(addrText, BLE_ADDR_PUBLIC), BLE_ADDR_PUBLIC)) {
      ok = true;
      addrType = BLE_ADDR_PUBLIC;
    }
  }

  if (!ok) {
    BR_LOGW(kTag, "connect to %s failed", addrText.c_str());
    return false;
  }

  s_connectSuccesses++;
  s_connectedAddr = addrText;
  s_lastRssi = s_client->getRssi();
  if (s_connectedName.length() == 0) {
    s_connectedName = settings::hasRc003() ? settings::rc003Name() : String("Xiaomi BT Remote");
  }


  return true;
}

void configureScan() {
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(BRIDGE_SCAN_INTERVAL_MS);
  scan->setWindow(BRIDGE_SCAN_WINDOW_MS);
  scan->setAdvertisedDeviceCallbacks(&s_scanCallbacks, /*wantDuplicates=*/false, /*shouldParse=*/false);

  if (settings::hasRc003()) {
    BR_LOGI(kTag, "scanning for bound remote %s (\"%s\")", settings::rc003Address().c_str(),
            settings::rc003Name().c_str());
  } else {
    BR_LOGI(kTag, "scanning for device selection (empty slot)");
  }
}

// Run one scan chunk. Blocks for up to `seconds`, returns early on a match.
void runScanChunk(uint32_t seconds) {
  BLEScan *scan = BLEDevice::getScan();
  scan->start(seconds, /*is_continue=*/false);
  // Everything is driven from the callback, so the accumulated result set is
  // only memory pressure; drop it every chunk so an all-day scan cannot grow.
  scan->clearResults();

  portENTER_CRITICAL(&s_pendMux);
  const bool found = s_pendValid;
  portEXIT_CRITICAL(&s_pendMux);

  if (found) {
    setState(St::CONNECTING, "scan matched");
  }
}

bool takePending(String &addrOut, uint8_t &typeOut, String &nameOut, int &rssiOut) {
  portENTER_CRITICAL(&s_pendMux);
  if (!s_pendValid) {
    portEXIT_CRITICAL(&s_pendMux);
    return false;
  }
  addrOut = String(s_pendAddr);
  typeOut = s_pendAddrType;
  nameOut = String(s_pendName);
  rssiOut = s_pendRssi;
  s_pendValid = false;
  portEXIT_CRITICAL(&s_pendMux);
  return true;
}

void clearPending() {
  portENTER_CRITICAL(&s_pendMux);
  s_pendValid = false;
  portEXIT_CRITICAL(&s_pendMux);
}

// ---------------------------------------------------------------------------
// Request handling
// ---------------------------------------------------------------------------
void handleRequests() {
  bool doForget = false;
  bool doScan = false;
  bool doReconnect = false;
  bool doConnect = false;
  char addr[kAddrStrLen] = {0};
  uint8_t type = BLE_ADDR_PUBLIC;
  char name[32] = {0};

  portENTER_CRITICAL(&s_reqMux);
  if (s_reqForget) {
    s_reqForget = false;
    doForget = true;
  }
  if (s_reqScanNow) {
    s_reqScanNow = false;
    doScan = true;
  }
  if (s_reqReconnect) {
    s_reqReconnect = false;
    doReconnect = true;
  }
  if (s_reqConnect) {
    s_reqConnect = false;
    doConnect = true;
    copyFixed(addr, sizeof(addr), s_reqAddr);
    type = s_reqAddrType;
    copyFixed(name, sizeof(name), s_reqName);
  }
  portEXIT_CRITICAL(&s_reqMux);

  if (doForget) {
    BR_LOGI(kTag, "forget requested: dropping bond and saved address");
    if (s_client && s_client->isConnected()) {
      s_client->disconnect();
      vTaskDelay(pdMS_TO_TICKS(80));
    }
    if (settings::hasRc003()) {
      ble_bonds::removePeer(BLEAddress(settings::rc003Address(), settings::rc003AddrType()));
    }
    settings::clearRc003();
    s_directAttempts = 0;
    s_connectedAddr = "";
    clearPending();
    setState(St::SCANNING, "forget");
  }

  if (doConnect) {
    BR_LOGI(kTag, "console requested connect to %s (type %u)", addr, (unsigned)type);
    if (s_client && s_client->isConnected()) {
      s_client->disconnect();
      vTaskDelay(pdMS_TO_TICKS(80));
    }
    portENTER_CRITICAL(&s_pendMux);
    copyFixed(s_pendAddr, sizeof(s_pendAddr), addr);
    s_pendAddrType = type;
    s_pendRssi = 0;
    copyFixed(s_pendName, sizeof(s_pendName), name);
    s_pendValid = true;
    portEXIT_CRITICAL(&s_pendMux);
    s_directAttempts = 0;
    setState(St::CONNECTING, "console connect");
  }

  if (doReconnect) {
    s_refreshServices=true;
    BR_LOGI(kTag, "console requested reconnect");
    if (s_client && s_client->isConnected()) {
      s_client->disconnect();
      vTaskDelay(pdMS_TO_TICKS(80));
    }
    clearPending();
    s_directAttempts = 0;
    setState(settings::hasRc003() ? St::DIRECT_CONNECT : St::SCANNING, "manual reconnect");
  }

  if (doScan) {
    BR_LOGI(kTag, "console requested a fresh scan");
    s_directAttempts = 0;
    clearPending();
    setState(St::SCANNING, "manual scan");
  }
}

// ---------------------------------------------------------------------------
// Central task
// ---------------------------------------------------------------------------
void taskLoop() {
  if(s_newPeer.length() && s_client && !s_client->isConnected()) {
    if(rc003_client::discardUnassigned(s_newPeer,s_newPeerType)) s_newPeer="";
    else {BR_LOGE(kTag,"failed to clean temporary bond %s",s_newPeer.c_str());vTaskDelay(pdMS_TO_TICKS(500));return;}
  }
  if (s_slotBusy) {
    if (!s_slotPaused) {
      BLEDevice::getScan()->stop();
      if (s_client && s_client->isConnected()) {
        s_client->disconnect();
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
      }
      clearPending();
      portENTER_CRITICAL(&s_reqMux);
      s_reqConnect=s_reqForget=s_reqReconnect=s_reqScanNow=false;
      portEXIT_CRITICAL(&s_reqMux);
      s_slotPaused = true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    return;
  }
  handleRequests();
  if (!settings::hasRc003() && !settings::pairingEnabled()) {
    setState(St::IDLE, "empty slot");
    vTaskDelay(pdMS_TO_TICKS(100));
    return;
  }

  switch (s_state) {
    case St::IDLE:
      setState(settings::hasRc003() ? St::DIRECT_CONNECT : St::SCANNING, "boot");
      break;

    case St::DIRECT_CONNECT: {
      if (!settings::hasRc003()) {
        setState(St::SCANNING, "no bound address");
        break;
      }
      // The remote frequently needs a moment after boot before it will answer
      // a page, so the two direct attempts are spaced out.
      if (s_directAttempts > 0 && (nowMs() - s_lastDirectAttemptMs) < 1500) {
        vTaskDelay(pdMS_TO_TICKS(100));
        break;
      }

      const String bound = settings::rc003Address();
      s_lastDirectAttemptMs = nowMs();
      setState(St::CONNECTING, "direct connect to bound address");

      if (connectToAddress(bound, settings::rc003AddrType())) {
        setState(St::DISCOVERING, "connected");
      } else {
        s_directAttempts++;
        if (s_directAttempts >= 2) {
          BR_LOGI(kTag, "direct connect unsuccessful, falling back to scanning");
          s_directAttempts = 0;
          setState(St::SCANNING, "direct connect gave up");
        } else {
          setState(St::DIRECT_CONNECT, "retry direct connect");
        }
      }
      break;
    }

    case St::SCANNING: {
      if (s_scanBurstStartMs == 0) {
        s_scanBurstStartMs = nowMs();
        s_scanStarts++;
        configureScan();
      }
      // Duty cycle the radio so the concurrently connected Windows link keeps
      // enough air time and the console stays readable.
      if ((nowMs() - s_scanBurstStartMs) > BRIDGE_SCAN_BURST_MS) {
        BLEScan *scan = BLEDevice::getScan();
        if (scan->isScanning()) scan->stop();
        s_scanBurstStartMs = 0;
        s_idleUntilMs = nowMs() + BRIDGE_SCAN_IDLE_MS;
        setState(St::BACKOFF, "scan burst finished");
        break;
      }

      runScanChunk(3);
      break;
    }

    case St::BACKOFF:
      if (nowMs() >= s_idleUntilMs) {
        s_scanBurstStartMs = 0;
        setState(St::SCANNING, "backoff over");
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      break;

    case St::CONNECTING: {
      String addr;
      String name;
      uint8_t type = BLE_ADDR_PUBLIC;
      int rssi = 0;
      if (!takePending(addr, type, name, rssi)) {
        setState(St::SCANNING, "nothing to connect to");
        break;
      }
      s_lastRssi = rssi;
      if (name.length() > 0) s_connectedName = name;
      if (connectToAddress(addr, type)) {
        setState(St::DISCOVERING, "connected");
      } else {
        setState(St::SCANNING, "connect failed");
      }
      break;
    }

    case St::DISCOVERING:
      if (discoverAndSubscribe()) {
        ble_gap_conn_desc peer{};
        if (ble_gap_conn_find(s_client->getConnId(),&peer) ||
            !peer.sec_state.encrypted || (!peer.sec_state.bonded && !storedPeerKey(peer))) {
          BR_LOGW(kTag,"pairing not complete; identity not saved");
          s_client->disconnect();setState(St::SCANNING,"pairing incomplete");break;
        }
        {
          BLEAddress identity(peer.peer_id_addr);
          bool duplicate=false;
          for(uint8_t slot=0;slot<3;slot++) {
            if(slot==settings::activeSlot()) continue;
            String address,name;uint8_t type;
            settings::slotInfo(slot,address,name,type);
            if(address==identity.toString() && type==identity.getType()) duplicate=true;
          }
          if(duplicate || !settings::setRc003(identity.toString(), identity.getType(), s_connectedName)) {
            s_slotError=duplicate ? "此遥控器已属于其他槽位" : "保存设备失败，请重试";
            s_client->disconnect();setState(St::SCANNING,"identity rejected");break;
          }
          s_connectedAddr=identity.toString();
        }
        settings::setPairingEnabled(false);
        s_newPeer="";
        s_slotError="";
        setState(St::READY, "subscribed");
        event_bus::post(BR_EV_RC_READY);
        requestBatteryRead();
      } else {
        event_bus::post(BR_EV_RC_BOND_FAIL);
        if(!s_slotError[0]) s_slotError="按键服务订阅失败，请重新进入配对模式后选择设备";
        BR_LOGE(kTag, "service discovery/subscription failed, disconnecting");
        if (s_client && s_client->isConnected()) s_client->disconnect();
        vTaskDelay(pdMS_TO_TICKS(200));
        setState(St::SCANNING, "discovery failed");
      }
      break;

    case St::READY: {
      if (!s_client || !s_client->isConnected()) {
        // The disconnect callback already reset the tracker and posted the
        // BR_EV_RC_LINK_DOWN event.
        setState(St::SCANNING, "link gone");
        break;
      }
      // Read on the central task: some remotes never send battery notifications.
      // No timer task or retained payload; unchanged values do not reach NVS.
      if(BRIDGE_BATTERY_PASSTHROUGH && nowMs()-s_lastBatteryReadMs>=60000) requestBatteryRead();
      // Keep the link parameters favourable for latency without hammering the
      // remote with requests. Timeout stays at 1 s (see discoverAndSubscribe):
      // the short supervision timeout is what makes the next hard reboot
      // recover quickly, so it must be re-asserted here too.
      static uint32_t s_lastParamUpdate = 0;
      if ((nowMs() - s_lastParamUpdate) > 30000) {
        s_lastParamUpdate = nowMs();
        s_client->updateConnParams(12, 12, 0, 100);
        s_lastRssi = s_client->getRssi();
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      break;
    }
  }
}

void taskEntry(void *arg) {
  (void)arg;
  BR_LOGI(kTag, "central task started");
  for (;;) {
    taskLoop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

namespace rc003_client {

void handleNotification(uint16_t conn,uint16_t handle,uint8_t *data,size_t length) {
  if(s_slotBusy || !s_client || s_client->getConnId()!=conn)return;
  for(uint8_t i=0;i<s_notifyHandleCount;i++) if(s_notifyHandles[i].handle==handle) {
    if(s_notifyHandles[i].atvv)onAtvvCtlNotify(nullptr,data,length,true);
    else onHogpNotify(nullptr,data,length,true);
    return;
  }
}

bool discardUnassigned(const String &address, uint8_t type) {
  if(address.length()!=17 || type>1) return false;
  for(uint8_t i=0;i<3;i++) {String saved,name;uint8_t t;settings::slotInfo(i,saved,name,t);if(saved.equalsIgnoreCase(address)&&t==type)return true;}
  BLEAddress peer(address,type);ble_addr_t native{};native.type=type;memcpy(native.val,peer.getNative(),6);
  ble_gap_conn_desc info{};if(!ble_gap_conn_find_by_addr(&native,&info))return false;
  const bool ok=ble_bonds::removePeer(peer)>0;
  BR_LOGI(kTag,"discard temporary peer %s type=%u result=%d",address.c_str(),type,ok);
  return ok;
}
bool slotBusy() { return s_slotBusy; }
const char *slotError() { return s_slotError; }
bool requestSlot(uint8_t slot, uint8_t action) {
  if (slot>2 || action>2 || s_slotBusy || !hid_server::slotSwitchSafe()) return false;
  s_slotTarget=slot; s_slotAction=action; s_slotError="";
  s_slotPaused=false; s_slotBusy=true;
  BLEScan *scan=BLEDevice::getScan();if(scan->isScanning())scan->stop();
  return true;
}
void serviceSlot() {
  if (!s_slotBusy || !s_slotPaused || event_bus::pending() || !hid_server::slotSwitchSafe()) return;
  bridge::releaseAllKeys();
  BLEDevice::stopAdvertising();
  const uint8_t old=settings::activeSlot();
  const String addr=settings::rc003Address();
  const uint8_t type=settings::rc003AddrType();
  bool ok=true;
  if (settings::hasRc003())
    ok=ble_bonds::archiveSlot(old,BLEAddress(addr,type));
  if (ok) ok=settings::selectSlot(s_slotTarget);
  if (ok && s_slotAction==2) {
    ok=ble_bonds::deleteSlot(s_slotTarget,
        settings::hasRc003() ? BLEAddress(settings::rc003Address(),settings::rc003AddrType()) : BLEAddress());
    if(ok) { settings::clearRc003(); settings::setPairingEnabled(false); }
  }
  if (ok && s_slotAction==1) {
    if (settings::hasRc003()) ok=false; // Delete explicitly before adding.
    else settings::setPairingEnabled(true);
  }
  if (ok && settings::hasRc003() && ble_bonds::hasSlotArchive(s_slotTarget, BLEAddress(settings::rc003Address(),settings::rc003AddrType())))
    ok=ble_bonds::restoreSlot(s_slotTarget,
        BLEAddress(settings::rc003Address(),settings::rc003AddrType()));
  if (!ok) {
    s_slotError="槽位操作失败，保留原配置";
    settings::selectSlot(old);
    if(addr.length()) ble_bonds::restoreSlot(old,BLEAddress(addr,type));
  }
  s_connectedAddr=""; s_connectedName="";
  s_remoteBatteryValid=false; s_scanBurstStartMs=0; s_directAttempts=0;
  rc003_tracker_reset(&s_tracker);
  setState(St::IDLE,"slot operation");
  s_slotPaused=false; s_slotBusy=false;
  hid_server::ensureAdvertising();
}

bool restoreActiveSlot() {
  if(settings::hasRc003() && ble_bonds::hasSlotArchive(settings::activeSlot(),
      BLEAddress(settings::rc003Address(),settings::rc003AddrType())))
    return ble_bonds::restoreSlot(settings::activeSlot(),
      BLEAddress(settings::rc003Address(),settings::rc003AddrType()));
  return true;
}

bool begin() {
  if (s_taskHandle) return true;

  rc003_tracker_reset(&s_tracker);
  memset(s_nearby, 0, sizeof(s_nearby));

  // Seed the battery cache with the last level the remote actually reported.
  // Without this, the first battery read after a reboot would post a
  // "changed" event for a value the host already knows, and until the remote
  // was re-discovered the status line would claim "unknown" despite NVS
  // holding a real number.
  const int persisted = settings::batteryLevel();
  if (persisted >= 0 && persisted <= 100) {
    s_remoteBattery = (uint8_t)persisted;
    s_remoteBatteryValid = false;
    BR_LOGI(kTag, "battery cache seeded from NVS: %d%%", persisted);
  }

  BLEDevice::getScan()->setAdvertisedDeviceCallbacks(&s_scanCallbacks, false, false);

  // 8192 bytes: this task does String formatting, std::map iteration and
  // native NimBLE calls, all of which are stack hungry. The C3 has a single
  // core, so the priority only decides preemption against the Arduino loop.
  const BaseType_t rc = xTaskCreateUniversal(taskEntry, "rc003", 8192, nullptr, 4, &s_taskHandle,
                                             ARDUINO_RUNNING_CORE);
  if (rc != pdPASS) {
    BR_LOGE(kTag, "failed to start central task");
    s_taskHandle = nullptr;
    return false;
  }

  if (settings::hasRc003()) {
    BR_LOGI(kTag, "bound remote: %s (\"%s\")", settings::rc003Address().c_str(), settings::rc003Name().c_str());
  } else {
    BR_LOGI(kTag, "no bound remote yet - will scan and pair with the first Xiaomi remote it sees");
  }
  return true;
}

void requestScanNow() {
  portENTER_CRITICAL(&s_reqMux);
  s_reqScanNow = true;
  portEXIT_CRITICAL(&s_reqMux);
}

void requestReconnect() {
  portENTER_CRITICAL(&s_reqMux);
  s_reqReconnect = true;
  portEXIT_CRITICAL(&s_reqMux);
}

void requestForget() {
  portENTER_CRITICAL(&s_reqMux);
  s_reqForget = true;
  portEXIT_CRITICAL(&s_reqMux);
}

bool requestConnect(const String &address, uint8_t addrType, const String &name) {
  if (address.length() != 17 || address.length() >= sizeof(s_reqAddr)) {
    BR_LOGE(kTag, "\"%s\" is not a valid BLE address", address.c_str());
    return false;
  }
  portENTER_CRITICAL(&s_reqMux);
  copyFixed(s_reqAddr, sizeof(s_reqAddr), address.c_str());
  s_reqAddrType = addrType;
  copyFixed(s_reqName, sizeof(s_reqName), name.c_str());
  s_reqConnect = true;
  portEXIT_CRITICAL(&s_reqMux);
  return true;
}

const char *stateName() {
  switch (s_state) {
    case St::IDLE:           return "IDLE";
    case St::DIRECT_CONNECT: return "DIRECT";
    case St::SCANNING:       return "SCANNING";
    case St::CONNECTING:     return "CONNECTING";
    case St::DISCOVERING:    return "DISCOVERING";
    case St::READY:          return "READY";
    case St::BACKOFF:        return "BACKOFF";
    default:                 return "?";
  }
}

bool connected() { return s_state == St::READY; }
bool notified() { return s_subscribed; }

String boundAddress() { return settings::hasRc003() ? settings::rc003Address() : String("(none)"); }
String connectedAddress() { return s_connectedAddr.length() ? s_connectedAddr : String("(none)"); }
String connectedName() { return s_connectedName.length() ? s_connectedName : String("(none)"); }
int lastRssi() { return s_lastRssi; }

int lastReportAgeMs() {
  if (s_lastReportMs == 0) return -1;
  return (int)(nowMs() - s_lastReportMs);
}

uint32_t notifyCount() { return s_notifyCount; }

int batteryLevel() { return s_remoteBatteryValid ? (int)s_remoteBattery : -1; }

bool isNearby(const String &address) {
  bool found = false;
  portENTER_CRITICAL(&s_nearbyMux);
  for (size_t i = 0; i < kNearbyMax; i++) {
    if (s_nearby[i].used && address.equalsIgnoreCase(s_nearby[i].addr)) {
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&s_nearbyMux);
  return found;
}

size_t nearbyCount() {
  size_t n = 0;
  portENTER_CRITICAL(&s_nearbyMux);
  for (size_t i = 0; i < kNearbyMax; i++) {
    if (s_nearby[i].used && nowMs()-s_nearby[i].lastSeenMs<30000) n++;
  }
  portEXIT_CRITICAL(&s_nearbyMux);
  return n;
}

bool nearbyAt(size_t index, String *address, uint8_t *addrType, String *name, int *rssi) {
  NearbyEntry entry{};bool ok=false;
  portENTER_CRITICAL(&s_nearbyMux);
  for(size_t i=0;i<kNearbyMax;i++) {
    if(!s_nearby[i].used || nowMs()-s_nearby[i].lastSeenMs>=30000) continue;
    size_t rank=0;
    for(size_t j=0;j<kNearbyMax;j++) {
      if(!s_nearby[j].used || nowMs()-s_nearby[j].lastSeenMs>=30000) continue;
      if(s_nearby[j].rssi>s_nearby[i].rssi || (s_nearby[j].rssi==s_nearby[i].rssi && j<i)) rank++;
    }
    if(rank==index) {entry=s_nearby[i];ok=true;break;}
  }
  portEXIT_CRITICAL(&s_nearbyMux);
  if(ok) {
    if(address)*address=String(entry.addr);
    if(addrType)*addrType=entry.addrType;
    if(name)*name=String(entry.name);
    if(rssi)*rssi=entry.rssi;
  }
  return ok;
}

uint32_t scanStarts() { return s_scanStarts; }
uint32_t connectAttempts() { return s_connectAttempts; }
uint32_t connectSuccesses() { return s_connectSuccesses; }
uint32_t subscriptionFailures() { return s_subscriptionFailures; }

}  // namespace rc003_client
