/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * log.cpp - levelled logger
 *
 * SPDX-License-Identifier: MIT
 */

#include "log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

uint8_t s_level = BR_LOG_INFO;
bool s_raw = false;
bool s_latency = true;

// Simple token bucket so a misbehaving peer cannot flood the console and stall
// the loop task inside a blocking UART write.
constexpr uint32_t kBucketCapacity = 40;
constexpr uint32_t kBucketRefillMs = 1000;

uint32_t s_tokens = kBucketCapacity;
uint32_t s_lastRefill = 0;

bool takeToken() {
  const uint32_t now = millis();
  const uint32_t elapsed = now - s_lastRefill;
  if (elapsed >= kBucketRefillMs) {
    const uint32_t periods = elapsed / kBucketRefillMs;
    s_lastRefill = now;
    s_tokens = kBucketCapacity;
    (void)periods;
  }
  if (s_tokens == 0) return false;
  s_tokens--;
  return true;
}

}  // namespace

namespace brlog {

void begin(uint32_t baud) {
  Serial.begin(baud);
  s_lastRefill = millis();
}

void setLevel(uint8_t level) {
  if (level <= BR_LOG_DEBUG) s_level = level;
}

uint8_t level() { return s_level; }

void setRawEnabled(bool on) { s_raw = on; }
bool rawEnabled() { return s_raw; }

void setLatencyEnabled(bool on) { s_latency = on; }
bool latencyEnabled() { return s_latency; }

void emit(const char *tag, const char *body) {
  // One single write so lines from different tasks do not interleave.
  char line[224];
  snprintf(line, sizeof(line), "[%8lu][%-7s] %s\n", (unsigned long)millis(), tag ? tag : "-", body);
  Serial.print(line);
}

void printf(uint8_t level, const char *tag, const char *fmt, ...) {
  if (level > s_level) return;
  if (!takeToken()) return;

  char body[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  emit(tag, body);
}

void always(const char *tag, const char *fmt, ...) {
  char body[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(body, sizeof(body), fmt, ap);
  va_end(ap);

  emit(tag, body);
}

}  // namespace brlog
