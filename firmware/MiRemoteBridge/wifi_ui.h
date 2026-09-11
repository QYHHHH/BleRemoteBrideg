/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * wifi_ui.h - on-demand Wi-Fi AP + Web UI for programmable key bindings
 *
 * Wi-Fi and BLE share the same radio on the ESP32-C3, and coexistence costs
 * both heap (~50 KB for the Wi-Fi stack) and BLE airtime. Configuration is a
 * rare activity, so the AP is OFF by default and only runs after
 * `wifi on` (serial console). `wifi off` tears everything down and returns
 * the heap. Bindings live in NVS, so changes survive the AP going away.
 *
 * When enabled the board opens an open access point named "MiRemoteBridge"
 * and serves the configuration page at http://192.168.4.1/
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>

namespace wifi_ui {

// Register the AP/WebServer toggle with the console. Safe to call at boot.
void begin();

// Bring the AP + HTTP server up (or down). Returns false when the request
// could not be honoured (radio busy, already in the requested state, ...).
bool enable();
bool disable();
bool enabled();

// Feed the HTTP server. No-op while disabled; call from loop().
void loop();

}  // namespace wifi_ui
