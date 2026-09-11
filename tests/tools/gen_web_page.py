# SPDX-License-Identifier: GPL-3.0-or-later
"""Pre-compress the Web UI page so the firmware ships a payload the AP can send.

Why this exists (docs/TESTING.md 4.13): the plain page is ~21 KB and the
ESP32-C3 cannot push that out in one write while BLE has the heap - measured
ceiling is ~13 KB. Gzip takes it to ~7 KB, which fits, and it is generated at
build time instead of hand-maintained.

Usage:
  python tests/tools/gen_web_page.py            # write web_page_gz.h
  python tests/tools/gen_web_page.py --check    # fail if the header is stale

The generated header is committed, the same way selftest_vectors.h is, so a
plain arduino-cli build works without running Python first.
"""
import argparse
import gzip
import io
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/MiRemoteBridge/web_page.h"
OUTPUT = ROOT / "firmware/MiRemoteBridge/web_page_gz.h"


def firmware_html():
    source = SOURCE.read_text(encoding="utf-8")
    return source.split('R"rawliteral(', 1)[1].split(')rawliteral";', 1)[0]


def compress(raw: bytes) -> bytes:
    # mtime=0 keeps the output byte-identical across runs, so the generated
    # header does not churn on every build.
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as gz:
        gz.write(raw)
    return buf.getvalue()


def render(html: bytes, blob: bytes) -> str:
    lines = []
    for i in range(0, len(blob), 16):
        chunk = ",".join("0x%02x" % b for b in blob[i:i + 16])
        lines.append("  " + chunk + ",")
    body = "\n".join(lines)
    return f"""/*
 * MiRemoteBridge - gzip-compressed Web UI page. GENERATED FILE, DO NOT EDIT.
 *
 * Regenerate with: python tests/tools/gen_web_page.py
 * Source: firmware/MiRemoteBridge/web_page.h ({len(html)} bytes -> {len(blob)} bytes gzipped)
 *
 * Served with Content-Encoding: gzip. The plain page is kept as a fallback for
 * clients that do not advertise gzip; browsers always do.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <pgmspace.h>

static const size_t kIndexHtmlGzLen = {len(blob)};

static const char kIndexHtmlGz[] PROGMEM = {{
{body}
}};
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed header matches the source")
    args = parser.parse_args()

    html = firmware_html().encode("utf-8")
    blob = compress(html)
    generated = render(html, blob)

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != generated:
            raise SystemExit("web_page_gz.h is stale - run gen_web_page.py")
        print(f"web_page_gz.h up to date ({len(html)} -> {len(blob)} bytes)")
        return

    OUTPUT.write_text(generated, encoding="utf-8")
    ratio = 100 * len(blob) / len(html)
    print(f"{OUTPUT.name}: {len(html)} -> {len(blob)} bytes ({ratio:.0f}%), "
          f"{len(generated)} bytes of C source")


if __name__ == "__main__":
    main()
