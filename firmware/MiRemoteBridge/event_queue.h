/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * event_queue.h - fixed-capacity lock-free SPSC ring buffer
 *
 * Requires one serialized producer and one consumer. event_bus.cpp serializes
 * NimBLE, central-worker and console pushes with a short critical section;
 * release/acquire index updates then keep the Arduino-loop consumer safe.
 * Do not use the raw ring concurrently from multiple producer tasks.
 *
 * The whole point of this file is requirement #5: a BLE callback must never
 * call into the GATT server directly. It parses the notification and pushes an
 * event; the loop does the HID work. That keeps the two roles from re-entering
 * each other inside the NimBLE host task.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

template <typename T, size_t N>
class SpscRing {
 public:
  static_assert(N >= 2, "capacity must be at least 2");

  // Producer side. Returns false when the queue is full (the event is dropped
  // and the caller is expected to report it).
  bool push(const T &value) {
    const uint32_t w = m_write.load(std::memory_order_relaxed);
    const uint32_t next = (w + 1u) % N;
    if (next == m_read.load(std::memory_order_acquire)) {
      m_dropped.fetch_add(1u, std::memory_order_relaxed);
      return false;
    }
    m_buf[w] = value;
    m_write.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side.
  bool pop(T &out) {
    const uint32_t r = m_read.load(std::memory_order_relaxed);
    if (r == m_write.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    out = m_buf[r];
    m_read.store((r + 1u) % N, std::memory_order_release);
    return true;
  }

  bool empty() const {
    return m_read.load(std::memory_order_acquire) == m_write.load(std::memory_order_acquire);
  }

  size_t size() const {
    const uint32_t w = m_write.load(std::memory_order_acquire);
    const uint32_t r = m_read.load(std::memory_order_acquire);
    return (w >= r) ? (size_t)(w - r) : (size_t)(N - r + w);
  }

  static constexpr size_t capacity() { return N - 1; }

  // Producer-side only.
  uint32_t dropped() const { return m_dropped.load(std::memory_order_relaxed); }
  void reset_dropped() { m_dropped.store(0, std::memory_order_relaxed); }

  // Consumer-side only, and only while the producer is quiet.
  void clear() {
    m_read.store(0, std::memory_order_release);
    m_write.store(0, std::memory_order_release);
  }

 private:
  T m_buf[N]{};
  std::atomic<uint32_t> m_write{0};
  std::atomic<uint32_t> m_read{0};
  std::atomic<uint32_t> m_dropped{0};
};
