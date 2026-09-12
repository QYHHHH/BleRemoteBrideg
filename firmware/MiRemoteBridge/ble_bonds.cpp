/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_bonds.cpp - bond store helpers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "ble_bonds.h"

#include <string.h>

#include <host/ble_store.h>
#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <nvs.h>
#include <nimble/ble.h>

#include "log.h"

namespace {

static const char *kTag = "BOND";

void toBleAddr(BLEAddress &in, ble_addr_t *out) {
  out->type = in.getType();
  memcpy(out->val, in.getNative(), BLE_DEV_ADDR_LEN);
}

// Stored outside NimBLE's three-record live store. Version/size guard the SDK ABI.
static constexpr uint32_t kArchiveMagic = 0x424F4E31;
struct BondArchive {
  uint32_t magic;
  uint16_t size;
  uint8_t ours, theirs, cccds;
  ble_addr_t address;
  ble_store_value_sec our, peer;
  ble_store_value_cccd cccd[MYNEWT_VAL(BLE_STORE_MAX_CCCDS)];
};

bool emptyAddress(const ble_addr_t &a) {
  const uint8_t zero[6]{};
  return memcmp(a.val, zero, sizeof(zero)) == 0;
}

bool sameAddress(const ble_addr_t &a, const ble_addr_t &b) {
  return a.type == b.type && memcmp(a.val, b.val, sizeof(a.val)) == 0;
}

bool archiveKey(uint8_t slot, char *key) {
  if (slot >= 3) return false;
  key[0] = 's'; key[1] = '0' + slot; key[2] = 0;
  return true;
}

bool loadArchive(uint8_t slot, BondArchive &a) {
  char key[3];
  if (!archiveKey(slot, key)) return false;
  nvs_handle_t nvs;
  if (nvs_open("rc_bonds", NVS_READONLY, &nvs) != ESP_OK) return false;
  size_t size = sizeof(a);
  const esp_err_t rc = nvs_get_blob(nvs, key, &a, &size);
  nvs_close(nvs);
  if (rc != ESP_OK || size != sizeof(a) || a.magic != kArchiveMagic ||
      a.size != sizeof(a) || a.ours > 1 || a.theirs > 1 ||
      a.cccds > MYNEWT_VAL(BLE_STORE_MAX_CCCDS) || (!a.ours && !a.theirs)) return false;
  if ((a.ours && !sameAddress(a.address, a.our.peer_addr)) ||
      (a.theirs && !sameAddress(a.address, a.peer.peer_addr))) return false;
  for (unsigned i = 0; i < a.cccds; ++i)
    if (!sameAddress(a.address, a.cccd[i].peer_addr)) return false;
  return true;
}

bool saveArchive(uint8_t slot, const BondArchive &a) {
  char key[3];
  if (!archiveKey(slot, key)) return false;
  nvs_handle_t nvs;
  if (nvs_open("rc_bonds", NVS_READWRITE, &nvs) != ESP_OK) return false;
  esp_err_t rc = nvs_set_blob(nvs, key, &a, sizeof(a));
  if (rc == ESP_OK) rc = nvs_commit(nvs);
  nvs_close(nvs);
  if (rc != ESP_OK) return false;
  BondArchive check{};
  return loadArchive(slot, check) && memcmp(&a, &check, sizeof(a)) == 0;
}

bool readLive(const ble_addr_t &address, BondArchive &a) {
  a = {};
  a.magic = kArchiveMagic; a.size = sizeof(a); a.address = address;
  ble_store_key_sec key{}; key.peer_addr = address;
  int rc = ble_store_read_our_sec(&key, &a.our);
  if (rc != 0 && rc != BLE_HS_ENOENT) return false;
  a.ours = rc == 0;
  rc = ble_store_read_peer_sec(&key, &a.peer);
  if (rc != 0 && rc != BLE_HS_ENOENT) return false;
  a.theirs = rc == 0;
  // This SDK stores one security record per identity and direction.
  key.idx = 1;
  ble_store_value_sec extra{};
  if (ble_store_read_our_sec(&key, &extra) != BLE_HS_ENOENT ||
      ble_store_read_peer_sec(&key, &extra) != BLE_HS_ENOENT) return false;
  ble_store_key_cccd ck{}; ck.peer_addr = address;
  for (unsigned i = 0; i <= MYNEWT_VAL(BLE_STORE_MAX_CCCDS); ++i) {
    ble_store_value_cccd v{}; ck.idx = i;
    rc = ble_store_read_cccd(&ck, &v);
    if (rc == BLE_HS_ENOENT) return true;
    if (rc || i == MYNEWT_VAL(BLE_STORE_MAX_CCCDS)) return false;
    a.cccd[a.cccds++] = v;
  }
  return false;
}

bool quietPeer(const ble_addr_t &address) {
  ble_gap_conn_desc desc{};
  return !ble_gap_adv_active() && !ble_gap_disc_active() &&
      !ble_gap_conn_active() && ble_gap_conn_find_by_addr(&address, &desc) == BLE_HS_ENOTCONN;
}

}  // namespace

namespace ble_bonds {

int count() {
  int n = 0;
  const int rc = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
  if (rc != 0) return -1;
  return n;
}

int list(BLEAddress *out, int max_out) {
  if (!out || max_out <= 0) return -1;

  ble_addr_t peers[16];
  int num = max_out < 16 ? max_out : 16;
  const int rc = ble_store_util_bonded_peers(peers, &num, 16);
  if (rc != 0) return -1;

  for (int i = 0; i < num; i++) {
    out[i] = BLEAddress(peers[i]);
  }
  return num;
}

int removePeer(BLEAddress peer) {
  ble_addr_t addr;
  toBleAddr(peer, &addr);
  const int rc = ble_store_util_delete_peer(&addr);
  BR_LOGI(kTag, "delete_peer %s (type %u) -> %d", peer.toString().c_str(), (unsigned)addr.type, rc);
  return rc == 0 ? 1 : 0;
}

int makeRoomForPeer(const BLEAddress &incoming, const BLEAddress &protected_addr) {
  BLEAddress peers[16];
  const int n = list(peers, 16);
  if (n < 0) return 0;

  const String incoming_text = incoming.toString();
  const String protected_text = protected_addr.toString();

  bool incoming_known = false;
  int victim = -1;
  for (int i = 0; i < n; i++) {
    const String addr = peers[i].toString();
    if (addr.equalsIgnoreCase(incoming_text)) incoming_known = true;
    // The store iterates in insertion order, so the first entry that is not
    // the protected remote is the oldest bond we are willing to give up.
    if (victim < 0 && !addr.equalsIgnoreCase(protected_text)) victim = i;
  }

  // A returning friend re-authenticates from its existing bond and writes
  // nothing new: no slot needed, nothing to delete.
  if (incoming_known) return 0;
  // Store not full yet - the incoming pairing will simply take a free slot.
  if (n < MYNEWT_VAL(BLE_STORE_MAX_BONDS)) return 0;
  // Only the protected remote is stored; refuse to touch it.
  if (victim < 0) {
    BR_LOGW(kTag, "store full but only the remote's bond exists - new peer %s cannot be paired",
            incoming_text.c_str());
    return 0;
  }

  BR_LOGI(kTag, "store full (%d/%u), freeing oldest non-remote bond %s for incoming %s", n,
          (unsigned)MYNEWT_VAL(BLE_STORE_MAX_BONDS), peers[victim].toString().c_str(),
          incoming_text.c_str());
  return removePeer(peers[victim]);
}

bool hasSlotArchive(uint8_t slot, BLEAddress peer) {
  BondArchive a{}; ble_addr_t address; toBleAddr(peer, &address);
  return loadArchive(slot, a) && sameAddress(address, a.address);
}

bool snapshotSlot(uint8_t slot, BLEAddress peer) {
  ble_addr_t address; toBleAddr(peer, &address);
  BondArchive a{};
  if (slot >= 3 || !readLive(address, a)) return false;
  // Never replace a good archive with an empty store (e.g. already archived).
  if (!a.ours && !a.theirs) return true;
  return saveArchive(slot, a);
}

bool archiveSlot(uint8_t slot, BLEAddress peer) {
  ble_addr_t address; toBleAddr(peer, &address);
  if (slot >= 3 || !quietPeer(address) || !snapshotSlot(slot, peer)) return false;
  // Unlike deleting store records directly, unpair also removes the controller IRK.
  const int rc = ble_gap_unpair(&address);
  return rc == 0 || rc == BLE_HS_ENOENT;
}

bool restoreSlot(uint8_t slot, BLEAddress peer) {
  ble_addr_t address; toBleAddr(peer, &address);
  BondArchive a{}, live{};
  if (!quietPeer(address) || !loadArchive(slot, a) || !sameAddress(address, a.address) ||
      !readLive(address, live)) return false;
  // A live record is newer than its snapshot. Never overwrite it with stale keys.
  if (live.ours || live.theirs)
    return live.ours >= a.ours && live.theirs >= a.theirs && live.cccds >= a.cccds;
  int ourCount = 0, peerCount = 0, cccdCount = 0;
  if (ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &ourCount) ||
      ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &peerCount) ||
      ble_store_util_count(BLE_STORE_OBJ_TYPE_CCCD, &cccdCount)) return false;
  // Preflight prevents NimBLE's overflow callback evicting a computer bond.
  if (ourCount + a.ours > MYNEWT_VAL(BLE_STORE_MAX_BONDS) ||
      peerCount + a.theirs > MYNEWT_VAL(BLE_STORE_MAX_BONDS) ||
      cccdCount + a.cccds > MYNEWT_VAL(BLE_STORE_MAX_CCCDS)) return false;
  int rc = a.ours ? ble_store_write_our_sec(&a.our) : 0;
  // This API also installs the peer IRK into the controller resolving list.
  if (!rc && a.theirs) rc = ble_store_write_peer_sec(&a.peer);
  for (unsigned i = 0; !rc && i < a.cccds; ++i) rc = ble_store_write_cccd(&a.cccd[i]);
  if (!rc) return true;
  // Only this previously absent peer may be partially written; retain its archive.
  const int rollback = ble_gap_unpair(&address);
  BR_LOGE(kTag, "slot restore failed (%d), rollback=%d; archive retained", rc, rollback);
  return false;
}

bool deleteSlot(uint8_t slot, BLEAddress peer) {
  char key[3]; ble_addr_t address; toBleAddr(peer, &address);
  if (!archiveKey(slot, key)) return false;
  // An empty slot has no peer: never pass the wildcard/zero identity to unpair.
  if (!emptyAddress(address)) {
    if (!quietPeer(address)) return false;
    const int unpair = ble_gap_unpair(&address);
    if (unpair != 0 && unpair != BLE_HS_ENOENT) return false;
  }
  nvs_handle_t nvs;
  esp_err_t rc = nvs_open("rc_bonds", NVS_READWRITE, &nvs);
  if (rc != ESP_OK) return false;
  rc = nvs_erase_key(nvs, key);
  if (rc == ESP_ERR_NVS_NOT_FOUND) rc = ESP_OK;
  if (rc == ESP_OK) rc = nvs_commit(nvs);
  nvs_close(nvs);
  return rc == ESP_OK;
}

bool clearSlotArchives() {
  nvs_handle_t nvs;
  esp_err_t rc = nvs_open("rc_bonds", NVS_READWRITE, &nvs);
  if (rc != ESP_OK) return false;
  rc = nvs_erase_all(nvs);
  if (rc == ESP_OK) rc = nvs_commit(nvs);
  nvs_close(nvs);
  return rc == ESP_OK;
}

int removeAll() {
  if (!clearSlotArchives()) {
    BR_LOGE(kTag, "failed to clear slot bond archives");
    return 0;
  }
  const int rc = ble_store_clear();
  BR_LOGI(kTag, "ble_store_clear() -> %d", rc);
  return rc == 0 ? 1 : 0;
}

}  // namespace ble_bonds
