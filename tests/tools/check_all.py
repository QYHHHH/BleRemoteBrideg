"""Run every hardware check and print one verdict.

The remaining checks cover different layers and none of them subsumes the others:

  preconnect_probe - socket level: is the HTTP slot released promptly?
  browser_check    - page level: does the real page wire up its own JS?

Both need the board reachable over Wi-Fi (a short BOOT press, or `wifi on`,
opens the 30-minute window). preconnect_probe speaks HTTP only and leaves no
trace behind; browser_check drives a real browser and does not touch the
board's settings.

  The auth-guard scripts that used to live here (auth_guard_check,
  mobile_login_check) were removed together with the web console password in
  2026-09-16. See docs/AUTH.md.

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
    ('preconnect_probe.py', 'HTTP slot is released promptly'),
    ('browser_check.py', 'the page works in a real browser'),
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
    if failed:
        print('\nFAILED: %s' % ', '.join(failed))
        return 1
    print('\nALL HARDWARE CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
