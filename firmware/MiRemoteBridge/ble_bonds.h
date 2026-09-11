/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * ble_bonds.h - bond store helpers
 *
 * The stock Arduino BLE wrapper exposes no bond management at all (there is no
 * equivalent of NimBLEDevice::deleteBond()), so the few calls we need are made
 * straight against the NimBLE host API. Everything else in the firmware goes
 * through the wrapper.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <BLEAddress.h>

namespace ble_bonds {

// Number of bond records currently held by the NimBLE store.
int count();

// Dump the bonded peer identity addresses into `out`. Returns how many were
// written, or -1 on error.
int list(BLEAddress *out, int max_out);

// Forget the bond with one peer. Returns 1 when a record was removed, 0 when
// nothing matched, -1 on error.
int removePeer(BLEAddress peer);

// Bond housekeeping for a newly connected peer, run BEFORE that peer pairs.
//
// If `incoming` is already bonded it is a returning friend - a reconnect
// writes nothing new and no slot is freed. If the store is full and `incoming`
// is genuinely new, the oldest stored bond whose address differs from
// `protected_addr` is deleted to make room. Making room here matters: left
// alone, the stack's own overflow handler deletes the absolute oldest record
// with no idea what it is for, and if that happens to be the upstream
// remote's bond the bridge silently loses its keys until it is re-paired.
//
// Returns the number of records deleted.
int makeRoomForPeer(const BLEAddress &incoming, const BLEAddress &protected_addr);

// Forget everything. Used by the factory-reset console command.
int removeAll();

}  // namespace ble_bonds
