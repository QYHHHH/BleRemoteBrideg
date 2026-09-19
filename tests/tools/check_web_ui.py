# SPDX-License-Identifier: GPL-3.0-or-later
"""Page regression for the firmware page, without a board.

Extracts the page from web_page.h, injects a PREVIEW-ONLY fetch simulator
(never shipped in firmware) and runs the assertions below in the Chrome that is
installed here, driven over CDP by cdp.py - the same client the board-facing
checks use. Nothing has to be installed beyond Chrome and Python.

Two things are covered:
  * the flash budget - the three gzip assets are what is actually compiled in
  * the page logic - registration, editing, readback, offline and reset paths

Usage:
  python tests/tools/check_web_ui.py                  # budget + assertions
  python tests/tools/check_web_ui.py --check-size     # budget only (fast)
  python tests/tools/check_web_ui.py --emit-js out.js # write the assertions out

This covers page logic only: not the ESP32 HTTP stack, NVS durability,
Wi-Fi/BLE coexistence headroom or a real phone browser (docs/TESTING.md 4.12).
Those need a board - see browser_check.py.
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_web_page  # noqa: E402
from cdp import CHROME, Cdp, find_page, fresh_profile, launch_chrome  # noqa: E402
from web_ui_preview import ROOT, demo_html  # noqa: E402

# The check watches the packed gzip assets that are stored in flash, rather than
# the larger source page. The ceiling is a tripwire for accidental asset growth;
# low runtime heap and partial TCP writes are handled by the firmware's
# streaming path and are not fixed by an artificially tight page-size number.
# Raised from 24000 when the page became bilingual: the Chinese override
# dictionary is a deliberate ~5 KB, so the old number would have failed every
# run rather than catching anything.
FLASH_BUDGET = 28000

# How many assertions TESTS is expected to report. A drop means assertions were
# deleted or silently stopped running - both worth failing over.
EXPECTED_CHECKS = 170

TESTS = r"""(async () => {
  const results=[];
  const assert=(condition,name)=>{if(!condition)throw Error(name);results.push(name);};
  // The page boots in Chinese; every assertion below reads the authored English
  // source text, so note what the boot language was and switch once up front.
  // The bilingual block near the end checks the boot default and the round trip.
  const bootLang=LI,bootHtmlLang=document.documentElement.lang;setLang(0);
  while(S.load)await new Promise(r=>setTimeout(r,10));await load();
  assert(S.on && S.ok,'initial API load');
  assert($('hostRepair').hidden&&$('remoteRepair').hidden,'both repair notices hidden normally');
  __demo.status.hostRepairRequired=true;await load();
  assert(!$('hostRepair').hidden&&$('hostRepair').textContent.includes('Remove Mi Remote Bridge in Windows and add it again'),'stale computer key tells the user to re-add it in Windows');
  assert(!document.querySelector('#hostRepair button'),'the computer notice has no button to press');
  assert(__demo.calls.every(c=>c.path!=='/api/host-pairing'),'nothing ever posts to the removed host-pairing endpoint');
  __demo.status.hostRepairRequired=false;__demo.status.remoteRepairRequired=true;await load();
  assert($('hostRepair').hidden&&!$('remoteRepair').hidden,'the remote notice shows independently of the computer one');
  assert($('remoteRepair').textContent.includes("The remote's pairing data is no longer valid, so automatic reconnection has stopped. To keep using it, put the remote into pairing mode and pick it from the nearby devices."),'remote notice carries the exact instruction');
  wsMessage({type:'status',...__demo.status});
  assert(!$('remoteRepair').hidden,'the notice stays latched across a status heartbeat');
  $('repairRemote').onclick();
  assert(pairing.open,'the remote notice opens the device picker');
  const connects=()=>__demo.calls.filter(c=>c.path==='/api/connect').length;
  const before=connects();
  await $('nearby').onclick({target:{closest:()=>({dataset:{address:'00:11:22:33:44:00',type:'0'}})}});
  assert(connects()===before+1,'picking a device from the list sends the connect request');
  pairing.close();
  __demo.devices[0]={name:'RC003',address:'',identity:''};
  __demo.status.remoteRepairRequired=false;await load();
  assert($('remoteRepair').hidden,'the notice clears once the pairing works again');
  S.busy=true;S.slotBusy=true;btns();assert(!document.querySelector('[data-slot="1"]').disabled,'slot cards stay clickable to interrupt');S.busy=false;S.slotBusy=false;btns();
  const oldConfirm=window.confirm,pairCalls=()=>__demo.calls.filter(c=>c.path==='/api/slot');
  window.confirm=()=>false;await $('pairRemote').onclick();
  assert(pairCalls().length===0,'pair cancellation sends no request');
  window.confirm=()=>true;await $('pairRemote').onclick();
  assert(pairCalls().length===1&&pairCalls()[0].method==='POST','confirmed pairing queues POST');
  online(false);await $('pairRemote').onclick();
  assert(pairCalls().length===1&&$('pairRemote').disabled,'offline pairing is blocked');
  online(true);window.confirm=oldConfirm;

  stat({...__demo.status,heapTotal:20480,heapFree:15360,heapMin:12288,heapLargest:8192});
  assert($('memFree').textContent.includes('15.0 KiB'),'free heap rendered in KiB');
  assert($('memPct').textContent==='25.0%'&&$('memTotal').textContent.includes('20.0 KiB'),'used heap percentage and total rendered');
  assert($('memDetail').textContent.includes('12.0 KiB')&&$('memDetail').textContent.includes('8.0 KiB'),'minimum and largest block rendered');
  wsMessage({type:'status',...__demo.status,heapTotal:20480,heapFree:10240,heapMin:9216,heapLargest:6144});
  assert($('memFree').textContent.includes('10.0 KiB'),'heartbeat updates memory');
  assert($('memPct').textContent==='50.0%','heartbeat updates used heap percentage');
  assert($('ver').textContent.includes('v0.0.1'),'compact heartbeat preserves version and build stamp');
  online(false);
  assert($('pW').textContent.includes('disconnected')&&$('pW').classList.contains('off'),'board shows web control disconnected');
  assert($('uiReset').disabled&&document.querySelector('#k40 button').disabled,'disconnect locks all mutation controls');
  wsClosed({code:1006});assert(S.wsTimer!==null,'unexpected socket loss schedules automatic reconnect');clearTimeout(S.wsTimer);S.wsTimer=null;
  S.claimed=true;wsClosed({code:1006});assert(S.wsTimer===null&&$('offWhy').textContent.includes('Another page took control'),'claimed socket does not reconnect');
  let reconnects=0,claimed=false,oldConnect=connectWS;connectWS=m=>{reconnects++;claimed=m};$('pW').click();connectWS=oldConnect;
  assert(reconnects===1&&claimed,'board connection pill manually reclaims control');
  assert(!$('memState').textContent.includes('5'),'disconnect removes live update claim');online(true);
  __demo.status.fwVersion='v9.9.9';__demo.status.buildTime='Jan  1 2030 00:00:00';await load();
  assert($('ver').textContent==='v9.9.9 · build Jan  1 2030 00:00:00','version and build stamp come from /api/status');
  delete __demo.status.fwVersion;delete __demo.status.buildTime;await load();
  assert($('ver').textContent==='','no stale stamp when firmware omits it');
  __demo.status.fwVersion='v0.0.1';__demo.status.buildTime='Sep 12 2026 12:56:33';await load();
  __demo.status.battery=-1;await load();assert($('slots').textContent.includes('Battery not read'),'missing live battery is not displayed as cached percentage');__demo.status.battery=97;await load();
  await slotAction('select',1);
  assert(selectedSlot===1&&$('grid').hidden,'select slot activates generic remote page');
  await slotAction('add',1);
  assert(!slots[1].address,'scan does not automatically bind a device');
  await req('/api/connect?address=00:11:22:33:44:01&type=0','POST');await load();
  assert(slots[1].address&&$('pairRemote').hidden,'explicit device selection is persisted');
  __demo.keys[1].push({raw:144,name:'自定义键'});await load();
  openSlotEditor(144);$('slotKind').value='1';$('slotKey').value='4';await autoSave();
  assert(S.b[144]&&S.b[144].key===4,'generic discovered key saves real binding');
  $('slotName').value='新名称';await $('slotName').onchange();
  assert(learned[0].name==='新名称','key rename reads back from board');
  editor.close();await slotAction('select',0);
  assert(!S.b[144],'switch isolates mappings');
  await slotAction('select',1);
  assert(S.b[144].key===4,'switch back restores previous mapping');
  await slotAction('delete',1);
  assert(!slots[1].address&&!S.b[144]&&learned.length===0,'device deletion clears discovered keys and shortcuts');
  await slotAction('add',1);await req('/api/connect?address=00:11:22:33:44:01&type=0','POST');await load();await reset();
  assert(slots[1].address&&!S.b[144]&&learned.length===0,'shortcut reset retains pairing after deleted keys stay cleared');
  await slotAction('select',0);
  assert(document.querySelectorAll('.k').length===13,'13 key cards');
  assert(document.querySelectorAll('#rmArt [data-r]').length===13,'13 remote hotspots');
  assert(fmt(cur(0x3E))==='Ctrl + Win','runtime voice mode wins over static table');
  const voice=S.e[0x3E];S.e[0x3E]={raw:0x3E,kind:0,mod:0,key:0,cons:0};
  assert(fmt(cur(0x3E)).indexOf('Not forwarded')>=0,'disabled mode does not fall back');
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
  assert($('ed').open && $('edE').textContent.indexOf('does not match')>=0,'readback mismatch rejected');
  __demo.fault=null;$('ed').close();
  __demo.fault='offline';await load();
  assert(!S.on && !$('off').hidden,'offline shows stale-data notice');
  assert(document.querySelector('#k40 button').disabled,'offline writes disabled');
  __demo.fault=null;await load();assert(S.on,'reconnect recovery');
  __demo.fault='json';await load();assert(!S.on,'invalid JSON safely rejected');
  __demo.fault=null;await load();
  __demo.status.remoteName='<img src=x onerror=alert(1)> "test"';
  __demo.status.battery=-1;__demo.status.hostConnected=false;await load();
  assert(!$('h2').querySelector('img') && $('h1').textContent.indexOf('waiting for host')>=0,'remote name is text, not HTML');
  assert($('pB').lastChild.textContent==='Battery unknown','unknown battery not fabricated');
  __demo.status.remoteConnected=false;await load();
  assert($('h1').textContent.indexOf('Waiting for the remote')>=0,'remote disconnection state');
  __demo.status.remoteName='\u5c0f\u7c73\u84dd\u7259\u8bed\u97f3\u9065\u63a7\u5668';__demo.status.battery=97;
  __demo.status.hostConnected=true;__demo.status.remoteConnected=true;await load();
  $('resetAll').click();$('rdC').click();
  assert(Object.keys(S.b).length>0,'reset cancellation preserves map');
  $('resetAll').click();__demo.fault='write';await reset();
  assert($('rd').open && !$('rdE').hidden,'reset HTTP error visible');
  __demo.fault=null;await reset();
  assert(!$('rd').open && Object.keys(S.b).length===0,'reset confirmed and read back');
  assert(cur(0x3E).mod===9,'reset preserves serial runtime mode');
  // Every hotspot carries a position class alongside .rb, and several of those
  // set background / border / box-shadow / color themselves. They used to sit
  // after .rb.live/.rb.sel at the same specificity, so source order beat the
  // states: the four D-pad keys (du/dd/dl/dr) never lit up at all, power, mic,
  // OK and the volume rocker only half did, and .pulse - added by the page on
  // every real key press - had no rule anywhere. Assert per key, because the
  // whole defect was that keys differ from each other.
  const vis=el=>{const s=getComputedStyle(el);
    return [s.backgroundColor,s.backgroundImage,s.borderTopColor,s.boxShadow,s.color].join('|');};
  const states=el=>{
    // .k carries transition:.15s: reading computed style in the same tick as a
    // class change returns the pre-transition value and every state looks dead.
    const t=el.style.transition;el.style.transition='none';
    el.classList.remove('live');el.classList.remove('sel');el.classList.remove('pulse');
    const base=vis(el);
    el.classList.add('live');const live=vis(el);el.classList.remove('live');
    el.classList.add('sel');const sel=vis(el);el.classList.remove('sel');
    el.classList.add('pulse');const pulse=vis(el);el.classList.remove('pulse');
    el.style.transition=t;return {base:base,live:live,sel:sel,pulse:pulse};};
  for (const k of KEYS) {
    const s=states(document.querySelector('#rmArt [data-r="'+k[0]+'"]'));
    assert(s.live!==s.base,'hotspot lights up while held '+hx(k[0]));
    assert(s.sel!==s.base,'hotspot marks the key being edited '+hx(k[0]));
    assert(s.pulse!==s.base,'hotspot flashes on a real press '+hx(k[0]));
  }
  // The cards carry no position class, so one rule covers all thirteen.
  assert(KEYS.every(k=>{const s=states($('k'+k[0]));return s.live!==s.base;}),'every card lights up while held');
  assert(KEYS.every(k=>{const s=states($('k'+k[0]));return s.sel!==s.base;}),'every card marks the key being edited');
  assert(!document.querySelector('img[src^=http]'),'no remote assets');
  // Bilingual UI. English is the authored source, Chinese is what the page
  // shows by default; ZH is an override layer, so a missing entry must fall
  // back rather than render a key. Switching repaints in place - the round trip
  // back to English proves the cached originals survive, which is what the
  // dictionary deliberately omits.
  assert(bootLang===1&&bootHtmlLang==='zh','page defaults to Chinese');
  assert($('repoLink').getAttribute('href')==='https://github.com/QYHHHH/BleRemoteBrideg'&&$('siteLink').getAttribute('href')==='https://qyhhhh.github.io/BleRemoteBrideg/','top bar links to the real repository and project site');
  assert(document.querySelectorAll('.slot .slot-line .slot-link').length===3&&document.querySelectorAll('.slot .slot-line .slot-battery').length===3,'slot link state and battery share one row');
  const enCard=$('k40').querySelector('b').textContent;
  setLang(1);
  assert(LI===1&&document.documentElement.lang==='zh','switching sets the Chinese document language');
  assert($('k40').querySelector('b').textContent==='确定键','JS-rendered key cards follow the language');
  assert($('rdY').textContent==='确认恢复','authored markup follows the language');
  assert(fmt({kind:0}).indexOf('不转发')>=0,'runtime status strings follow the language');
  assert(pick(CSL(),233,'')==='音量 +','select option labels follow the language');
  setLang(0);
  assert($('k40').querySelector('b').textContent===enCard&&$('rdY').textContent==='Confirm restore','switching back restores the authored English');
  return {passed:results.length,checks:results};
})()"""


def flash_parts():
    """The three assets exactly as gen_web_page would emit them: (name, raw, gz)."""
    parts = gen_web_page.split(gen_web_page.firmware_html())
    return [(name, len(text.encode("utf-8")), len(gen_web_page.compress(text)))
            for name, text in zip(("html", "css", "js"), parts)]


def check_budget():
    """Compare the compiled-in gzip payloads against the reviewed budget."""
    parts = flash_parts()
    total = sum(gz for _, _, gz in parts)
    print("flash budget: %d B in gzip assets (limit %d B)" % (total, FLASH_BUDGET))
    for name, raw, gz in parts:
        print("    %-5s %6d raw -> %5d gzip" % (name, raw, gz))
    if total > FLASH_BUDGET:
        print("FAILED: the page is %d B over the reviewed flash budget"
              % (total - FLASH_BUDGET))
        return 1
    return 0


def run_assertions():
    """Run TESTS in the installed Chrome, against the generated preview page.

    This used to require an `agent-browser` executable passed in as --browser -
    a tool that is not part of this repo's toolchain, so in practice the
    assertions were unreachable: scripts/test.ps1 only ever wired up
    --check-size. cdp.py already drives the installed Chrome over its debugging
    port, so this carries its own runner and needs nothing extra.
    """
    out = ROOT / "outputs"
    out.mkdir(exist_ok=True)
    preview = out / "web-ui-preview.html"
    preview.write_text(demo_html(), encoding="utf-8")

    proc, port = launch_chrome(fresh_profile("page"))
    try:
        ws_url = find_page(port)
        if not ws_url:
            print("Chrome never exposed a page target - is it installed at %s?" % CHROME)
            return 2
        cdp = Cdp(ws_url)
        cdp.call("Page.enable")
        cdp.call("Runtime.enable")
        cdp.call("Page.navigate", {"url": preview.as_uri()})

        # The assertions drive the page's own load()/btns(), so wait for the
        # script to define them. Evaluating earlier reports "load is not
        # defined", which is a race, not a page defect.
        ready = False
        for _ in range(60):
            if cdp.evaluate("typeof load") == "function":
                ready = True
                break
            time.sleep(0.25)
        if not ready:
            print("the page never defined load() - the preview did not execute")
            return 1

        r = cdp.call("Runtime.evaluate",
                     {"expression": TESTS, "returnByValue": True, "awaitPromise": True},
                     timeout=120)
        if "exceptionDetails" in r:
            detail = (r["exceptionDetails"].get("exception") or {})
            print("assertion FAILED: %s" % detail.get("description", r["exceptionDetails"]))
            return 1

        # returnByValue hands the object back already decoded; older clients
        # returned it as a JSON string, so accept both.
        value = r["result"].get("value")
        result = json.loads(value) if isinstance(value, str) else (value or {})
        passed = result.get("passed", 0)
        print("page assertions: %d passed" % passed)
        for name in result.get("checks", [])[-5:]:
            print("    ok  %s" % name)
        if EXPECTED_CHECKS is not None and passed != EXPECTED_CHECKS:
            print("FAILED: expected %d assertions, %d ran - assertions went missing"
                  % (EXPECTED_CHECKS, passed))
            return 1
        return 0
    finally:
        proc.kill()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-js", type=Path,
                        help="write the assertions to a file instead of running them")
    parser.add_argument("--check-size", action="store_true",
                        help="check the flash budget only")
    args = parser.parse_args()

    if args.emit_js:
        args.emit_js.write_text(TESTS, encoding="utf-8")
        print("Browser assertions: %s" % args.emit_js.resolve())
        return 0

    code = check_budget()
    if code == 0 and not args.check_size:
        code = run_assertions()
    sys.exit(code)


if __name__ == "__main__":
    main()
