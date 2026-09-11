/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * selftest.h - on-device verification of the pure logic and the dispatch path
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

namespace selftest {

// Runs the vector suite (parser, tracker, key map, runtime modes, event queue)
// plus a cross-check of the HID report descriptor constants.
// Returns the number of failures; results are printed on the console.
int runVectors();

// Drives real parse -> tracker -> event queue -> bridge -> HID dispatch code
// with synthetic reports and checks the resulting state. This exercises the
// same functions the live firmware uses, only without a peer device.
int runSimulation();

// Convenience wrapper used by the `selftest` console command.
int runAll();

}  // namespace selftest
