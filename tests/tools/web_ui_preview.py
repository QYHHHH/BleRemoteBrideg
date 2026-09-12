# SPDX-License-Identifier: GPL-3.0-or-later
"""Extract the firmware page and add an explicitly labeled, device-free demo.

Usage: python tests/tools/web_ui_preview.py [--output outputs/web-ui-preview.html]
The demo adapter exists ONLY in the generated preview, never in firmware.
"""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def firmware_html():
    source = (ROOT / "firmware/MiRemoteBridge/web_page.h").read_text(encoding="utf-8")
    return source.split('R"rawliteral(', 1)[1].split(')rawliteral";', 1)[0]


def demo_html():
    # Fixture: default physical keys; voice uses the real runtime-mode chord,
    # intentionally different from the static table to detect wrong UI fallback.
    rows = [(0x66, 1, 4, 61, 0), (0x52, 1, 0, 82, 0),
            (0x50, 1, 0, 80, 0), (0xF1, 2, 0, 0, 548),
            (0x4A, 1, 8, 7, 0), (0x65, 1, 0, 44, 0),
            (0x3E, 1, 64, 54, 0), (0x4F, 1, 0, 79, 0),
            (0x28, 1, 0, 40, 0), (0x51, 1, 0, 81, 0),
            (0x80, 2, 0, 0, 233), (0x81, 2, 0, 0, 234),
            (0x35, 1, 0, 65, 0)]
    defaults = [dict(zip(("raw", "kind", "mod", "key", "cons"), row)) for row in rows]
    adapter = r'''<script>
// Preview-only adapter: no network request can reach a device.
(() => {
  const defaults = FIXTURE;
  const base = defaults.map(a => a.raw === 62 ? {...a, mod:9, key:0} : {...a});
  let bindings = [];
  try { bindings = JSON.parse(sessionStorage.getItem('mrb-ui-demo') || '[]'); } catch (_) {}
  const control = window.__demo = {fault:null, calls:[], status:{wifi:true,ap:'MiRemoteBridge',hostConnected:true,remoteConnected:true,remoteName:'小米蓝牙语音遥控器',battery:97,fwVersion:'v0.0.1',buildTime:'Sep 12 2026 12:56:33'}};
  window.fetch = async (input, options={}) => {
    const u = new URL(input, 'http://preview.invalid');
    const method = options.method || 'GET';
    control.calls.push({path:u.pathname, query:u.search, method});
    if (control.fault === 'offline') throw new TypeError('Failed to fetch');
    if (control.fault === 'json') return new Response('{', {status:200});
    const json = (data, status=200) => new Response(JSON.stringify(data), {status,headers:{'Content-Type':'application/json'}});
    if (method === 'GET' && u.pathname === '/api/status') return json({...control.status,bindings:bindings.length});
    if (method === 'GET' && u.pathname === '/api/bindings') return json({bindings,defaults,effective:base.map(a=>bindings.find(b=>b.raw===a.raw)||a)});
    if (method !== 'POST') return json({error:'not found'},404);
    if (control.fault === 'write') return json({error:'模拟：设备拒绝保存'},400);
    if (control.fault === 'mismatch') return json({ok:true});
    if (u.pathname === '/api/set') {
      const raw = parseInt(u.searchParams.get('raw'),16), kind = Number(u.searchParams.get('kind'));
      const mod = Number(u.searchParams.get('mod')), key = Number(u.searchParams.get('key')), cons = Number(u.searchParams.get('cons'));
      if (!raw || raw>255 || ![0,1,2].includes(kind) || (kind===1&&!mod&&!key) || (kind===2&&!cons)) return json({error:'invalid binding'},400);
      bindings = bindings.filter(a=>a.raw!==raw);
      if (kind) bindings.push({raw,kind,mod,key,cons});
    } else if (u.pathname === '/api/reset') { bindings = []; }
    else return json({error:'not found'},404);
    try { sessionStorage.setItem('mrb-ui-demo',JSON.stringify(bindings)); } catch (_) {}
    return json({ok:true});
  };
  document.addEventListener('DOMContentLoaded',()=>document.getElementById('demo').hidden=false);
})();
</script>
'''.replace("FIXTURE", json.dumps(defaults, ensure_ascii=False))
    # The firmware generator splits these into local /app.css and /app.js
    # endpoints. The standalone preview keeps them inline so it remains one
    # file that browsers can open without a server.
    #
    # Inject ahead of the page's own <script>, matching the TAG rather than the
    # exact whitespace after it. The previous version anchored on
    # "<script>\n'use strict';" and silently stopped matching the day an empty
    # line appeared after the tag: str.replace() was a no-op, the preview ran
    # with no fetch simulator, and every assertion failed for a reason that had
    # nothing to do with the page. Fail loudly instead of silently doing nothing.
    html = firmware_html()
    at = html.find("<script>")
    if at < 0:
        raise SystemExit("web_page.h has no <script> block to inject the adapter into")
    return html[:at] + adapter + html[at:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "outputs/web-ui-preview.html")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(demo_html(), encoding="utf-8")
    print(f"Preview: {args.output.resolve()}")
    print(f"Firmware HTML: {len(firmware_html().encode('utf-8'))} bytes; no external assets")


if __name__ == "__main__":
    main()
