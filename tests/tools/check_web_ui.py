# SPDX-License-Identifier: GPL-3.0-or-later
"""Real Chromium regression against the extracted page + preview-only API adapter.

Usage: python tests/tools/check_web_ui.py --browser /path/to/agent-browser[.exe]
Requires agent-browser installed separately. No board/network changes occur.
This tests browser logic, not ESP32 HTTP/NVS durability or real BLE behavior.
"""
import argparse
import json
import subprocess
from pathlib import Path
from web_ui_preview import ROOT, demo_html

TESTS = r"""(async () => {
  const results=[];
  const assert=(condition,name)=>{if(!condition)throw Error(name);results.push(name);};
  await refresh();
  assert(state.online && state.loaded,'initial API load');
  assert(document.querySelectorAll('.key-card').length===13,'13 key cards');
  assert(document.querySelectorAll('#mappingRemote button').length===13,'13 physical hotspots');
  assert(fmt(current(62))==='Ctrl + Win','runtime voice mode, not static fallback');
  const voice=state.effective[62];state.effective[62]={raw:62,kind:0,mod:0,key:0,cons:0};
  assert(fmt(current(62)).includes('不转发'),'disabled mode does not fall back');
  state.effective[62]=voice;
  state.loading=true;updateButtons();openEditor(40);
  assert(!$('editor').open && document.querySelector('#card-40 button').disabled,'refresh blocks editing');
  state.loading=false;updateButtons();
  openEditor(40);state.busy=true;updateButtons();
  assert($('kind').disabled && $('key').disabled && $('cons').disabled && document.querySelector('.mod').disabled,'pending save locks draft controls');
  state.busy=false;updateButtons();
  const reads=__demo.calls.length;await refresh();
  assert(__demo.calls.length===reads,'editing suspends refresh');closeEditor();
  location.hash='mapping'; showPage();
  assert(!$('page-mapping').hidden && $('page-home').hidden,'mapping navigation');
  for (const k of KEYS) {
    document.querySelector('#card-'+k.raw+' button').click();
    assert($('editor').open && state.editing===k.raw,'open editor '+hex(k.raw));
    $('kind').value='1';state.mods=3;$('key').value='4';updateDraft();
    await save();
    assert(!$('editor').open,'save closes editor '+hex(k.raw));
    const call=__demo.calls.filter(c=>c.path==='/api/set').at(-1);
    assert(new URLSearchParams(call.query).get('raw')===hex(k.raw),'hex wire code '+hex(k.raw));
    assert(state.bindings[k.raw].mod===3 && state.bindings[k.raw].key===4,'saved correct key '+hex(k.raw));
  }
  openEditor(40);
  assert(state.mods===3 && $('key').value==='4','keyboard and modifier readback');
  await save();
  assert(state.bindings[40].mod===3,'no-edit save preserves modifiers');
  openEditor(128);$('kind').value='2';$('cons').value='205';updateDraft();await save();
  openEditor(128);
  assert($('cons').value==='205' && $('kind').value==='2','consumer decimal round trip');
  closeEditor();
  openEditor(62);$('kind').value='1';state.mods=64;$('key').value='54';updateDraft();await save();
  openEditor(62);assert(state.mods===64 && $('key').value==='54','right modifier + comma');
  state.mods=9;$('key').value='0';updateDraft();await save();
  assert(state.bindings[62].key===0 && state.bindings[62].mod===9,'modifier-only chord');
  openEditor(62);state.mods=0;$('key').value='0';updateDraft();await save();
  assert($('editor').open && !$('editError').hidden,'empty keyboard rejected');
  closeEditor();
  openEditor(241);$('clearBinding').click();
  assert(!!state.bindings[241],'clear only changes draft before save');
  await save();assert(!state.bindings[241] && current(241).cons===548,'clear reverts base');
  openEditor(40);$('key').value='5';__demo.fault='write';await save();
  assert($('editor').open && !$('editError').hidden && state.bindings[40].key===4,'HTTP error retains editor');
  __demo.fault='mismatch';await save();
  assert($('editor').open && $('editError').textContent.includes('不一致'),'readback mismatch rejected');
  __demo.fault=null;closeEditor();
  __demo.fault='offline';await refresh();
  assert(!state.online && !$('offlineBanner').hidden,'offline stale-data notice');
  assert(document.querySelector('#card-40 button').disabled,'offline writes disabled');
  __demo.fault=null;await refresh();assert(state.online,'reconnect recovery');
  __demo.fault='json';await refresh();assert(!state.online,'invalid JSON safely rejected');
  __demo.fault=null;await refresh();
  __demo.status.remoteName='<img src=x onerror=alert(1)> "test"';
  __demo.status.battery=-1;__demo.status.hostConnected=false;await refresh();
  assert(!$('remoteNameText').querySelector('img'),'remote name is text, not HTML');
  assert($('deviceBattery').textContent==='未知','unknown battery not fabricated');
  assert($('hostMetric').textContent==='待连接','host disconnection not ready');
  __demo.status.remoteConnected=false;await refresh();
  assert($('heroTitle').textContent.includes('等待'),'remote disconnection state');
  __demo.status.remoteName='小米蓝牙语音遥控器';__demo.status.battery=97;
  __demo.status.hostConnected=true;__demo.status.remoteConnected=true;await refresh();
  document.querySelector('.reset-all').click();$('cancelReset').click();
  assert(Object.keys(state.bindings).length>0,'reset cancellation preserves map');
  document.querySelector('.reset-all').click();__demo.fault='write';await resetAll();
  assert($('resetDialog').open && !$('resetError').hidden,'reset HTTP error visible');
  __demo.fault=null;await resetAll();
  assert(!$('resetDialog').open && Object.keys(state.bindings).length===0,'reset confirmed and read back');
  assert(current(62).mod===9,'reset preserves serial runtime mode');
  location.hash='device';showPage();assert(!$('page-device').hidden,'device navigation');
  location.hash='home';showPage();
  assert(!document.querySelector('script[src],link[rel="stylesheet"],img[src^="http"]'),'no external assets');
  return {passed:results.length,checks:results};
})()"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--browser", help="Path to the agent-browser executable")
    parser.add_argument("--emit-js", type=Path, help="Export assertions for CLI eval --stdin (no child process)")
    args = parser.parse_args()
    if args.emit_js:
        args.emit_js.write_text(TESTS, encoding="utf-8")
        print(f"Browser assertions: {args.emit_js.resolve()}")
        return
    if not args.browser:
        parser.error("--browser or --emit-js is required")
    out = ROOT / "outputs"
    out.mkdir(exist_ok=True)
    preview = out / "web-ui-preview.html"
    preview.write_text(demo_html(), encoding="utf-8")
    base = [args.browser, "--session", "mrb-regression"]

    def run(*command):
        result = subprocess.run(base + list(command), capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=100)
        if result.returncode:
            raise RuntimeError(f"{command[0]}: {result.stdout}\n{result.stderr}")
        return result.stdout.strip()

    def evaluate(code):
        raw = run("--json", "eval", code)
        response = json.loads(raw)
        if not response.get("success", True):
            raise RuntimeError(raw)
        return response.get("data", response)

    try:
        run("open", preview.as_uri())
        run("set", "viewport", "1440", "1080")
        print(run("snapshot", "-i"))
        report = evaluate(TESTS)
        print(json.dumps(report, ensure_ascii=False, indent=2))
        # Screenshots use defaults, not mappings left over from tests.
        evaluate("$('toast').hidden=true; location.hash='home';showPage();true")
        run("screenshot", str(out / "web-ui-home.png"))
        evaluate("location.hash='mapping';showPage();markSelection();true")
        run("snapshot", "-i")
        run("screenshot", str(out / "web-ui-mapping.png"))
        evaluate("openEditor(62);true")
        run("screenshot", str(out / "web-ui-editor.png"))
        # Escape closes the native dialog (with focus restoration).
        run("press", "Escape")
        evaluate("if($('editor').open)throw Error('Escape did not close dialog');true")
        for width in (1440, 1024, 768, 390, 320):
            run("set", "viewport", str(width), "900")
            for page in ("home", "mapping", "device"):
                evaluate(f"location.hash='{page}';showPage();true")
                evaluate("if(document.documentElement.scrollWidth>innerWidth)throw Error('horizontal overflow');true")
            if width == 390:
                evaluate("location.hash='mapping';showPage();true")
                run("snapshot", "-i")
                run("screenshot", str(out / "web-ui-mobile.png"), "--full")
        print("Responsive checks: 5 widths x 3 pages; no horizontal overflow")
        print("Browser errors:", run("errors"))
        report["responsive"] = "5 widths x 3 pages passed"
        report["scope"] = "Chromium + simulated API only; no hardware testing"
        (out / "web-ui-test-results.json").write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    finally:
        subprocess.run(base + ["close"], capture_output=True, timeout=30)


if __name__ == "__main__":
    main()
