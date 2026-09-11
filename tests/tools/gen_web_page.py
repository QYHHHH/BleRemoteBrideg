# SPDX-License-Identifier: GPL-3.0-or-later
"""Split the Web UI page into assets and gzip them for the firmware.

Why this exists (docs/TESTING.md 4.13): the ESP32-C3 has to serve the config
page while BLE holds most of the heap. Measured on hardware, a response larger
than the largest free block does not arrive at all - the gzip payload was
8257 B while the largest contiguous block was only 7668 B, and the client got
0 bytes (no truncation, no error). Serving CSS and JS as separate resources
keeps every single response comfortably under that limit, and lets the browser
cache them.

Usage:
  python tests/tools/gen_web_page.py            # write web_page_gz.h
  python tests/tools/gen_web_page.py --check    # fail if the header is stale

The generated header is committed, the same way selftest_vectors.h is, so a
plain arduino-cli build works without running Python first.
"""
import argparse
import gzip
import io
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware/MiRemoteBridge/web_page.h"
OUTPUT = ROOT / "firmware/MiRemoteBridge/web_page_gz.h"

STYLE_RE = re.compile(r"<style>(.*?)</style>", re.DOTALL)
SCRIPT_RE = re.compile(r"<script>(.*?)</script>", re.DOTALL)


def firmware_html():
    source = SOURCE.read_text(encoding="utf-8")
    return source.split('R"rawliteral(', 1)[1].split(')rawliteral";', 1)[0]


def compress(text: str) -> bytes:
    # mtime=0 keeps the output byte-identical across runs, so the generated
    # header does not churn on every build.
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as gz:
        gz.write(text.encode("utf-8"))
    return buf.getvalue()


def split(html: str):
    """Return (shell html, css, js) with the assets pulled out and linked."""
    style = STYLE_RE.search(html)
    script = SCRIPT_RE.search(html)
    if not style or not script:
        raise SystemExit("web_page.h must contain exactly one <style> and one <script>")
    css = style.group(1).strip()
    js = script.group(1).strip()
    shell = html[:style.start()] + '<link rel="stylesheet" href="/app.css">' + html[style.end():]
    # Re-find the script: the style replacement shifted the offsets.
    script = SCRIPT_RE.search(shell)
    shell = shell[:script.start()] + '<script src="/app.js"></script>' + shell[script.end():]
    return shell, css, js


def blob(name: str, data: bytes) -> str:
    lines = []
    for i in range(0, len(data), 16):
        lines.append("  " + ",".join("0x%02x" % b for b in data[i:i + 16]) + ",")
    return (f"static const size_t {name}Len = {len(data)};\n\n"
            f"static const char {name}[] PROGMEM = {{\n" + "\n".join(lines) + "\n};\n")


def render(parts) -> str:
    html, css, js = parts
    body = []
    for name, text in (("kIndexHtmlGz", html), ("kIndexCssGz", css), ("kIndexJsGz", js)):
        raw = text.encode("utf-8")
        packed = compress(text)
        body.append(f"// {name}: {len(raw)} -> {len(packed)} bytes")
        body.append(blob(name, packed))
    return f"""/*
 * MiRemoteBridge - gzip-compressed Web UI assets. GENERATED FILE, DO NOT EDIT.
 *
 * Regenerate with: python tests/tools/gen_web_page.py
 * Source: firmware/MiRemoteBridge/web_page.h
 *
 * Each asset is served on its own endpoint (/ , /app.css , /app.js) and always
 * gzip-encoded: every browser accepts gzip, and splitting keeps each response
 * under the largest free block the ESP32-C3 can manage while BLE is up.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <pgmspace.h>

""" + "\n".join(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="verify the committed header matches the source")
    args = parser.parse_args()

    parts = split(firmware_html())
    generated = render(parts)
    sizes = ", ".join(f"{len(t.encode())}->{len(compress(t))}" for t in parts)

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != generated:
            raise SystemExit("web_page_gz.h is stale - run gen_web_page.py")
        print(f"web_page_gz.h up to date (html, css, js: {sizes})")
        return

    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"{OUTPUT.name}: html, css, js = {sizes} bytes (raw -> gzip)")


if __name__ == "__main__":
    main()
