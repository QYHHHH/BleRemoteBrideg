"""HTTP-level guard check for the setup/login surface.

Three things this covers that the browser checks cannot, because a browser
follows redirects and fills forms:

  1. SETUP MODE ROUTING. With no password stored, /login used to render a real
     login form. Nothing can ever match against it - there is no stored
     password - so every submission came back "密码错误". Anyone who reached
     /login (a bookmark, browser history, autocomplete) was stuck on a form
     that rejected everything. It now redirects to /setup.

  2. THE PASSWORD-BY-GET HOLE. /setup was exempt from the session check, so
     `GET /setup?password=x&confirm=x` replaced the stored password with no
     authentication at all - one request from anyone on the Wi-Fi, and the
     caller even got the session cookie back. /setup now needs a session once a
     password exists; a signed-in user can still change the password with it.

  3. The rest of the surface: the login form, the wrong-password path, the
     /api/token session gate and the websocket token gate.

Needs COM3 (to `pass clear` the board back to a known state) and the board
reachable over Wi-Fi.

Run: python tests/tools/auth_guard_check.py [--leave-password PW] [--no-reset]
"""

import argparse
import base64
import json
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import board_auth as ba  # noqa: E402

IP = ba.IP

# Only [A-Za-z0-9._-] so the query string means exactly what it says: the
# firmware does no percent-decoding.
PW = 'mrbauth-A1'
PW2 = 'mrbauth-B2'
EVIL = 'mrbauth-EVIL'

FAILURES = []


def check(label, ok, detail=''):
    if not ok:
        FAILURES.append(label)
    print('    [%s] %s%s' % ('PASS' if ok else 'FAIL', label, (' - ' + detail) if detail else ''))


def ws_handshake(path):
    """Raw upgrade attempt; returns the response status line."""
    key = base64.b64encode(os.urandom(16)).decode()
    s = socket.socket()
    s.settimeout(6)
    try:
        s.connect((IP, 80))
        s.sendall(('GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\n'
                   'Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
                   'Sec-WebSocket-Version: 13\r\n\r\n' % (path, IP, key)).encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            c = s.recv(4096)
            if not c:
                break
            buf += c
        return buf.split(b'\r\n')[0].decode('utf-8', 'replace')
    finally:
        s.close()


def redirects_to(headers, where):
    return headers.get('Location') == where


def app_shell(body):
    """The app page is the only asset that links /app.css and /app.js out to
    separate requests; the setup and login pages are self-contained. Checking
    for the string "MiRemoteBridge" would not distinguish anything - it is in
    the login page's title too."""
    return '/app.css' in body and '/app.js' in body


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--leave-password', default='',
                    help='password to leave on the board (default: clear it)')
    ap.add_argument('--leave-current', default=PW2,
                    help='the password the board holds when the run ends, needed '
                         'to change it (the /setup gate requires a session)')
    ap.add_argument('--no-reset', action='store_true',
                    help='skip `pass clear`; requires the board to already be passwordless')
    args = ap.parse_args()

    print('board %s' % IP)

    if not args.no_reset:
        print('reset: clearing the stored password so we start in setup mode')
        got = ba.clear_password()
        print('   state after reset:', got)
    st = ba.state()
    if st != 'setup':
        print('    board is %r, not passwordless - pass clear first, or drop --no-reset' % st)
        return 2

    # --- 1. setup mode routing ---------------------------------------------
    print('\n[1] setup mode: what may a visitor reach?')
    st, hd, body = ba.http('/')
    check('no password + / -> /setup', st == 302 and redirects_to(hd, '/setup'),
          '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/login')
    check('/login -> /setup instead of a login form that cannot match',
          st == 302 and redirects_to(hd, '/setup'), '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/logout')
    check('/logout -> /setup', st == 302 and redirects_to(hd, '/setup'),
          '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/setup')
    check('the setup form is served', st == 200 and ba.title(body) == '设置访问密码',
          '%s %s' % (st, ba.title(body)))
    check('and it asks for both fields',
          body.count('name="password"') == 1 and body.count('name="confirm"') == 1,
          'password=%d confirm=%d' % (body.count('name="password"'), body.count('name="confirm"')))

    # --- 2. the form's own validation --------------------------------------
    print('\n[2] setup form validation')
    st, hd, body = ba.http('/setup?password=abc&confirm=abc')
    check('a 3-character password is refused (minimum is 4)',
          ba.title(body) == '设置访问密码', ba.title(body))
    st, hd, body = ba.http('/setup?password=%s&confirm=nope-nope' % PW)
    check('a mismatched confirm is refused', ba.title(body) == '两次输入不一致', ba.title(body))

    # --- 3. first-time setup ------------------------------------------------
    print('\n[3] first-time setup')
    st, hd, body = ba.set_password(PW)
    cookie = ba.session_of(hd)
    check('setup stores the password and signs the caller in',
          st == 302 and redirects_to(hd, '/') and bool(cookie),
          '%s %s cookie=%s' % (st, hd.get('Location'), bool(cookie)))
    st, hd, body = ba.http('/')
    check('with a password set, / needs a session', st == 302 and redirects_to(hd, '/login'),
          '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/', cookie=cookie)
    check('the session cookie opens the app page', st == 200 and app_shell(body),
          '%s %d B app-shell=%s' % (st, len(body), app_shell(body)))

    # --- 4. the hole --------------------------------------------------------
    print('\n[4] the password-by-GET hole (this used to succeed)')
    st, hd, body = ba.http('/setup')
    check('an anonymous /setup is sent to /login', st == 302 and redirects_to(hd, '/login'),
          '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/setup?password=%s&confirm=%s' % (EVIL, EVIL))
    check('an anonymous /setup does NOT set a password',
          st == 302 and redirects_to(hd, '/login') and not ba.session_of(hd),
          '%s %s cookie=%s' % (st, hd.get('Location'), bool(ba.session_of(hd))))
    st, hd, body = ba.http('/login?password=%s' % EVIL)
    check('the attacker password was not stored', ba.title(body) == '密码错误', ba.title(body))
    st, hd, body = ba.http('/login?password=%s' % PW)
    check('the original password still works', st == 302 and redirects_to(hd, '/'),
          '%s %s' % (st, hd.get('Location')))

    # --- 5. changing a password while signed in ----------------------------
    print('\n[5] a signed-in user can still change the password')
    st, hd, body = ba.http('/setup', cookie=cookie)
    check('a signed-in user still gets the form',
          st == 200 and 'name="confirm"' in body, '%s %s' % (st, ba.title(body)))
    st, hd, body = ba.set_password(PW2, cookie=cookie)
    cookie2 = ba.session_of(hd)
    check('the change is accepted and re-issues a session',
          st == 302 and redirects_to(hd, '/') and bool(cookie2),
          '%s %s cookie=%s' % (st, hd.get('Location'), bool(cookie2)))
    st, hd, body = ba.http('/', cookie=cookie2)
    check('the re-issued session opens the app page', st == 200 and app_shell(body),
          '%s app-shell=%s' % (st, app_shell(body)))
    st, hd, body = ba.http('/login?password=%s' % PW2)
    check('the new password works', st == 302 and redirects_to(hd, '/'),
          '%s %s' % (st, hd.get('Location')))
    st, hd, body = ba.http('/login?password=%s' % PW)
    check('the old password no longer works', ba.title(body) == '密码错误', ba.title(body))

    # --- 6. the rest of the surface ----------------------------------------
    print('\n[6] login form, token gate, websocket gate')
    st, hd, body = ba.http('/login')
    check('the login form is served', st == 200 and ba.title(body) == '登录 - MiRemoteBridge',
          '%s %s' % (st, ba.title(body)))
    st, hd, body = ba.http('/login?password=definitely-wrong')
    check('a wrong password is refused', ba.title(body) == '密码错误', ba.title(body))
    st, hd, body = ba.http('/api/token')
    check('/api/token without a session is a JSON 401', st == 401, str(st))
    st, hd, body = ba.http('/api/token', cookie=cookie2)
    ok = st == 200
    try:
        tok = json.loads(body)['token']
        ok = ok and len(tok) == 40
    except Exception:  # noqa: BLE001
        tok, ok = '', False
    check('/api/token with the session returns a 40-hex token', ok, body[:60])
    line = ws_handshake('/ws')
    check('/ws without a token is refused', '401' in line, line)
    time.sleep(1.5)
    line = ws_handshake('/ws?token=deadbeef')
    check('/ws with a wrong token is refused', '401' in line, line)
    # The /ws attempt was refused but the single exchange slot was still in use;
    # give the board a moment to notice the closed socket before the next
    # request, or the teardown call can time out against a busy slot.
    time.sleep(1.5)

    # --- end state ----------------------------------------------------------
    ba.leave(args.leave_password or None, current=args.leave_current)

    print('\n' + '=' * 60)
    if FAILURES:
        print('FAILED %d check(s):' % len(FAILURES))
        for f in FAILURES:
            print('   -', f)
        return 1
    print('AUTH GUARDS: ALL CHECKS PASSED')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    finally:
        ba.close_console()
