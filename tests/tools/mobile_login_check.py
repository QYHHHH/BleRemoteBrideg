"""Phone-flow check: the real setup and login FORMS, submitted for real.

Coverage gap this closes: every earlier check reached /setup and /login by
hand-building a URL with the parameters already in it. That never exercised the
HTML forms - the field names, the browser's URL encoding of the typed password,
the submit, the redirect, the Set-Cookie, and the next request carrying it. A
phone only ever does the form path, so this is the first test that walks the
journey a phone actually walks.

Runs headless Chrome with an iPhone user agent, viewport and touch emulation.

Run: python tests/tools/mobile_login_check.py [ip]
"""

import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board_auth as ba  # noqa: E402
from cdp import Cdp, emulate_mobile, find_page, fresh_profile, launch_chrome  # noqa: E402

IP = ba.board_ip()

# Two rounds: a plain password, then one full of characters that must survive
# URL encoding - a space, an ampersand, an equals sign, a percent.
PASSWORDS = ['mrbtest99', 'p a&s=s%2B']


STATE_JS = """(() => {
  const t = s => { const e = document.querySelector(s); return e ? e.textContent.trim() : null; };
  return JSON.stringify({
    path: location.pathname,
    title: document.title,
    h1: t('h1'),
    cards: document.querySelectorAll('.k').length,
    inputs: [].map.call(document.querySelectorAll('input'), i => i.name),
    wsOpen: (typeof S !== 'undefined' && S) ? S.on : 'no-symbol'
  });
})()"""


def state(cdp, tries=12, pause=0.8):
    """Poll the page until it reports something, so we never read a mid-navigate DOM."""
    last = None
    for _ in range(tries):
        time.sleep(pause)
        v = cdp.evaluate(STATE_JS)
        if v:
            last = json.loads(v)
            if last.get('path') is not None:
                return last
    return last


def settle(cdp, timeout=25.0):
    """Wait for the app page to finish its first load.

    Landing on / is not the same as being loaded: the header still shows the
    placeholder and S.on is still false for as long as it takes the page to fetch
    /api/bindings, /api/status, /api/token and then open the websocket. Asserting
    on the DOM before that is the same trap as reading S.keyPress before any key
    has arrived - it reports working code as broken. Returns (state, seconds).
    """
    t0 = time.time()
    st = None
    while time.time() - t0 < timeout:
        v = cdp.evaluate(STATE_JS)
        if v:
            st = json.loads(v)
            if st.get('wsOpen') is True and st.get('h1') != '正在读取设备状态':
                return st, time.time() - t0
        time.sleep(0.5)
    return st, time.time() - t0


def fill_and_submit(cdp, values):
    """Set the form fields and submit the form the way a user does - requestSubmit()
    runs real constraint validation and produces the browser's real GET encoding."""
    js = """(() => {
      const f = document.querySelector('form');
      if (!f) return 'no form';
      const vals = %s;
      for (const k in vals) {
        const el = document.querySelector('input[name="' + k + '"]');
        if (!el) return 'missing input ' + k;
        el.value = vals[k];
        el.dispatchEvent(new Event('input', { bubbles: true }));
      }
      f.requestSubmit();
      return 'submitted';
    })()""" % json.dumps(values)
    return cdp.evaluate(js)


def navigate(cdp, url, wait=2.5):
    cdp.call('Page.navigate', {'url': url})
    time.sleep(wait)


def run_round(cdp, pw, label):
    print('\n--- %s : password %r ---' % (label, pw))

    print('  clear the board back to setup mode')
    # Round 1 opens the console, which reboots the board, so this waits for HTTP.
    # Round 2 reuses the open port: no reboot, nothing to wait for.
    ba.clear_password()

    print('  open the root URL as a phone would')
    navigate(cdp, 'http://%s/' % IP)
    st = state(cdp)
    print('   ', json.dumps(st, ensure_ascii=False))
    ba.check('root redirects to the setup page', st.get('path') == '/setup', str(st.get('path')))
    ba.check('setup form has the password fields',
          st.get('inputs') == ['password', 'confirm'], str(st.get('inputs')))

    print('  submit the SETUP form')
    print('   ', fill_and_submit(cdp, {'password': pw, 'confirm': pw}))
    st = state(cdp)
    print('   ', json.dumps(st, ensure_ascii=False))
    ba.check('setup signs the phone in and lands on the app', st.get('path') == '/', str(st.get('path')))
    ba.check('app page rendered its key cards', st.get('cards') == 13, str(st.get('cards')))

    ck = cdp.cookies('http://%s/' % IP)
    names = sorted(c['name'] for c in ck)
    print('    cookies:', names)
    ba.check('session cookie was stored by the browser', 'mrb_sess' in names, str(names))

    print('  sign out, then sign back in through the LOGIN form')
    navigate(cdp, 'http://%s/logout' % IP)
    st = state(cdp)
    print('   ', json.dumps(st, ensure_ascii=False))
    ba.check('logout lands on the login page', st.get('path') == '/login', str(st.get('path')))
    ba.check('login form has a password field',
          st.get('inputs') == ['password'], str(st.get('inputs')))

    print('  submit the LOGIN form')
    print('   ', fill_and_submit(cdp, {'password': pw}))
    st, took = settle(cdp)
    print('    settled in %.1f s: %s' % (took, json.dumps(st, ensure_ascii=False)))
    ba.check('login is accepted and lands on the app', st.get('path') == '/', str(st.get('path')))
    ba.check('app page rendered after login', st.get('cards') == 13, str(st.get('cards')))
    ba.check('page reports the websocket is open', st.get('wsOpen') is True, str(st.get('wsOpen')))
    ba.check('header left the placeholder', st.get('h1') != '正在读取设备状态', repr(st.get('h1')))

    print('  a WRONG password must not sign in')
    navigate(cdp, 'http://%s/logout' % IP)
    state(cdp)
    fill_and_submit(cdp, {'password': pw + '-wrong'})
    st = state(cdp)
    print('   ', json.dumps(st, ensure_ascii=False))
    ba.check('wrong password does not reach the app', st.get('cards') != 13, str(st.get('cards')))


def main():
    print('board %s' % IP)
    proc, port = launch_chrome(fresh_profile('mobile'))
    try:
        ws_url = find_page(port)
        if not ws_url:
            print('    Chrome never exposed a page target - aborting')
            return 2
        cdp = Cdp(ws_url)
        cdp.enable_all()
        emulate_mobile(cdp)
        print('    emulating iPhone (%s)' % cdp.evaluate('navigator.userAgent')[:60])

        for i, pw in enumerate(PASSWORDS, 1):
            run_round(cdp, pw, 'round %d' % i)
    finally:
        proc.kill()
        ba.console(b'pass clear\r')
        ba.close_console()

    return ba.report('MOBILE FORM FLOW: ALL CHECKS PASSED')


if __name__ == '__main__':
    sys.exit(main())
