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
 * No output (LED) report is declared. That is deliberate and is the difference
 * between a device Windows starts and one it does not - see below.
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
 * collections sharing an ID are simply concatenated. It also keeps us on the
 * public API instead of patching a core library that lives outside this
 * repository.
 *
 * Why there is no output report
 * ---------------------------------------------------------------------
 * A standard USB keyboard descriptor also declares a one-byte LED output
 * report, and this file used to copy that. It is wrong for HOGP: every report
 * the descriptor declares must have a matching Report characteristic carrying
 * a Report Reference descriptor of that report TYPE, and BLEHIDDevice only
 * ever creates an INPUT report characteristic here.
 *
 * Windows 11 makes that fatal rather than merely untidy. With the LED output
 * present, the HID-over-GATT device failed to start with CM_PROB_FAILED_START
 * (Code 10) and problem status 0xC0110002, i.e. HIDP_STATUS_INVALID_REPORT_TYPE
 * - the driver went looking for the output report it had been told about and
 * found only a characteristic marked "input". The other four GATT services
 * started fine, and the device was correctly paired the whole time, which is
 * what pointed at the descriptor rather than at the radio or the security
 * setup.
 *
 * Adding a real output report characteristic is not an option with this
 * library version: BLEHIDDevice::outputReport() creates a second 0x2A4D
 * characteristic and therefore hits the duplicate-UUID bug described above,
 * so the characteristic would silently never exist. Since this firmware never
 * drives keyboard LEDs, dropping the declaration is both the correct HALF of
 * the fix and the only workable one.
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
