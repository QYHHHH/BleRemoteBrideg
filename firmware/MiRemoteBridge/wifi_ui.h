/*
 * MiRemoteBridge - on-demand Web UI for programmable key bindings
 *
 * The page is served over the local network. There is no authentication: the
 * device does not join the Wi-Fi on its own, and a single short press of the
 * BOOT key is what opens a 30-minute window. The window starts the moment the
 * board gets an IP address from the router; once it closes the radio is
 * powered off and the page is unreachable. A new press restarts the window.
 *
 * Bluetooth stays on all the time - that is the product. Closing the radio
 * for the config UI just shrinks the window where the bridge is reachable
 * from the LAN: it is not a security primitive on its own, only a way to
 * keep the page off the network by default.
 *
 * Setup connection and HTTP transfers advance cooperatively in loop(), so
 * configuration never pauses Bluetooth or blocks HID key-release dispatch.
 *
 * An access point is still available as a one-shot fallback (`wifi ap on`)
 * for when the router is out of range or its credentials are not known.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

namespace wifi_ui {

// Register the console commands. Safe to call at boot.
void begin();

// `wifi on`: open the 30-minute window by joining the configured network.
// enable() starts association and returns immediately; use ready() / ip()
// after loop() has observed a valid address. Bluetooth stays fully enabled.
bool enable();

// `wifi off`: close the window immediately. The HTTP listener goes away and
// the radio powers off; BLE links remain. Safe to call any time.
bool disable();

// Re-arm the 30-minute window. If the radio is up this is a no-op (the page
// is already reachable and the window keeps running); if it is down this
// brings the station interface back up, the same as enable(). Returns false
// when no network is configured.
//
// Wired to a short press of the BOOT key - which is also why this lives in
// the Wi-Fi module: keeping the GPIO9 / DTR / short-press-vs-hold rules in
// one place means the button file does not need to know about Wi-Fi state.
bool triggerRejoin();

// Re-associate with the credentials currently stored, leaving the fallback
// access point first if it is the one running. Used by Improv serial after a
// client hands over new credentials: enable() does nothing once the UI is
// already up, and disable() used to refuse to stop it - so a re-provision
// needs this entry point. With the 30-minute window this no longer matters
// for the page itself, but Improv serial still calls it to swap creds.
bool rejoin();

// `wifi ap on`: start the fallback access point instead of joining a network.
// The AP does NOT run the 30-minute timer; it stays up until explicitly turned
// off. That is on purpose - it is meant for "router out of reach / unknown
// creds" cases where the user may need a long stretch to re-configure.
bool enableAp();

bool enabled();
bool ready();

// "off", "sta" or "ap".
const char *mode();

// Diagnostics for `wifi status`. Both return harmless values when off.
unsigned stationCount();
const char *ip();

// Seconds left in the current window, 0 when off or expired. Used by the
// page for its countdown, and by the heartbeat so the page can update
// without polling.
uint32_t windowRemainingSec();
bool windowActive();
// True when the radio is up but nothing is counting down - the fallback AP is
// the only case, and it stays up until `wifi ap off`. The page uses this to say
// "常开" instead of showing a countdown that would mean nothing.
bool windowUnlimited();

// Feed the HTTP server. No-op while disabled; call from loop().
void loop();

}  // namespace wifi_ui
