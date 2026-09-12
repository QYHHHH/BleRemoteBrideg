# SPDX-License-Identifier: GPL-3.0-or-later
"""Print, for every remote hotspot, whether its states actually change pixels.

Three classes drive a hotspot's appearance: `.live` while the key is held,
`.sel` for the key being edited, `.pulse` for 260 ms on a real key press. Each
hotspot also carries a position class (`p1..p8`, `du/dd/dl/dr`, `do`) and
several of those set background, border, box-shadow or color themselves. Get the
cascade wrong and a key silently stops reacting: the four D-pad keys once lost
live and sel completely, five more lost half of them, and `.pulse` had no rule
at all. This tool prints the truth per key instead of trusting the stylesheet.

Usage:
  python tests/tools/spot_audit.py                 # state table for all 13 keys
  python tests/tools/spot_audit.py --raw 0x52      # + screenshots of that key
Screenshots land in outputs/ (gitignored).

The invariant is also asserted, per key, in the page regression
(check_web_ui.py) - this tool is for *diagnosing* a failure it reports.
"""
import argparse
import base64
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from cdp import Cdp, find_page, fresh_profile, launch_chrome  # noqa: E402
from web_ui_preview import demo_html  # noqa: E402

PROPS = ["backgroundColor", "backgroundImage", "borderTopColor", "boxShadow", "color"]

PROBE = """
(() => {
  const props = %s;
  const grab = el => { const s = getComputedStyle(el); return props.map(p => s[p]); };
  // .k carries transition:.15s, so reading computed style right after a class
  // change returns the pre-transition value and every state looks dead.
  const probe = el => {
    const t = el.style.transition; el.style.transition = 'none';
    el.classList.remove('live'); el.classList.remove('sel'); el.classList.remove('pulse');
    const base = grab(el);
    el.classList.add('live'); const live = grab(el); el.classList.remove('live');
    el.classList.add('sel');  const sel  = grab(el); el.classList.remove('sel');
    el.classList.add('pulse');const pulse= grab(el); el.classList.remove('pulse');
    el.style.transition = t;
    return {base, live, sel, pulse};
  };
  const spots = KEYS.map(k => Object.assign(
      {raw: k[0], name: k[1], cls: k[4]},
      probe(document.querySelector('#rmArt [data-r="' + k[0] + '"]'))));
  const cards = KEYS.map(k => Object.assign({raw: k[0]}, probe(document.getElementById('k' + k[0]))));
  return JSON.stringify({spots, cards});
})()
""" % json.dumps(PROPS)


def changes(base, other):
    return [PROPS[i] for i in range(len(PROPS)) if base[i] != other[i]]


def brief(prop_list):
    return ",".join(prop_list) or "-- NONE --"


def print_table(rows, with_cls):
    print("%-5s %-6s %-9s %-16s %-16s %-16s"
          % ("raw", "class" if with_cls else "", "name",
             "live vs base", "sel vs base", "pulse vs base"))
    bad = []
    for r in rows:
        lv = changes(r["base"], r["live"])
        se = changes(r["base"], r["sel"])
        pu = changes(r["base"], r["pulse"])
        print("0x%02X  %s %-9s %-16s %-16s %-16s"
              % (r["raw"], (".%-5s " % r["cls"]) if with_cls else "", r.get("name", ""),
                 brief(lv), brief(se), brief(pu)))
        if not lv or not se or not pu:
            bad.append(r)
    return bad


def shoot(cdp, out, label, expr):
    cdp.call("Runtime.evaluate", {"expression": expr, "awaitPromise": True})
    cdp.call("Runtime.evaluate", {"expression": "window.scrollTo(0,0)"})
    # Recomputed every time: anything appearing above the remote (the offline
    # banner, the demo strip) shifts the content and a cached rect crops wrong.
    r = json.loads(cdp.evaluate("""
(() => { const b = document.querySelector('.rm').getBoundingClientRect();
  return JSON.stringify({x:b.x,y:b.y,width:b.width,height:b.height}); })()
"""))
    shot = cdp.call("Page.captureScreenshot", {
        "format": "png",
        "clip": {"x": r["x"], "y": r["y"], "width": r["width"], "height": r["height"],
                 "scale": 2}})
    path = out / ("spot-%s.png" % label)
    path.write_bytes(base64.b64decode(shot["data"]))
    print("screenshot: %s" % path)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--raw", default="0x52",
                    help="key to screenshot, hex (default 0x52, the up key)")
    args = ap.parse_args()
    raw = int(args.raw, 16)

    out = ROOT / "outputs"
    out.mkdir(exist_ok=True)
    preview = out / "web-ui-preview.html"
    preview.write_text(demo_html(), encoding="utf-8")

    proc, port = launch_chrome(fresh_profile("audit"))
    try:
        ws = find_page(port)
        if not ws:
            raise SystemExit("Chrome never exposed a page target")
        cdp = Cdp(ws)
        cdp.call("Page.enable")
        cdp.call("Runtime.enable")
        cdp.call("Page.navigate", {"url": preview.as_uri()})
        for _ in range(60):
            if cdp.evaluate("typeof load") == "function":
                break
            time.sleep(0.25)

        data = json.loads(cdp.evaluate(PROBE))

        print("=== remote hotspots (#rmArt children) ===")
        bad = print_table(data["spots"], with_cls=True)
        print()
        print("=== key cards (#kNN) ===")
        print_table(data["cards"], with_cls=False)
        print()
        print("hotspots with a dead state: %s"
              % (", ".join("0x%02X(.%s)" % (r["raw"], r["cls"]) for r in bad) or "NONE"))

        for r in bad:
            print()
            print("0x%02X .%s computed:" % (r["raw"], r["cls"]))
            for state in ("base", "live", "sel", "pulse"):
                print("   %-5s %s" % (state, " | ".join(r[state][:4])))

        shoot(cdp, out, "default", "load().then(function(){S.sel=0;render()})")
        for state in ("sel", "live", "pulse"):
            shoot(cdp, out, "%02X-%s" % (raw, state), """
load().then(function(){
  KEYS.forEach(function(k){ const el = document.querySelector('#rmArt [data-r=\"' + k[0] + '\"]');
    el.classList.remove('live','sel','pulse'); });
  const el = document.querySelector('#rmArt [data-r=\"%d\"]');
  el.classList.add('%s');
})""" % (raw, state))

        return 1 if bad else 0
    finally:
        proc.terminate()


if __name__ == "__main__":
    sys.exit(main())
