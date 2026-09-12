"""Run every hardware check and print one verdict.

Four checks cover different layers and none of them subsumes the others:

  auth_guard_check   - auth surface: setup-mode routing, the /setup write gate,
                       session cookies, token gates (HTTP level, no browser)
  preconnect_probe   - socket level: is the HTTP slot released promptly?
  browser_check      - page level: does the real page wire up its own JS?
  mobile_login_check - phone level: do the real setup/login FORMS work?

They all need the board reachable over Wi-Fi (serial `wifi on` first) and COM3
free. Expect a few minutes: each check resets the board and waits for boot.

Every one of them finishes by CLEARING the board's web password, which leaves
the device in setup mode - so /login rejects everything until a password is set
again. This script therefore ends by stating the auth state it leaves behind.

Run: python tests/tools/check_all.py
"""

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
PY = sys.executable

CHECKS = [
    ('auth_guard_check.py', 'the auth guards hold'),
    ('preconnect_probe.py', 'HTTP slot is released promptly'),
    ('browser_check.py', 'the page works in a real browser'),
    ('mobile_login_check.py', 'the phone (form) flow works'),
]


def main():
    results = []
    for script, what in CHECKS:
        print('\n' + '#' * 64)
        print('# %s  (%s)' % (script, what))
        print('#' * 64)
        t0 = time.time()
        r = subprocess.run([PY, os.path.join(HERE, script)],
                           capture_output=True, text=True, errors='replace')
        out = (r.stdout or '') + (r.stderr or '')
        tail = [l for l in out.splitlines() if l.strip()]
        for line in tail[-6:]:
            print('   ', line[:150])
        results.append((script, r.returncode, time.time() - t0))

    print('\n' + '=' * 64)
    print('%-26s %-8s %s' % ('check', 'result', 'time'))
    for script, rc, secs in results:
        print('%-26s %-8s %.0f s' % (script, 'PASS' if rc == 0 else 'FAIL', secs))

    failed = [s for s, rc, _ in results if rc != 0]

    # Every check ends in setup mode. Say so here as well, so nobody (human or
    # agent) reads a green aggregate result and then wonders why the login page
    # rejects the password they set earlier.
    try:
        import board_auth
        print('\nboard auth state now: %s' % board_auth.state())
        if board_auth.state() == 'setup':
            print('  -> the stored password was cleared by these tests. Open '
                  'http://%s/ and\n     set one; /login is a dead end until then.'
                  % board_auth.IP)
    except Exception as e:  # noqa: BLE001
        print('\ncould not read the board auth state: %r' % (e,))

    if failed:
        print('\nFAILED: %s' % ', '.join(failed))
        return 1
    print('\nALL HARDWARE CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
