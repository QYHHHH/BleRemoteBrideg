/*
 * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote
 *
 * hid_report_map.h - HID report descriptor advertised to Windows
 *
 * ONE input report, ONE report ID, holding TWO top-level application
 * collections:
 *
 *   Report ID 1, 10 bytes total
 *     bytes 0..7  keyboard   [modifier, reserved, k0..k5]
 *     bytes 8..9  consumer   16-bit usage, little endian (0 = none)
 *
 * Why one report instead of the usual "report ID 1 = keyboard,
 * report ID 2 = media keys"
 * ---------------------------------------------------------------------
 * HOGP maps one report ID to one Report characteristic, so two report IDs
 * means two characteristics that both carry UUID 0x2A4D. Arduino-ESP32
 * 3.3.11's BLE wrapper cannot do that: BLEService::addCharacteristic()
 * deliberately refuses to insert a second characteristic whose UUID already
 * exists, so the second one is never handed to BLEService::start(), never
 * reaches the GATT table, and never even gets its m_pService member assigned
 * (the BLECharacteristic constructor leaves it uninitialised). Calling
 * notify() on it then dereferences that garbage pointer and the chip panics
 * with a load access fault - reproduced on real hardware before this change.
 * The library's own Server_Gamepad example only ever creates one input report,
 * so upstream never exercised the two-characteristic path.
 *
 * Sharing a single report ID across the two collections is legal HID: report
 * IDs are unique per report type, not per collection, and the bytes of all
 * collections sharing an ID are simply concatenated. Windows enumerates the
 * collections from the descriptor exactly as it does for any other composite
 * HID device. It also keeps us on the public API instead of patching a core
 * library that lives outside this repository.
 *
 * A 16-bit array field was chosen for Consumer Control on purpose: it can
 * express any usage in the page, so AC Back (0x0224) and friends work, whereas
 * a "one bit per usage" layout would need a descriptor edit for every new
 * media key.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The single input report shared by both collections.
#define HID_REPORT_ID_INPUT 1
#define HID_INPUT_REPORT_LEN 10

#define HID_INPUT_OFFSET_MODIFIER 0
#define HID_INPUT_OFFSET_RESERVED 1
#define HID_INPUT_OFFSET_KEYS 2  // 6 bytes, null padded
#define HID_INPUT_KEY_COUNT 6
#define HID_INPUT_OFFSET_CONSUMER 8  // 2 bytes, little endian

static const uint8_t kHidReportMap[] = {
    // =================================================================
    // Collection 1: keyboard
    // =================================================================
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID_INPUT,  //   Report ID (1)

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

    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x02,        //   Output (Data,Var,Abs)         -> LED state
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const)
    0xC0,              // End Collection

    // =================================================================
    // Collection 2: Consumer Control, sharing report ID 1
    // =================================================================
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, HID_REPORT_ID_INPUT,  //   Report ID (1)

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
