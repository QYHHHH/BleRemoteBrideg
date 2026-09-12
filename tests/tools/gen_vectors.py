#!/usr/bin/env python3
"""Generate firmware/MiRemoteBridge/selftest_vectors.h from the shared JSON.

The JSON file is the single source of truth for the expected behaviour of the
parser, the press/release tracker, the key map and the runtime mode overrides.
This script turns it into a plain C header so the on-device `selftest` console
command exercises exactly the same expectations that the host-side model check
validates.

Usage (from the repository root):
    python tests/tools/gen_vectors.py
"""

from __future__ import annotations

import json
import pathlib
import sys

KIND_NONE = 0
KIND_KEYBOARD = 1
KIND_CONSUMER = 2

KIND_BY_NAME = {
    "none": KIND_NONE,
    "keyboard": KIND_KEYBOARD,
    "consumer": KIND_CONSUMER,
}

ROOT = pathlib.Path(__file__).resolve().parents[2]
VECTORS_JSON = ROOT / "tests" / "vectors" / "key_vectors.json"
OUTPUT_HEADER = ROOT / "firmware" / "MiRemoteBridge" / "selftest_vectors.h"


def c_bytes(values: list[int]) -> str:
    if not values:
        return "{0}"
    return "{" + ", ".join(str(int(v) & 0xFF) for v in values) + "}"


def action_fields(entry: dict) -> str:
    kind = KIND_BY_NAME[entry.get("kind", "none")]
    modifier = int(entry.get("modifier", 0))
    keycode = int(entry.get("keycode", 0))
    consumer = int(entry.get("consumer", 0))
    return f"{{ {kind}, {modifier}, {keycode}, {consumer} }}"


def main() -> int:
    data = json.loads(VECTORS_JSON.read_text(encoding="utf-8"))

    out: list[str] = []
    out.append("/*")
    out.append(" * MiRemoteBridge - ESP32-C3 dual-role BLE bridge for Xiaomi RC003 remote")
    out.append(" *")
    out.append(" * selftest_vectors.h - AUTO-GENERATED, DO NOT EDIT BY HAND.")
    out.append(" *")
    out.append(" * Source : tests/vectors/key_vectors.json")
    out.append(" * Generator: tests/tools/gen_vectors.py")
    out.append(" *")
    out.append(" * SPDX-License-Identifier: GPL-3.0-or-later")
    out.append(" */")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdbool.h>")
    out.append("#include <stddef.h>")
    out.append("#include <stdint.h>")
    out.append("")
    out.append("typedef struct {")
    out.append("  uint8_t code;")
    out.append("  bool pressed;")
    out.append("} st_key_expect_t;")
    out.append("")
    out.append("typedef struct {")
    out.append("  uint8_t kind;      /* 0 none, 1 keyboard, 2 consumer */")
    out.append("  uint8_t modifier;")
    out.append("  uint8_t keycode;")
    out.append("  uint16_t consumer;")
    out.append("} st_action_expect_t;")
    out.append("")
    out.append("typedef struct {")
    out.append("  const char *name;")
    out.append("  const uint8_t *bytes;")
    out.append("  uint16_t len;")
    out.append("  uint16_t expect_count;")
    out.append("  st_key_expect_t expect[8];")
    out.append("} st_parse_vec_t;")
    out.append("")
    out.append("typedef struct {")
    out.append("  const char *name;")
    out.append("  const uint8_t *input_codes;")
    out.append("  const uint8_t *input_pressed;")
    out.append("  uint16_t input_count;")
    out.append("  uint16_t expect_count;")
    out.append("  st_key_expect_t expect[8];")
    out.append("} st_tracker_vec_t;")
    out.append("")
    out.append("typedef struct {")
    out.append("  uint8_t code;")
    out.append("  const char *name;")
    out.append("  st_action_expect_t expect;")
    out.append("} st_keymap_vec_t;")
    out.append("")
    out.append("/* mode axis: 0 = back, 1 = power, 2 = voice; mode_index indexes the")
    out.append(" * corresponding keymap_*_mode_t enum. */")
    out.append("typedef struct {")
    out.append("  uint8_t axis;")
    out.append("  uint8_t mode_index;")
    out.append("  uint8_t code;")
    out.append("  st_action_expect_t expect;")
    out.append("} st_mode_vec_t;")
    out.append("")

    # ---- parse vectors -------------------------------------------------
    parse_vectors = data.get("parse", [])
    for idx, vec in enumerate(parse_vectors):
        out.append(f"static const uint8_t kParseBytes_{idx}[] = {c_bytes(vec.get('bytes', []))};")
    out.append("")
    out.append("static const st_parse_vec_t kParseVectors[] = {")
    for idx, vec in enumerate(parse_vectors):
        expects = vec.get("expect", [])
        exp = ", ".join(f"{{ {int(e['code'])}, {'true' if e['pressed'] else 'false'} }}" for e in expects)
        # The C array is fixed at 8 entries, so it must be padded. The real
        # expectations come FIRST: the consumer walks expect[0..count-1].
        init = ("{ " + exp + ", { 0, false } }") if exp else "{ { 0, false } }"
        name = vec["name"].replace('"', '\\"')
        out.append(
            f'  {{ "{name}", kParseBytes_{idx}, {len(vec.get("bytes", []))}, {len(expects)}, {init} }},'
        )
    out.append("};")
    out.append(f"static const size_t kParseVectorCount = {len(parse_vectors)};")
    out.append("")

    # ---- atvv vectors --------------------------------------------------
    atvv_vectors = data.get("atvv", [])
    for idx, vec in enumerate(atvv_vectors):
        out.append(f"static const uint8_t kAtvvBytes_{idx}[] = {c_bytes(vec.get('bytes', []))};")
    out.append("")
    out.append("static const st_parse_vec_t kAtvvVectors[] = {")
    for idx, vec in enumerate(atvv_vectors):
        expects = vec.get("expect", [])
        exp = ", ".join(f"{{ {int(e['code'])}, {'true' if e['pressed'] else 'false'} }}" for e in expects)
        init = ("{ " + exp + ", { 0, false } }") if exp else "{ { 0, false } }"
        name = vec["name"].replace('"', '\\"')
        out.append(
            f'  {{ "{name}", kAtvvBytes_{idx}, {len(vec.get("bytes", []))}, {len(expects)}, {init} }},'
        )
    out.append("};")
    out.append(f"static const size_t kAtvvVectorCount = {len(atvv_vectors)};")
    out.append("")

    # ---- tracker vectors -----------------------------------------------
    tracker_vectors = data.get("tracker", [])
    for idx, vec in enumerate(tracker_vectors):
        codes = [int(i["code"]) for i in vec.get("input", [])]
        pressed = [1 if i["pressed"] else 0 for i in vec.get("input", [])]
        out.append(f"static const uint8_t kTrackerCodes_{idx}[] = {c_bytes(codes)};")
        out.append(f"static const uint8_t kTrackerPressed_{idx}[] = {c_bytes(pressed)};")
    out.append("")
    out.append("static const st_tracker_vec_t kTrackerVectors[] = {")
    for idx, vec in enumerate(tracker_vectors):
        expects = vec.get("expect", [])
        exp = ", ".join(f"{{ {int(e['code'])}, {'true' if e['pressed'] else 'false'} }}" for e in expects)
        init = ("{ " + exp + ", { 0, false } }") if exp else "{ { 0, false } }"
        name = vec["name"].replace('"', '\\"')
        out.append(
            f'  {{ "{name}", kTrackerCodes_{idx}, kTrackerPressed_{idx}, {len(vec.get("input", []))}, '
            f"{len(expects)}, {init} }},"
        )
    out.append("};")
    out.append(f"static const size_t kTrackerVectorCount = {len(tracker_vectors)};")
    out.append("")

    # ---- keymap vectors ------------------------------------------------
    keymap_vectors = data.get("keymap", [])
    out.append("static const st_keymap_vec_t kKeymapVectors[] = {")
    for vec in keymap_vectors:
        name = vec["name"].replace('"', '\\"')
        out.append(f'  {{ {int(vec["code"])}, "{name}", {action_fields(vec)} }},')
    out.append("};")
    out.append(f"static const size_t kKeymapVectorCount = {len(keymap_vectors)};")
    out.append("")

    # ---- mode override vectors -----------------------------------------
    mode_vectors = data.get("keymap_modes", [])
    out.append("static const st_mode_vec_t kModeVectors[] = {")
    for vec in mode_vectors:
        if "back" in vec:
            axis, mode_index = 0, {"consumer_back": 0, "kb_esc": 1, "kb_alt_left": 2, "kb_backspace": 3}[vec["back"]]
        elif "power" in vec:
            axis, mode_index = 1, {"kb_alt_f4": 0, "consumer_sleep": 1, "consumer_power": 2, "kb_esc": 3}[vec["power"]]
        elif "voice" in vec:
            axis, mode_index = 2, {"kb_ralt_comma": 0, "consumer_mute": 1, "kb_lctrl_lgui": 2, "disabled": 3}[vec["voice"]]
        else:
            raise SystemExit(f"mode vector without axis: {vec}")
        out.append(f'  {{ {axis}, {mode_index}, {int(vec["code"])}, {action_fields(vec)} }},')
    out.append("};")
    out.append(f"static const size_t kModeVectorCount = {len(mode_vectors)};")
    out.append("")

    # ---- plain data ----------------------------------------------------
    codes = [int(c) for c in data.get("all_key_codes", [])]
    out.append(f"static const uint8_t kAllKeyCodes[] = {c_bytes(codes)};")
    out.append(f"static const size_t kAllKeyCodeCount = {len(codes)};")
    out.append("")
    fuzz = data.get("fuzz", {})
    out.append(f"static const uint32_t kFuzzIterations = {int(fuzz.get('iterations', 0))};")
    out.append(f"static const uint32_t kFuzzSeed = {int(fuzz.get('seed', 1))}U;")
    out.append("")

    OUTPUT_HEADER.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"wrote {OUTPUT_HEADER.relative_to(ROOT)}")
    print(
        f"  parse={len(parse_vectors)} atvv={len(atvv_vectors)} tracker={len(tracker_vectors)} "
        f"keymap={len(keymap_vectors)} modes={len(mode_vectors)} codes={len(codes)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
