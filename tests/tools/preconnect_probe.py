"""Regression test: a silent (preconnected) socket must not starve the HTTP slot.

Browser preconnect is a real, common traffic shape - mobile Safari opens TCP
connections ahead of time and often never sends anything on them. Such a socket
used to occupy the only HTTP exchange slot for the full 4 s progress timeout, so
two of them delayed a real request by ~19 s and three by ~29 s (measured against
a real board). On a phone that reads as "login does not work".

The fix gives a socket that has not yet sent any request byte a short grace
window (kAcceptGraceMs) instead of the full progress timeout. This test pins it
down: each case must answer in well under a second.

Also exercises the keep-alive path: a parked socket must release the slot too
(kKeepAliveIdleMs), so a second request on a new connection is never queued
behind an idle one.

Run: python tests/tools/preconnect_probe.py [ip]
"""

import socket
import sys
import time

IP = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.100'
BUDGET_S = 2.0          # generous: the fix should land near 0.1-0.5 s
FAILURES = []


def timed_get(path='/', timeout=40.0):
    """Return (seconds, status_line) for one GET on a fresh connection."""
    t0 = time.time()
    s = socket.create_connection((IP, 80), timeout=timeout)
    try:
        s.settimeout(timeout)
        s.sendall(('GET %s HTTP/1.1\r\nHost: %s\r\n'
                   'Connection: close\r\n\r\n' % (path, IP)).encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
        return time.time() - t0, buf.split(b'\r\n', 1)[0].decode('latin1')
    finally:
        s.close()


def silent_sockets(n):
    """Open n connections and send NOTHING - what a preconnect looks like."""
    return [socket.create_connection((IP, 80), timeout=5) for _ in range(n)]


def check(label, ok, detail=''):
    if not ok:
        FAILURES.append(label)
    print('    [%s] %s%s' % ('PASS' if ok else 'FAIL', label, (' - ' + detail) if detail else ''))


def main():
    print('board %s\n' % IP)

    print('[1] baseline: a GET on its own')
    base = []
    for _ in range(3):
        secs, status = timed_get()
        base.append(secs)
        print('    %.2f s   %s' % (secs, status))
    check('baseline GET answers quickly', max(base) < BUDGET_S,
          'slowest %.2f s' % max(base))

    print('\n[2] a GET queued behind N silent preconnections')
    for n in (1, 2, 3, 4):
        held = silent_sockets(n)
        try:
            secs, status = timed_get()
        finally:
            for s in held:
                s.close()
        print('    %d silent -> %.2f s   %s' % (n, secs, status))
        check('GET behind %d silent socket(s) is not starved' % n, secs < BUDGET_S,
              '%.2f s (budget %.1f s)' % (secs, BUDGET_S))
        time.sleep(0.6)

    print('\n' + '=' * 60)
    if FAILURES:
        print('FAILED %d check(s): %s' % (len(FAILURES), '; '.join(FAILURES)))
        return 1
    print('PRECONNECT: ALL CHECKS PASSED')
    return 0


if __name__ == '__main__':
    sys.exit(main())
