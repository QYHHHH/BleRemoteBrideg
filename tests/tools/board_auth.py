"""Talking to the board's web auth surface from a test.

Two jobs in one module:

  * HTTP helpers that read the auth state WITHOUT following redirects - the
    redirect chain is the state machine we want to observe, so a client that
    follows it hides exactly the thing under test.
  * the serial console, because an auth check has to start from a known state
    ("no password stored") and `pass clear` is the only way to get there.

And the rule this module exists to enforce: **a script that changes the board's
password must say so when it ends.** Every auth check finishes with a board in
SETUP mode, and in setup mode `/login` rejects every input with "密码错误" -
there is no stored password to compare against. That reads as "my password
stopped working" and it has already cost one confusing debugging round. So
`leave()` always prints a banner with the state it left behind.

The serial port is opened once and kept open: opening it pulses RTS, which
resets the board, so a per-call open would restart the device mid-test.
"""

import gzip
import re
import sys
import time
import urllib.error
import urllib.request

from cdp import board_ip

DEFAULT_PORT = 'COM3'
DEFAULT_BAUD = 115200

# The firmware's queryValue() does no percent-decoding, so a query string is
# taken literally. Keep helper passwords inside this alphabet and the value we
# send is byte-for-byte the value that gets stored.
SAFE_PW = re.compile(r'^[A-Za-z0-9._-]{4,}$')

IP = board_ip()

_SER = None


# --- serial console ----------------------------------------------------------

def ser(port=DEFAULT_PORT, baud=DEFAULT_BAUD):
    """Open the console port once and keep it (opening pulses RTS = reset)."""
    global _SER
    if _SER is None:
        sys.path.insert(0, r'C:\Users\<user>\.workbuddy\binaries\python\pylibs')
        import serial  # noqa: PLC0415
        _SER = serial.Serial(port, baud, timeout=0.3, dsrdtr=False, rtscts=False)
        _SER.dtr = False
        _SER.rts = False
        time.sleep(0.4)
    return _SER


def console(cmd, settle=1.2, port=DEFAULT_PORT):
    """Send one console command and return whatever it prints."""
    s = ser(port)
    s.reset_input_buffer()
    s.write(cmd)
    time.sleep(settle)
    return s.read(s.in_waiting or 1).decode('utf-8', 'replace')


def close_console():
    global _SER
    if _SER is not None:
        _SER.close()
        _SER = None


# --- HTTP --------------------------------------------------------------------

class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}), _NoRedirect())


def http(path, ip=None, cookie=None, timeout=8):
    """GET with no redirect following and no proxy (the dev box has a TUN proxy
    that hijacks 192.168.x.x). Returns (status, headers-dict, body-text).

    The body is decompressed when the board labels it gzip. The page assets are
    stored gzip-only in flash and always sent with `Content-Encoding: gzip`,
    whatever `Accept-Encoding` asked for (web_page_gz.h says so on purpose), so
    a caller that does not decode sees binary and every `in body` check fails
    for a reason that has nothing to do with what is being tested.
    """
    url = 'http://%s%s' % (ip or IP, path)
    headers = {'Accept-Encoding': 'identity'}
    if cookie:
        headers['Cookie'] = cookie
    req = urllib.request.Request(url, headers=headers)
    try:
        r = _OPENER.open(req, timeout=timeout)
        return r.status, dict(r.headers), _decode(dict(r.headers), r.read())
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), _decode(dict(e.headers), e.read())
    except Exception as e:  # noqa: BLE001
        return None, {}, 'CONNECT-FAILED %r' % (e,)


def _decode(headers, raw):
    if 'gzip' in (headers.get('Content-Encoding') or ''):
        try:
            raw = gzip.decompress(raw)
        except OSError:
            return '(gzip body failed to decompress, %d B)' % len(raw)
    return raw.decode('utf-8', 'replace')


def title(body):
    m = re.search(r'<title>(.*?)</title>', body or '', re.S)
    return m.group(1).strip() if m else '(no title)'


def session_of(headers):
    """Pull mrb_sess out of a Set-Cookie header, or None."""
    c = headers.get('Set-Cookie', '')
    m = re.search(r'mrb_sess=([^;]+)', c)
    return 'mrb_sess=%s' % m.group(1) if m else None


# --- auth state --------------------------------------------------------------

def state(ip=None):
    """'setup' when no password is stored, 'locked' when one is and we hold no
    session. Read from `/`, whose redirect target IS the state."""
    st, hd, _ = http('/', ip=ip)
    if st == 302 and hd.get('Location') == '/setup':
        return 'setup'
    if st == 302 and hd.get('Location') == '/login':
        return 'locked'
    return 'unknown(status=%s,location=%s)' % (st, hd.get('Location'))


def set_password(pw, ip=None, cookie=None):
    """GET /setup with both fields. Only works without a session while the
    board is passwordless - that asymmetry is the point of the guard."""
    if not SAFE_PW.match(pw):
        raise ValueError('helper passwords must match %s, got %r' % (SAFE_PW.pattern, pw))
    q = '/setup?password=%s&confirm=%s' % (pw, pw)
    return http(q, ip=ip, cookie=cookie)


def clear_password(ip=None, port=DEFAULT_PORT):
    console(b'pass clear\r', port=port)
    time.sleep(28)          # pass clear reboots the board
    return state(ip)


def leave(pw, ip=None, port=DEFAULT_PORT, current=None):
    """End the run in a state the caller chose, and say so loudly.

    pw given  -> the board's password is set to it.
    pw is None -> the board is cleared back to setup mode.
    Either way the caller must not have to guess what the tests left behind.

    Setting a password needs a session once one exists - that is the /setup gate
    - so pass the board's CURRENT password in `current` and log in first. This
    is the gate working as intended, not an obstacle to work around.
    """
    ip = ip or IP
    if pw:
        cookie = None
        if state(ip) == 'locked':
            if not current:
                return _banner(False, "a password is already stored, and no "
                                      "--leave-current was given to sign in with")
            st, hd, _ = http('/login?password=%s' % current, ip=ip)
            cookie = session_of(hd)
            if not cookie:
                return _banner(False, "could not sign in with %r to change the "
                                      "password (%s)" % (current, st))

        st, hd, body = set_password(pw, ip=ip, cookie=cookie)
        ok = (st == 302 and bool(session_of(hd)))
        detail = "password set to %r (log in at http://%s/ with it)" % (pw, ip)
        if not ok:
            detail = "FAILED to set %r: %s %s" % (pw, st, title(body))
        return _banner(ok, detail)

    clear_password(ip=ip, port=port)
    return _banner(state(ip) == 'setup',
                   'password CLEARED - the board is in SETUP mode. Open '
                   'http://%s/ and set a password; /login will reject\n'
                   '     everything until you do, which is not a bug.' % ip)


def _banner(ok, detail):
    bar = '!' * 68
    print('\n' + bar)
    print('!!  BOARD AUTH STATE CHANGED BY THIS TEST')
    print('!!  %s' % detail)
    print('!!  %s' % ('VERIFIED' if ok else 'NOT VERIFIED - check the board by hand'))
    print(bar + '\n')
    return ok
