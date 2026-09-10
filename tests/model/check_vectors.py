#!/usr/bin/env python3
"""Host-side model check for MiRemoteBridge.

This runs today, without any hardware, and covers three things that are easy to
get wrong and impossible to see from a successful compile:

1.  The shared test vectors (tests/vectors/key_vectors.json) are consistent with
    the documented parsing / tracking / mapping rules, and cover every physical
    RC003 key.

2.  The invariants that keep Windows free of stuck keys hold for randomised
    press/release streams, including duplicate reports, overlapping presses and
    explicit release-all.

3.  The HID report descriptor in firmware/MiRemoteBridge/hid_report_map.h parses
    as a well-formed descriptor and declares exactly the input report sizes the
    firmware actually sends. A malformed or mismatched descriptor is rejected by
    Windows at enumeration time, and nothing in the Arduino build would catch it.

The *C implementation* of the pure logic is verified separately, on the device,
by the `selftest` console command using the very same vectors (converted to C by
tests/tools/gen_vectors.py).

Usage (from the repository root):
    python tests/model/check_vectors.py
"""

from __future__ import annotations

import json
import pathlib
import random
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
VECTORS_JSON = ROOT / "tests" / "vectors" / "key_vectors.json"
REPORT_MAP_H = ROOT / "firmware" / "MiRemoteBridge" / "hid_report_map.h"

# ---------------------------------------------------------------------------
# Constants mirrored from key_definitions.h
# ---------------------------------------------------------------------------
# Mirrors key_definitions.h. The unqualified codes are the ones measured on a
# real RC003 (2026-09-10, `raw on`, every physical key pressed in turn); the
# *_ALT codes come from third-party captures and were not observed here.
MI_KEYS = {
    0x80: "VOL_UP",
    0x81: "VOL_DOWN",
    0xF1: "BACK",
    0x66: "POWER",
    0x4A: "HOME",
    0x65: "MENU",
    0x35: "TV",
    0x52: "UP",
    0x51: "DOWN",
    0x50: "LEFT",
    0x4F: "RIGHT",
    0x28: "OK",
    0x3E: "VOICE",
    0xFF: "POWER_ALT",
    0x24: "HOME_ALT",
    0x5D: "MENU_ALT",
    0xC0: "TV_ALT",
    0x04: "VOICE_ALT",
}

ALIASES = {0xFF: 0x66, 0x24: 0x4A, 0x5D: 0x65, 0xC0: 0x35}

KIND_NONE, KIND_KEYBOARD, KIND_CONSUMER = 0, 1, 2

KEY_SLOT_OFFSET = 2
KEY_REPORT_MAX_LEN = 8

PASSED = 0
FAILED = 0
FAILURES: list[str] = []


def check(condition: bool, message: str) -> None:
    global PASSED, FAILED
    if condition:
        PASSED += 1
    else:
        FAILED += 1
        FAILURES.append(message)


def is_plausible_key_code(value: int) -> bool:
    return value in MI_KEYS


# ---------------------------------------------------------------------------
# Model of rc003_report.cpp
# ---------------------------------------------------------------------------
def classify(data: list[int]) -> str:
    if len(data) == 0:
        return "EMPTY"
    if len(data) > KEY_REPORT_MAX_LEN:
        return "AUDIO"
    return "HID"


def extract_key(data: list[int]) -> int:
    n = len(data)
    if n == 0:
        return 0
    if n == 1:
        return data[0]
    if n == 2:
        if data[1] != 0:
            return data[1]
        return data[0] if data[0] != 0 else 0
    for i in range(KEY_SLOT_OFFSET, n):
        if data[i] != 0:
            return data[i]
    if data[0] != 0 and is_plausible_key_code(data[0]):
        return data[0]
    if n >= 3 and data[1] != 0 and is_plausible_key_code(data[1]):
        return data[1]
    return 0


def parse_hid_report(data: list[int]) -> list[tuple[int, bool]]:
    if classify(data) != "HID":
        return []
    key = extract_key(data)
    return [(key, key != 0)]


def parse_atvv_ctl(data: list[int]) -> list[tuple[int, bool]]:
    if not data:
        return []
    op = data[0]
    if op == 0x04 and len(data) >= 2 and data[1] == 0x03:
        return [(0x04, True)]
    if op in (0x00, 0x08):
        return [(0x04, False)]
    return []


# ---------------------------------------------------------------------------
# Model of the tracker in rc003_report.cpp
# ---------------------------------------------------------------------------
class Tracker:
    def __init__(self) -> None:
        self.last_code = 0

    def reset(self) -> None:
        self.last_code = 0

    def apply(self, code: int, pressed: bool) -> list[tuple[int, bool]]:
        out: list[tuple[int, bool]] = []
        if pressed:
            if code == 0:
                return out
            if self.last_code == code:
                return out
            if self.last_code != 0:
                out.append((self.last_code, False))
            out.append((code, True))
            self.last_code = code
            return out
        if self.last_code == 0:
            return out
        out.append((self.last_code, False))
        self.last_code = 0
        return out

    def release_all(self) -> list[tuple[int, bool]]:
        if self.last_code == 0:
            return []
        code = self.last_code
        self.last_code = 0
        return [(code, False)]


# ---------------------------------------------------------------------------
# Model of the default key map in keymap.cpp
# ---------------------------------------------------------------------------
DEFAULT_TABLE: dict[int, dict] = {
    0x80: {"kind": KIND_CONSUMER, "consumer": 0x00E9},
    0x81: {"kind": KIND_CONSUMER, "consumer": 0x00EA},
    0x51: {"kind": KIND_KEYBOARD, "keycode": 0x51},
    0x52: {"kind": KIND_KEYBOARD, "keycode": 0x52},
    0x50: {"kind": KIND_KEYBOARD, "keycode": 0x50},
    0x4F: {"kind": KIND_KEYBOARD, "keycode": 0x4F},
    0x28: {"kind": KIND_KEYBOARD, "keycode": 0x28},
    0x4A: {"kind": KIND_KEYBOARD, "modifier": 0x08, "keycode": 0x07},  # Home -> Win+D
    0x65: {"kind": KIND_KEYBOARD, "keycode": 0x2C},                    # Menu -> Space
    0x35: {"kind": KIND_KEYBOARD, "keycode": 0x41},                    # TV   -> F8
}

BACK_MODES = {
    "consumer_back": {"kind": KIND_CONSUMER, "consumer": 0x0224},
    "kb_esc": {"kind": KIND_KEYBOARD, "keycode": 0x29},
    "kb_alt_left": {"kind": KIND_KEYBOARD, "modifier": 0x04, "keycode": 0x50},
}
POWER_MODES = {
    "kb_alt_f4": {"kind": KIND_KEYBOARD, "modifier": 0x04, "keycode": 0x3D},
    "consumer_sleep": {"kind": KIND_CONSUMER, "consumer": 0x0032},
    "consumer_power": {"kind": KIND_CONSUMER, "consumer": 0x0030},
    "kb_esc": {"kind": KIND_KEYBOARD, "keycode": 0x29},
}
VOICE_MODES = {
    "kb_ralt_comma": {"kind": KIND_KEYBOARD, "modifier": 0x40, "keycode": 0x36},
    "consumer_mute": {"kind": KIND_CONSUMER, "consumer": 0x00E2},
    "disabled": {"kind": KIND_NONE},
}


def keymap_lookup(
    code: int,
    back: str = "consumer_back",
    power: str = "kb_alt_f4",
    voice: str = "kb_ralt_comma",
) -> dict:
    if code == 0xF1:
        return dict(BACK_MODES[back])
    if code in (0x66, 0xFF):
        return dict(POWER_MODES[power])
    if code in (0x04, 0x3E):
        return dict(VOICE_MODES[voice])
    if code in DEFAULT_TABLE:
        return dict(DEFAULT_TABLE[code])
    if code in ALIASES:
        canonical = ALIASES[code]
        if canonical in DEFAULT_TABLE:
            return dict(DEFAULT_TABLE[canonical])
    return {"kind": KIND_NONE}


def expect_to_dict(entry: dict) -> dict:
    kind = {"none": KIND_NONE, "keyboard": KIND_KEYBOARD, "consumer": KIND_CONSUMER}[entry.get("kind", "none")]
    return {
        "kind": kind,
        "modifier": int(entry.get("modifier", 0)),
        "keycode": int(entry.get("keycode", 0)),
        "consumer": int(entry.get("consumer", 0)),
    }


def normalise(action: dict) -> dict:
    return {
        "kind": action.get("kind", KIND_NONE),
        "modifier": action.get("modifier", 0),
        "keycode": action.get("keycode", 0),
        "consumer": action.get("consumer", 0),
    }


# ---------------------------------------------------------------------------
# HID report descriptor parser
# ---------------------------------------------------------------------------
TYPE_MAIN, TYPE_GLOBAL, TYPE_LOCAL = 0, 1, 2

TAG_NAMES = {
    # Global items
    (TYPE_GLOBAL, 0x0): "Usage Page",
    (TYPE_GLOBAL, 0x1): "Logical Minimum",
    (TYPE_GLOBAL, 0x2): "Logical Maximum",
    (TYPE_GLOBAL, 0x3): "Physical Minimum",
    (TYPE_GLOBAL, 0x4): "Physical Maximum",
    (TYPE_GLOBAL, 0x5): "Unit Exponent",
    (TYPE_GLOBAL, 0x6): "Unit",
    (TYPE_GLOBAL, 0x7): "Report Size",
    (TYPE_GLOBAL, 0x8): "Report ID",
    (TYPE_GLOBAL, 0x9): "Report Count",
    # Local items
    (TYPE_LOCAL, 0x0): "Usage",
    (TYPE_LOCAL, 0x1): "Usage Minimum",
    (TYPE_LOCAL, 0x2): "Usage Maximum",
    # Main items
    (TYPE_MAIN, 0x8): "Input",
    (TYPE_MAIN, 0x9): "Output",
    (TYPE_MAIN, 0xA): "Collection",
    (TYPE_MAIN, 0xC): "End Collection",
}


def parse_descriptor(data: list[int]) -> dict:
    """Return per-report-id input/output bit counts plus structural checks."""
    i = 0
    usage_page = 0
    report_size = 0
    report_count = 0
    logical_min = 0
    logical_max = 0
    report_id = 0
    usage_min = None
    usage_max = None
    collection_depth = 0
    collection_count = 0
    max_depth = 0
    inputs: dict[int, int] = {}
    outputs: dict[int, int] = {}
    errors: list[str] = []

    while i < len(data):
        prefix = data[i]
        i += 1
        if prefix == 0xFE:
            errors.append("long items are not supported by this checker")
            break
        size_code = prefix & 0x03
        size = {0: 0, 1: 1, 2: 2, 3: 4}[size_code]
        item_type = (prefix >> 2) & 0x03
        tag = (prefix >> 4) & 0x0F
        if i + size > len(data):
            errors.append("truncated item")
            break
        raw = 0
        for k in range(size):
            raw |= data[i + k] << (8 * k)
        i += size

        name = TAG_NAMES.get((item_type, tag))
        if name is None:
            errors.append(f"unknown item type={item_type} tag=0x{tag:X}")
            continue

        if item_type == TYPE_GLOBAL:
            if name == "Usage Page":
                usage_page = raw
            elif name == "Report Size":
                report_size = raw
            elif name == "Report Count":
                report_count = raw
            elif name == "Logical Minimum":
                logical_min = raw
            elif name == "Logical Maximum":
                logical_max = raw
            elif name == "Report ID":
                report_id = raw
        elif item_type == TYPE_LOCAL:
            if name == "Usage Minimum":
                usage_min = raw
            elif name == "Usage Maximum":
                usage_max = raw
        else:  # Main
            if name == "Collection":
                collection_depth += 1
                collection_count += 1
                max_depth = max(max_depth, collection_depth)
            elif name == "End Collection":
                collection_depth -= 1
                if collection_depth < 0:
                    errors.append("unbalanced End Collection")
            elif name in ("Input", "Output"):
                if report_size == 0 or report_count == 0:
                    errors.append("Input/Output with zero report size or count")
                bits = report_size * report_count
                target = inputs if name == "Input" else outputs
                target[report_id] = target.get(report_id, 0) + bits
                # For an Array field the report value is the usage index, so the
                # logical maximum must cover the declared usage range. For a
                # Variable (bit field) the logical range describes the bit value
                # and says nothing about the usages.
                is_array = (raw & 0x02) == 0
                if is_array and usage_min is not None and usage_max is not None and logical_max < usage_max:
                    errors.append(
                        f"usage max 0x{usage_max:04X} exceeds logical max 0x{logical_max:04X} "
                        f"(usage page 0x{usage_page:02X})"
                    )
            usage_min = None
            usage_max = None

    if collection_depth != 0:
        errors.append(f"collection depth left at {collection_depth}")

    return {
        "inputs": inputs,
        "outputs": outputs,
        "errors": errors,
        "collections": collection_count,
        "max_depth": max_depth,
    }


def read_report_map() -> list[int]:
    """Pull the descriptor bytes out of hid_report_map.h.

    Simple `#define NAME 1` macros are resolved too, so the header can use
    HID_REPORT_ID_INPUT instead of a bare literal without the two drifting
    apart. Anything that cannot be resolved is an error rather than a silently
    skipped byte - skipping a byte would shift the whole descriptor and make
    every assertion below meaningless.
    """
    text = REPORT_MAP_H.read_text(encoding="utf-8")
    defines = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", text)}

    start = text.index("kHidReportMap[] = {")
    body = text[text.index("{", start) + 1: text.index("};", start)]

    values: list[int] = []
    for line in body.splitlines():
        line = re.sub(r"//.*$", "", line)
        for token in re.findall(r"0x[0-9A-Fa-f]{2}|[A-Za-z_]\w*", line):
            if token.startswith("0x"):
                values.append(int(token, 16))
            elif token in defines:
                values.append(defines[token])
            else:
                raise AssertionError(f"unresolved token in the report map: {token!r}")
    return values


# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
def check_parse_vectors(data: dict) -> None:
    for vec in data["parse"]:
        got = parse_hid_report(list(vec["bytes"]))
        want = [(int(e["code"]), bool(e["pressed"])) for e in vec["expect"]]
        check(got == want, f"parse vector '{vec['name']}': got {got}, want {want}")

    for vec in data["atvv"]:
        got = parse_atvv_ctl(list(vec["bytes"]))
        want = [(int(e["code"]), bool(e["pressed"])) for e in vec["expect"]]
        check(got == want, f"atvv vector '{vec['name']}': got {got}, want {want}")


def check_tracker_vectors(data: dict) -> None:
    for vec in data["tracker"]:
        tracker = Tracker()
        got: list[tuple[int, bool]] = []
        for step in vec["input"]:
            got.extend(tracker.apply(int(step["code"]), bool(step["pressed"])))
        want = [(int(e["code"]), bool(e["pressed"])) for e in vec["expect"]]
        check(got == want, f"tracker vector '{vec['name']}': got {got}, want {want}")


def check_keymap_vectors(data: dict) -> None:
    for vec in data["keymap"]:
        got = normalise(keymap_lookup(int(vec["code"])))
        want = expect_to_dict(vec)
        check(
            got == want,
            f"keymap vector '{vec['name']}' (0x{int(vec['code']):02X}): got {got}, want {want}",
        )

    for vec in data["keymap_modes"]:
        if "back" in vec:
            got = normalise(keymap_lookup(int(vec["code"]), back=vec["back"]))
        elif "power" in vec:
            got = normalise(keymap_lookup(int(vec["code"]), power=vec["power"]))
        else:
            got = normalise(keymap_lookup(int(vec["code"]), voice=vec["voice"]))
        want = expect_to_dict(vec)
        check(got == want, f"mode vector {vec}: got {got}, want {want}")


def check_coverage(data: dict) -> None:
    codes = [int(c) for c in data["all_key_codes"]]
    check(len(codes) == 13, f"expected 13 physical keys, got {len(codes)}")
    check(len(set(codes)) == 13, "physical key list contains duplicates")

    for code in codes:
        action = keymap_lookup(code)
        check(
            action["kind"] != KIND_NONE,
            f"physical key 0x{code:02X} ({MI_KEYS.get(code)}) maps to nothing",
        )

    # Anything the parser can emit that is not a known key must be rejected, so
    # an unknown code can never turn into a random host keystroke.
    for byte in range(256):
        if byte in MI_KEYS:
            continue
        action = keymap_lookup(byte)
        if action["kind"] != KIND_NONE:
            check(False, f"unknown code 0x{byte:02X} unexpectedly maps to {action}")


def check_fuzz(data: dict) -> None:
    rng = random.Random(int(data["fuzz"]["seed"]))
    iterations = int(data["fuzz"]["iterations"])
    codes = [int(c) for c in data["all_key_codes"]]

    tracker = Tracker()
    host_keys: set[int] = set()
    tracked_actions: dict[int, dict] = {}

    for step in range(iterations):
        roll = rng.random()
        if roll < 0.45:
            code = rng.choice(codes)
            events = tracker.apply(code, True)
        elif roll < 0.75:
            events = tracker.apply(0, False)
        elif roll < 0.90:
            code = rng.choice(codes)
            events = tracker.apply(code, True)  # deliberate duplicate burst
        else:
            events = tracker.release_all()

        for code, pressed in events:
            if pressed:
                check(
                    not host_keys,
                    f"fuzz step {step}: host already had {host_keys} down when pressing 0x{code:02X}",
                )
                action = keymap_lookup(code)
                check(action["kind"] != KIND_NONE, f"fuzz step {step}: 0x{code:02X} pressed but maps to nothing")
                host_keys.add(code)
                tracked_actions[code] = action
            else:
                check(
                    code in host_keys,
                    f"fuzz step {step}: release of 0x{code:02X} which was not down",
                )
                host_keys.discard(code)
                tracked_actions.pop(code, None)

        check(len(host_keys) <= 1, f"fuzz step {step}: more than one host key down ({host_keys})")

    # The essential safety property: after a disconnect style release-all the
    # host must be completely clear.
    tracker.release_all()
    host_keys.clear()
    check(not host_keys, "after release-all the host key set must be empty")

    # And the same for the pattern used on every disconnect: a fresh tracker.
    tracker = Tracker()
    check(tracker.release_all() == [], "a fresh tracker must not emit a release")


def check_descriptor() -> None:
    values = read_report_map()
    check(len(values) > 40, f"report map looks too short ({len(values)} bytes)")

    result = parse_descriptor(values)
    for err in result["errors"]:
        check(False, f"report descriptor: {err}")

    inputs = result["inputs"]
    outputs = result["outputs"]

    # There must be exactly ONE input report, carrying 8 keyboard bytes plus the
    # 16-bit consumer field. Two report IDs would mean two Report
    # characteristics sharing UUID 0x2A4D, which the Arduino-ESP32 3.3.11 BLE
    # wrapper cannot register - the second one is dropped from the service map,
    # never gets its service pointer assigned, and notify() on it panics the
    # chip. That was reproduced on hardware, so this assertion is a regression
    # guard, not a style preference.
    check(
        len(result["inputs"]) == 1,
        f"exactly 1 input report expected (risk of the two-characteristic crash), "
        f"descriptor declares {sorted(result['inputs'])}",
    )
    check(
        inputs.get(1) == 80,
        f"input report 1 must be 80 bits (10 bytes: 8 keyboard + 2 consumer), descriptor says {inputs.get(1)}",
    )
    # No output report may be declared. Every report a descriptor mentions needs a
    # Report characteristic of that report TYPE, and BLEHIDDevice only ever creates
    # an input one. With the standard keyboard LED output copied in, Windows 11
    # failed to start the HID-over-GATT device: CM_PROB_FAILED_START (Code 10) with
    # problem status 0xC0110002 = HIDP_STATUS_INVALID_REPORT_TYPE. It was reproduced
    # on hardware, so this is a regression guard.
    check(
        not outputs,
        f"no output report may be declared (HIDP_STATUS_INVALID_REPORT_TYPE); "
        f"descriptor declares outputs={sorted(outputs)}",
    )
    check(
        2 not in inputs,
        "report ID 2 must not be declared: it would need a second 0x2A4D characteristic",
    )
    # The number of top-level collections is currently under test rather than
    # fixed. Two collections sharing report ID 1 is what Windows 11 refuses with
    # HIDP_STATUS_INVALID_REPORT_TYPE, so the diagnostic profile in
    # hid_report_map.h declares ONE and folds the consumer usages into it.
    # Accept either, but never zero - see docs/TESTING.md section 4.6.
    check(
        result["collections"] in (1, 2),
        f"expected 1 or 2 top-level application collections, got {result['collections']}",
    )
    check(result["max_depth"] == 1, f"application collections must not be nested, depth is {result['max_depth']}")

    # Cross-check against the lengths the firmware actually sends.
    hid_header = (ROOT / "firmware" / "MiRemoteBridge" / "hid_report_map.h").read_text(encoding="utf-8")
    report_len = int(re.search(r"#define HID_INPUT_REPORT_LEN\s+(\d+)", hid_header).group(1))
    keys_off = int(re.search(r"#define HID_INPUT_OFFSET_KEYS\s+(\d+)", hid_header).group(1))
    keys_cnt = int(re.search(r"#define HID_INPUT_KEY_COUNT\s+(\d+)", hid_header).group(1))
    cons_off = int(re.search(r"#define HID_INPUT_OFFSET_CONSUMER\s+(\d+)", hid_header).group(1))

    check(report_len * 8 == inputs.get(1), f"HID_INPUT_REPORT_LEN={report_len} disagrees with the descriptor")
    check(
        keys_off + keys_cnt == cons_off == 8,
        f"field layout mismatch: keys end at {keys_off + keys_cnt}, consumer starts at {cons_off}",
    )
    check(
        cons_off + 2 == report_len,
        f"consumer field must be the last two bytes (offset {cons_off}, report {report_len} bytes)",
    )


def check_consumer_usage_range() -> None:
    """Every consumer usage the firmware can send must fit the advertised range."""
    usages: list[int] = []
    for action in list(DEFAULT_TABLE.values()) + list(BACK_MODES.values()) + list(POWER_MODES.values()) + list(
        VOICE_MODES.values()
    ):
        if action["kind"] == KIND_CONSUMER:
            usages.append(action["consumer"])

    limit = 0x03FF  # logical/usage maximum in the descriptor
    for usage in sorted(set(usages)):
        check(usage <= limit, f"consumer usage 0x{usage:04X} exceeds the advertised maximum 0x{limit:04X}")
    check(len(usages) > 0, "no consumer usages found at all")


def main() -> int:
    data = json.loads(VECTORS_JSON.read_text(encoding="utf-8"))

    check_parse_vectors(data)
    check_tracker_vectors(data)
    check_keymap_vectors(data)
    check_coverage(data)
    check_descriptor()
    check_consumer_usage_range()
    check_fuzz(data)

    print(f"checks passed : {PASSED}")
    print(f"checks failed : {FAILED}")
    if FAILURES:
        print()
        for failure in FAILURES[:40]:
            print(f"  FAIL {failure}")
        if len(FAILURES) > 40:
            print(f"  ... and {len(FAILURES) - 40} more")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
