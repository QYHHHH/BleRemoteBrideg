/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * log.h - small levelled logger with rate limiting
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <stdint.h>

#define BR_LOG_OFF   0
#define BR_LOG_ERROR 1
#define BR_LOG_WARN  2
#define BR_LOG_INFO  3
#define BR_LOG_DEBUG 4

namespace brlog {

void begin(uint32_t baud);
void setLevel(uint8_t level);
uint8_t level();

// `raw` reports are the per-notification dumps. They are gated separately from
// the normal level because they are the noisiest thing in the firmware.
void setRawEnabled(bool on);
bool rawEnabled();

// `lat` measurements, also separately gated.
void setLatencyEnabled(bool on);
bool latencyEnabled();

void printf(uint8_t level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

// Bypasses both the level filter and the rate limiter. Reserved for output that
// is useless if it is dropped: the outcome of the on-device self test, and the
// console replies that confirm a command. Everything else, including the
// per-report dumps, stays on the throttled path so that no component can stall
// the loop by flooding the UART.
void always(const char *tag, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace brlog

#define BR_LOGE(tag, ...) brlog::printf(BR_LOG_ERROR, tag, __VA_ARGS__)
#define BR_LOGW(tag, ...) brlog::printf(BR_LOG_WARN, tag, __VA_ARGS__)
#define BR_LOGI(tag, ...) brlog::printf(BR_LOG_INFO, tag, __VA_ARGS__)
#define BR_LOGD(tag, ...) brlog::printf(BR_LOG_DEBUG, tag, __VA_ARGS__)

// Raw notification dump (only when `raw on`).
#define BR_LOGRAW(tag, ...)                       \
  do {                                            \
    if (brlog::rawEnabled()) {                    \
      brlog::printf(BR_LOG_INFO, tag, __VA_ARGS__); \
    }                                             \
  } while (0)
