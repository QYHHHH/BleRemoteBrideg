/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_report_map.h - HID report descriptor advertised to Windows
 *
 * Two reports are exposed, exactly like a real multimedia keyboard:
 *
 *   Report ID 1 - standard 6-key-rollover boot-compatible keyboard
 *                 8 bytes: [modifier, reserved, k0, k1, k2, k3, k4, k5]
 *   Report ID 2 - Consumer Control
 *                 2 bytes little endian: 16-bit consumer usage (0 = none)
 *
 * A 16-bit array report was chosen for Consumer Control on purpose: it can
 * express any usage in the page, so AC Back (0x0224) and friends work, whereas
 * the common "one bit per usage" layout would need a descriptor edit for every
 * new media key.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static const uint8_t kHidReportMap[] = {
    // -----------------------------------------------------------------
    // Keyboard, Report ID 1
    // -----------------------------------------------------------------
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0, Left Ctrl)
    0x29, 0xE7,        //   Usage Maximum (0xE7, Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)   -> modifier byte
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant)                   -> reserved byte
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (0x65)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x00,        //   Input (Data, Array, Absolute)      -> 6 key slots
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)  -> LED state
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)                  -> LED padding
    0xC0,              // End Collection

    // -----------------------------------------------------------------
    // Consumer Control, Report ID 2
    // -----------------------------------------------------------------
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0               // End Collection
};

#define HID_REPORT_MAP_LEN ((uint16_t)sizeof(kHidReportMap))

// Byte lengths of the reports we push, for compile-time assertions and tests.
#define HID_KEYBOARD_REPORT_LEN 8
#define HID_CONSUMER_REPORT_LEN 2

#ifdef __cplusplus
}
#endif
