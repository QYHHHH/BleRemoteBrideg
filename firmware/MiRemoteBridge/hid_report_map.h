/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_report_map.h - HID report descriptor advertised to Windows
 *
 * TWO top-level application collections, each on its OWN report ID:
 *
 *   Report ID 1, 8 bytes   keyboard         (modifier, reserved, 6 key codes)
 *   Report ID 2, 2 bytes   consumer control (16-bit usage, little endian)
 *
 * and therefore TWO Report characteristics in the HID service, both carrying
 * UUID 0x2A4D and told apart by their Report Reference descriptor (0x2908).
 * That is how HOGP defines it and how every real Bluetooth keyboard does it.
 *
 * History - this layout was reached the hard way
 * ---------------------------------------------------------------------
 * The first attempt avoided the second characteristic by putting both
 * collections on ONE report ID, which is legal HID (report IDs only have to be
 * unique per report type) and worked around a limitation in the Arduino BLE
 * wrapper. Windows 11 refused it: the HID-over-GATT device failed to start with
 * CM_PROB_FAILED_START (Code 10) and problem status 0xC0110002, i.e.
 * HIDP_STATUS_INVALID_REPORT_TYPE. Two collections sharing one Report
 * characteristic is not something Windows accepts, legal or not.
 *
 * A diagnostic build with ONE collection started fine, which confirmed it. The
 * real fix is to give each collection its own report ID - and to accept that
 * this needs two characteristics with the same UUID, which BLEHIDDevice cannot
 * create. So the HID service is now built directly on NimBLE's ble_gatt_svc_def
 * in hid_gatt.cpp, where an array of characteristics has no uniqueness rule.
 * The full story is in docs/TESTING.md sections 4.5 and 4.6.
 *
 * No output (LED) report is declared: HOGP requires a Report characteristic of
 * the matching report TYPE for every report the descriptor mentions, and this
 * firmware never drives keyboard LEDs.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Report ID 1: keyboard
// ---------------------------------------------------------------------------
#define HID_REPORT_ID_KEYBOARD  1
#define HID_KB_REPORT_LEN       8
#define HID_KB_OFFSET_MODIFIER  0
#define HID_KB_OFFSET_RESERVED  1
#define HID_KB_OFFSET_KEYS      2  // 6 bytes, null padded
#define HID_KB_KEY_COUNT        6

// ---------------------------------------------------------------------------
// Report ID 2: consumer control
// ---------------------------------------------------------------------------
#define HID_REPORT_ID_CONSUMER  2
#define HID_CONSUMER_REPORT_LEN 2

// Report type byte used in the Report Reference descriptor (0x2908).
#define HID_REPORT_TYPE_INPUT   0x01

static const uint8_t kHidReportMap[] = {
    // =================================================================
    // Collection 1: keyboard, report ID 1
    // =================================================================
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID_KEYBOARD,  //   Report ID (1)

    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)          -> modifier byte

    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const)                 -> reserved byte

    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (0x65)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (0x65)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x00,        //   Input (Data,Array,Abs)        -> 6 key codes
    0xC0,              // End Collection

    // =================================================================
    // Collection 2: Consumer Control, report ID 2
    // =================================================================
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID_CONSUMER,  //   Report ID (2)

    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs)        -> 16-bit usage
    0xC0               // End Collection
};

#define HID_REPORT_MAP_LEN ((uint16_t)sizeof(kHidReportMap))

#ifdef __cplusplus
}
#endif
