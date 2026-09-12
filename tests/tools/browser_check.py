"""Drive a real Chrome against the board and report what the PAGE actually shows.

Why this exists: socket-level tests prove the server speaks websocket, but they
cannot prove the page's own JS wires it up (token fetch, cookie, reconnect
policy). This launches the Chrome that is installed on this machine, points it at
the board, and reads the live DOM - the same surface a human sees.

Nothing is inferred from a screenshot: every value comes from Runtime.evaluate.
The screenshot it saves at the end is for a human to eyeball, not an assertion.

This is the only check that covers the full browser path end to end, so it lives
here in the repo rather than in the gitignored build/ scratch dir.

Needs: the board reachable over Wi-Fi (serial `wifi on`, STA IP in IP below), the
CH343 serial port free, and Chrome installed at CHROME.

Run: python tests/tools/browser_check.py
"""
import base64
import json
import os
import sys
import time

sys.path.insert(0, r'C:\Users\<user>\.workbuddy\binaries\python\pylibs')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import serial  # noqa: E402
from cdp import Cdp, board_ip, find_page, fresh_profile, launch_chrome  # noqa: E402

PW = os.environ.get('MRB_PW', 'mrbtest99')
IP = board_ip()

PROBE_JS = """(() => {
  const t = id => { const e = document.getElementById(id); return e ? e.textContent.trim() : null; };
  const card = document.querySelector('#colL .k .act');
  return JSON.stringify({
    offHidden:  document.getElementById('off') ? document.getElementById('off').hidden : null,
    offWhy:     t('offWhy'),
    h1:         t('h1'),
    h2:         t('h2'),
    customCount:t('cnt'),
    pillRemote: t('pR'),
    pillHost:   t('pH'),
    pillBatt:   t('pB'),
    cardAction: card ? card.textContent.trim() : null,
    disabledCards: document.querySelectorAll('.k button[disabled]').length,
    totalCards: document.querySelectorAll('#colL .k, #colR .k').length,
    wsReady:    (typeof S !== 'undefined' && S) ? S.on : 'no-symbol',
    keyPress:   (typeof S !== 'undefined' && S) ? S.keyPress : null
  });
})()"""


SER = None


def console(cmd):
    """The port is opened once and kept open for the whole run. Opening it pulses
    RTS, which resets the board - doing that between steps is how the earlier
    scripts talked themselves into seeing an unreachable device."""
    global SER
    if SER is None:
        SER = serial.Serial('COM3', 115200, timeout=0.3, dsrdtr=False, rtscts=False)
        SER.dtr = False
        SER.rts = False
        time.sleep(0.4)
    SER.reset_input_buffer()
    SER.write(cmd)
    time.sleep(1.2)
    return SER.read(SER.in_waiting or 1).decode('utf-8', 'replace')


FAILURES = []


def check(label, ok, detail=''):
    if not ok:
        FAILURES.append(label)
    print('    [%s] %s%s' % ('PASS' if ok else 'FAIL', label, (' - ' + detail) if detail else ''))


print('[1] clear any password left on the board, then wait for boot')
console(b'pass clear\r')
time.sleep(28)

print('[2] launch the installed Chrome with a debugging port')
proc, port = launch_chrome(fresh_profile('cdp'))
ws_url = find_page(port)
if not ws_url:
    print('    Chrome never exposed a page target - aborting')
    proc.kill()
    sys.exit(2)
print('    ', ws_url.split('/')[-1])

try:
    cdp = Cdp(ws_url)
    cdp.call('Page.enable')
    cdp.call('Runtime.enable')
    cdp.call('Network.enable')
    cdp.call('Log.enable')

    print('\n[3] navigate: set a password, land on the app page')
    url = 'http://%s/setup?password=%s&confirm=%s' % (IP, PW, PW)
    cdp.call('Page.navigate', {'url': url})

    print('\n[4] let the page settle, then read the LIVE DOM')
    snapshot = None
    for attempt in range(8):
        time.sleep(2)
        r = cdp.call('Runtime.evaluate', {'expression': PROBE_JS, 'returnByValue': True})
        snapshot = json.loads(r['result']['value'])
        if snapshot['customCount'] not in (None, '—') or snapshot['offHidden'] is False:
            break
    print('    ', json.dumps(snapshot, ensure_ascii=False, indent=6))

    print('\n[4b] what Chrome actually put on the wire (Network domain)')
    urls = {}
    for ev in cdp.events:
        if ev.get('method') == 'Network.requestWillBeSent':
            urls[ev['params']['requestId']] = ev['params']['request']['url']
    cdp.drain(12)
    for ev in cdp.events:
        m = ev.get('method', '')
        p = ev.get('params', {})
        if m == 'Network.webSocketCreated':
            print('    CREATED  ', p.get('url'))
        elif m == 'Network.webSocketWillSendHandshakeRequest':
            print('    REQUEST  ')
            for h in (p.get('request', {}).get('headers') or {}).items():
                print('        %s: %s' % h)
        elif m == 'Network.webSocketHandshakeResponseReceived':
            r = p.get('response', {})
            print('    RESPONSE  HTTP %s' % r.get('status'))
            for line in sorted((r.get('headers') or {}).items()):
                print('        %s: %s' % line)
        elif m == 'Network.webSocketFrameError':
            print('    FRAME ERROR', p.get('errorMessage'))
        elif m == 'Network.webSocketClosed':
            print('    CLOSED   at t=%.1f s' % time.time())
        elif m == 'Network.webSocketFrameReceived':
            data = (p.get('response') or {}).get('payloadData', '')
            print('    FRAME IN ', repr(data[:400]))
        elif m == 'Network.webSocketFrameSent':
            data = (p.get('response') or {}).get('payloadData', '')
            print('    FRAME OUT', repr(data[:400]))
        elif m == 'Network.loadingFailed':
            print('    HTTP FAIL', urls.get(p.get('requestId'), '?'), p.get('errorText'))
        elif m in ('Runtime.exceptionThrown',):
            d = p.get('exceptionDetails', {})
            print('    JS EXCEPTION', (d.get('exception') or {}).get('description', d))
        elif m == 'Runtime.consoleAPICalled':
            print('    CONSOLE  ', [a.get('value') for a in p.get('args', [])])
        elif m == 'Log.entryAdded':
            e = p.get('entry', {})
            print('    LOG[%s]  %s' % (e.get('level'), e.get('text')))

    r = cdp.call('Runtime.evaluate', {'expression': PROBE_JS, 'returnByValue': True})
    after = json.loads(r['result']['value'])
    print('    after drain:', json.dumps(after, ensure_ascii=False))

    print('\n[5] verdict')
    check('page rendered all 13 key cards', snapshot['totalCards'] == 13, str(snapshot['totalCards']))
    check('no "cannot reach the bridge" banner', snapshot['offHidden'] is True,
          'offWhy=%r' % snapshot['offWhy'])
    check('header left the "reading device status" placeholder',
          snapshot['h1'] not in (None, '正在读取设备状态'), repr(snapshot['h1']))
    check('key cards got real actions (not "等待读取")',
          snapshot['disabledCards'] == 0, '%d still disabled' % snapshot['disabledCards'])
    check('custom-binding counter filled in', snapshot['customCount'] not in (None, '—'),
          repr(snapshot['customCount']))
    check('websocket reported open by the page', snapshot['wsReady'] is True,
          'S.on=%r' % snapshot['wsReady'])

    print('\n[6] the actual product requirement: press a key, page reacts live')
    # S.keyPress is only ever assigned by a `key` frame - the 5 s `status`
    # heartbeat goes through stat() and does not touch it. So before the first
    # key of the session the field is *undefined*, which JSON.stringify drops
    # (reads back as None). None therefore means "no key seen yet", i.e. 0 - it
    # is a valid baseline, not a sign the page is broken. Wait only for the
    # socket, then normalise.
    before = None
    for _ in range(15):
        r = cdp.call('Runtime.evaluate', {'expression': PROBE_JS, 'returnByValue': True})
        cur = json.loads(r['result']['value'])
        if cur['wsReady'] is True:
            before = cur.get('keyPress')
            break
        time.sleep(1)
    if not isinstance(before, int):
        print('    no key event yet this session (keyPress=%r) - treating as 0' % (before,))
        before = 0
    print('    baseline keyPress = %r' % (before,))
    SER.reset_input_buffer()
    SER.write(b'key 4a press\r')
    time.sleep(0.6)
    SER.write(b'key 4a release\r')
    time.sleep(2.0)
    r = cdp.call('Runtime.evaluate', {'expression': PROBE_JS, 'returnByValue': True})
    live = json.loads(r['result']['value'])
    print('    keyPress %s -> %s' % (before, live.get('keyPress')))
    tail = console(b'status\r')
    for line in tail.splitlines():
        if 'keyPresses' in line or 'injected' in line:
            print('    device:', line.strip())
    check('the page received the pushed key event',
          isinstance(live.get('keyPress'), int) and live['keyPress'] > before,
          'page key counter %s -> %s' % (before, live.get('keyPress')))
    check('page still connected afterwards', live.get('wsReady') is True)

    print('\n[7] screenshot for the record')
    shot = cdp.call('Page.captureScreenshot', {'format': 'png'})
    path = r'C:\code\MiRemoteBridge\outputs\web-ui-live.png'
    with open(path, 'wb') as f:
        f.write(base64.b64decode(shot['data']))
    print('    ', path)
finally:
    proc.kill()
    console(b'pass clear\r')
    if SER:
        SER.close()

print('\n' + '=' * 60)
if FAILURES:
    print('FAILED %d check(s): %s' % (len(FAILURES), ', '.join(FAILURES)))
    sys.exit(1)
print('REAL CHROME: ALL CHECKS PASSED')
