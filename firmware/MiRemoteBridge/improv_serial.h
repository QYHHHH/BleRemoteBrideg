/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * improv_serial.h - Improv Wi-Fi provisioning over the USB serial console
 *
 * The config page lives on the local network, so the board has to be told which
 * network to join. Until now that meant a serial console command (`wifi join
 * <ssid> <pass>`). This module speaks Improv over the same USB port instead, so
 * a browser can do it with no companion app and no terminal: the public pages
 * that already implement the client side - ESPHome Web and anything built on
 * esp-web-tools - push the SSID and password and then get back the URL of the
 * config page, which they open for the user.
 *
 * Frame: 'I''M''P''R''O''V' | version | type | length | data... | checksum
 * The checksum is the sum of every preceding byte, low byte only.
 *
 * The console shares this port, so the parser is careful about ownership:
 * feedByte() claims the bytes that belong to a frame and hands back everything
 * else, including a partial "IMPROV" that turns out not to be a frame. A human
 * typing `status` while a provisioning client is idle therefore still works.
 *
 * Only the serial transport is implemented. The BLE dialect of the same
 * protocol would need its own GATT service, and the board's BLE roles are
 * already both in use (HID peripheral + RC003 central).
 *
 * This module deliberately does NOT track a "session" for the BOOT key. An
 * earlier version exposed sessionActive() so reset_button could stand down
 * while a client was connected, on the theory that a browser holding the port
 * open pulls GPIO9 low. It does not - Chromium applies DTR and RTS in a single
 * SetCommState, landing on (1,1), which leaves GPIO9 alone (measured
 * 2026-09-17; truth table in docs/WEB-UI.md). The guard could only ever fire
 * for a real press, so it was removed. Don't reintroduce it without a
 * measurement showing a client that actually drives the line.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace improv_serial {

// Called with every byte the parser decided was NOT part of an Improv frame, so
// the line-based console can have it. Pass the console's own byte handler.
using ConsoleSink = void (*)(char c);

// Announce the transport on the console. Call once from setup().
void begin();

// Feed one byte read from the shared console port. Returns true when the byte
// was claimed - either as part of a frame, or as the start of something that
// may still turn out to be one. Anything it returns false for is the console's.
bool feedByte(uint8_t byte, ConsoleSink sink);

// Advance the provisioning state machine and the asynchronous Wi-Fi scan.
// Call from loop(); it never blocks.
void loop();

}  // namespace improv_serial
