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

// Forget everything. Used by the factory-reset console command.
int removeAll();

}  // namespace ble_bonds
