/*
 * MiRemoteBridge - on-demand Web UI for programmable key bindings
 *
 * The page is served over the local network, and by default the bridge JOINS
 * the existing Wi-Fi as a station (`wifi join <ssid> <pass>` once, then
 * `wifi on`). The router provides DHCP and the client stays on the LAN.
 * Connection setup and HTTP transfers advance cooperatively in loop(), so
 * configuration never pauses Bluetooth or blocks HID key-release dispatch.
 *
 * An access point stays available as a fallback (`wifi ap on`) for when the
 * router is out of range or its credentials are not known.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>

namespace wifi_ui {

// Register the console commands. Safe to call at boot.
void begin();

// `wifi on` / `wifi off`: join the configured network and serve the page.
// enable() starts association and returns immediately. Use ready() / ip()
// after loop() has observed a valid address. Bluetooth stays fully enabled.
// ALWAYS-ON policy: with credentials stored the UI starts by itself shortly
// after boot and disable() refuses to stop it.
bool enable();
bool disable();
bool enabled();
bool ready();

// `wifi ap on`: start the fallback access point instead of joining a network.
bool enableAp();

// "off", "sta" or "ap".
const char *mode();

// Diagnostics for `wifi status`. Both return harmless values when off.
unsigned stationCount();
const char *ip();

// Feed the HTTP server. No-op while disabled; call from loop().
void loop();

}  // namespace wifi_ui
