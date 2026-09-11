/*
 * MiRemoteBridge - on-demand Web UI for programmable key bindings
 *
 * The page is served over the local network, and by default the bridge JOINS
 * the existing Wi-Fi as a station (`wifi join <ssid> <pass>` once, then
 * `wifi on`). That is deliberate: in access-point mode the board also has to
 * run a DHCP server, and with BLE holding the heap the AP cost 36-55 KB while
 * its DHCP server only answered above ~13-20 KB free - so the page was
 * reachable only by luck (docs/TESTING.md 4.13). As a station the router does
 * DHCP and the client never has to change networks.
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
// enable() fails (with a console hint) when no credentials are stored yet.
bool enable();
bool disable();
bool enabled();

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
