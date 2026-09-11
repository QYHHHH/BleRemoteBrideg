# SPDX-License-Identifier: GPL-3.0-or-later
"""Browser regression for the firmware page, without a board.

Extracts the page from web_page.h, injects a PREVIEW-ONLY fetch simulator
(never shipped in firmware) and runs assertions in a real Chromium.

Usage:
  python tests/tools/web_ui_preview.py                      # build the preview
  python tests/tools/check_web_ui.py --emit-js out.js       # export assertions
  <chromium driver> --json eval --stdin < out.js             # run them

This covers page logic only: not the ESP32 HTTP stack, NVS durability,
Wi-Fi/BLE coexistence headroom or real phone browsers (docs/TESTING.md 4.12).
"""
import argparse
import json
from pathlib import Path
from web_ui_preview import ROOT, demo_html

TESTS = r"""(async () => {
  const results=[];
  const assert=(condition,name)=>{if(!condition)throw Error(name);results.push(name);};
  await load();
  assert(S.on && S.ok,'initial API load');
  assert(document.querySelectorAll('.k').length===13,'13 key cards');
  assert(document.querySelectorAll('#rmArt [data-r]').length===13,'13 remote hotspots');
  assert(fmt(cur(0x3E))==='Ctrl + Win','runtime voice mode wins over static table');
  const voice=S.e[0x3E];S.e[0x3E]={raw:0x3E,kind:0,mod:0,key:0,cons:0};
  assert(fmt(cur(0x3E)).indexOf('\u4e0d\u8f6c\u53d1')>=0,'disabled mode does not fall back');
  S.e[0x3E]=voice;
  S.load=true;btns();openEd(0x28);
  assert(!$('ed').open && document.querySelector('#k40 button').disabled,'refresh blocks editing');
  S.load=false;btns();
  openEd(0x28);S.busy=true;btns();
  assert($('kind').disabled&&$('key').disabled&&$('cons').disabled&&document.querySelector('.m').disabled,'pending save locks draft');
  S.busy=false;btns();
  const reads=__demo.calls.length;await load();
  assert(__demo.calls.length===reads,'editing suspends refresh');$('ed').close();
  for (const k of KEYS) {
    const raw=k[0];
    openEd(raw);
    assert($('ed').open && S.cur===raw,'open editor '+hx(raw));
    $('kind').value='1';S.mods=3;$('key').value='4';draft();
    await save();
    assert(!$('ed').open,'save closes editor '+hx(raw));
    const call=__demo.calls.filter(c=>c.path==='/api/set').pop();
    assert(new URLSearchParams(call.query).get('raw')===hx(raw),'hex wire code '+hx(raw));
    assert(S.b[raw].mod===3 && S.b[raw].key===4,'saved correct key '+hx(raw));
  }
  openEd(0x28);
  assert(S.mods===3 && $('key').value==='4','keyboard and modifier readback');
  await save();
  assert(S.b[0x28].mod===3,'no-edit save preserves modifiers');
  openEd(0x80);$('kind').value='2';$('cons').value='205';draft();await save();
  openEd(0x80);
  assert($('cons').value==='205' && $('kind').value==='2','consumer decimal round trip');
  $('ed').close();
  openEd(0x3E);$('kind').value='1';S.mods=64;$('key').value='54';draft();await save();
  openEd(0x3E);assert(S.mods===64 && $('key').value==='54','right modifier + comma');
  S.mods=9;$('key').value='0';draft();await save();
  assert(S.b[0x3E].key===0 && S.b[0x3E].mod===9,'modifier-only chord');
  openEd(0x3E);S.mods=0;$('key').value='0';draft();await save();
  assert($('ed').open && !$('edE').hidden,'empty keyboard rejected');
  $('ed').close();
  openEd(0xF1);$('clr').click();
  assert(!!S.b[0xF1],'clear only changes draft before save');
  await save();assert(!S.b[0xF1] && cur(0xF1).cons===548,'clear reverts to base mapping');
  openEd(0x28);$('key').value='5';__demo.fault='write';await save();
  assert($('ed').open && !$('edE').hidden && S.b[0x28].key===4,'HTTP error keeps editor open');
  __demo.fault='mismatch';await save();
  assert($('ed').open && $('edE').textContent.indexOf('\u4e0d\u4e00\u81f4')>=0,'readback mismatch rejected');
  __demo.fault=null;$('ed').close();
  __demo.fault='offline';await load();
  assert(!S.on && !$('off').hidden,'offline shows stale-data notice');
  assert(document.querySelector('#k40 button').disabled,'offline writes disabled');
  __demo.fault=null;await load();assert(S.on,'reconnect recovery');
  __demo.fault='json';await load();assert(!S.on,'invalid JSON safely rejected');
  __demo.fault=null;await load();
  __demo.status.remoteName='<img src=x onerror=alert(1)> "test"';
  __demo.status.battery=-1;__demo.status.hostConnected=false;await load();
  assert(!$('h2').querySelector('img') && $('h1').textContent.indexOf('\u7b49\u5f85\u4e3b\u673a')>=0,'remote name is text, not HTML');
  assert($('pB').lastChild.textContent==='\u7535\u91cf\u672a\u77e5','unknown battery not fabricated');
  __demo.status.remoteConnected=false;await load();
  assert($('h1').textContent.indexOf('\u7b49\u5f85\u9065\u63a7\u5668')>=0,'remote disconnection state');
  __demo.status.remoteName='\u5c0f\u7c73\u84dd\u7259\u8bed\u97f3\u9065\u63a7\u5668';__demo.status.battery=97;
  __demo.status.hostConnected=true;__demo.status.remoteConnected=true;await load();
  $('resetAll').click();$('rdC').click();
  assert(Object.keys(S.b).length>0,'reset cancellation preserves map');
  $('resetAll').click();__demo.fault='write';await reset();
  assert($('rd').open && !$('rdE').hidden,'reset HTTP error visible');
  __demo.fault=null;await reset();
  assert(!$('rd').open && Object.keys(S.b).length===0,'reset confirmed and read back');
  assert(cur(0x3E).mod===9,'reset preserves serial runtime mode');
  assert(!document.querySelector('script[src],link[rel=stylesheet],img[src^=http]'),'no external assets');
  return {passed:results.length,checks:results};
})()"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", help="agent-browser executable (optional)")
    parser.add_argument("--emit-js", type=Path, help="write the assertions to a file")
    parser.add_argument("--check-size", action="store_true",
                        help="print the page size and fail if it grew past the budget")
    args = parser.parse_args()

    out = ROOT / "outputs"
    out.mkdir(exist_ok=True)
    if args.check_size:
        from web_ui_preview import firmware_html
        size = len(firmware_html().encode("utf-8"))
        print(f"page size: {size} bytes (budget 24000, proven-good 13087)")
        if size > 24000:
            raise SystemExit("page too large for the AP's heap headroom")
        if not args.emit_js and not args.browser:
            return
    if args.emit_js:
        args.emit_js.write_text(TESTS, encoding="utf-8")
        print(f"Browser assertions: {args.emit_js.resolve()}")
        return
    if not args.browser:
        parser.error("--browser or --emit-js is required")
    import subprocess
    preview = out / "web-ui-preview.html"
    preview.write_text(demo_html(), encoding="utf-8")
    base = [args.browser, "--session", "mrb-regression"]

    def run(*command):
        r = subprocess.run(base + list(command), capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
        if r.returncode:
            raise RuntimeError(f"{command[0]}: {r.stdout}\n{r.stderr}")
        return r.stdout.strip()

    try:
        run("open", preview.as_uri())
        run("set", "viewport", "1440", "1080")
        raw = run("--json", "eval", TESTS)
        report = json.loads(raw).get("data", {})
        print(json.dumps(report, ensure_ascii=False, indent=2))
        (out / "web-ui-test-results.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    finally:
        subprocess.run(base + ["close"], capture_output=True, timeout=30)


if __name__ == "__main__":
    main()
