/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_bonds.cpp - bond store helpers
 *
 * SPDX-License-Identifier: MIT
 */

#include "ble_bonds.h"

#include <string.h>

#include <host/ble_store.h>
#include <nimble/ble.h>

#include "log.h"

namespace {

static const char *kTag = "BOND";

void toBleAddr(BLEAddress &in, ble_addr_t *out) {
  out->type = in.getType();
  memcpy(out->val, in.getNative(), BLE_DEV_ADDR_LEN);
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

int removeAll() {
  const int rc = ble_store_clear();
  BR_LOGI(kTag, "ble_store_clear() -> %d", rc);
  return rc == 0 ? 1 : 0;
}

}  // namespace ble_bonds
