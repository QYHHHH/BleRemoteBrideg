/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * event_bus.cpp - global event queue instance
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "event_bus.h"

#include <Arduino.h>

#include "config.h"
#include "event_queue.h"

namespace {

SpscRing<bridge_event_t, BRIDGE_EVENT_QUEUE_SIZE> s_queue;
// There are THREE producer contexts: NimBLE callbacks, the central worker
// (READY/battery/failure), and CLI test injection. Serialize their small push
// operations; an SPSC ring alone is not safe when these tasks preempt each other.
portMUX_TYPE s_producerMux = portMUX_INITIALIZER_UNLOCKED;

}  // namespace

const char *bridge_event_name(uint8_t type) {
  switch (type) {
    case BR_EV_RC_KEY:        return "RC_KEY";
    case BR_EV_RC_LINK_UP:    return "RC_LINK_UP";
    case BR_EV_RC_READY:      return "RC_READY";
    case BR_EV_RC_LINK_DOWN:  return "RC_LINK_DOWN";
    case BR_EV_RC_BOND_FAIL:  return "RC_BOND_FAIL";
    case BR_EV_RC_BATTERY:    return "RC_BATTERY";
    case BR_EV_WIN_LINK_UP:   return "WIN_LINK_UP";
    case BR_EV_WIN_LINK_DOWN: return "WIN_LINK_DOWN";
    case BR_EV_WIN_AUTH_FAIL: return "WIN_AUTH_FAIL";
    case BR_EV_RC_AUTH_FAIL:  return "RC_AUTH_FAIL";
    case BR_EV_WIN_AUTH_OK:   return "WIN_AUTH_OK";
    case BR_EV_NONE:
    default:                  return "NONE";
  }
}

namespace event_bus {

bool post(uint8_t type, uint8_t code, bool pressed) {
  bridge_event_t ev;
  ev.type = type;
  ev.code = code;
  ev.pressed = pressed;
  ev.ts_us = micros();
  ev.ts_ms = millis();
  portENTER_CRITICAL(&s_producerMux);
  const bool queued = s_queue.push(ev);
  portEXIT_CRITICAL(&s_producerMux);
  return queued;
}

bool pop(bridge_event_t &out) {
  return s_queue.pop(out);
}

uint32_t dropped() { return s_queue.dropped(); }

size_t pending() { return s_queue.size(); }

}  // namespace event_bus
