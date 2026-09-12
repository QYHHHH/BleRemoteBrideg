"""Minimal Chrome DevTools Protocol client - no Playwright, no Selenium, no Node.

Drives the Chrome already installed on this machine over its debugging port and
speaks CDP over a hand-rolled websocket client. Enough for navigate / evaluate /
screenshot / user-agent emulation, which is all the checks here need.

Why hand-rolled: the checks must run on this machine with nothing installed
beyond Chrome and Python. Pulling in a browser-automation stack for four methods
is not a trade worth making, and the wire format is small.

Import from a check script:

    from cdp import Cdp, launch_chrome, find_page, local_json
"""

import base64
import json
import os
import re
import socket
import struct
import subprocess
import time
import urllib.request

CHROME = r'C:\Program Files\Google\Chrome\Application\chrome.exe'
DEFAULT_PORT = 9333


def launch_chrome(profile_dir, port=None, extra=None, headless=True):
    """Start Chrome with a debugging port. Returns (Popen handle, port actually used).

    Two things this has to get right, both learned the hard way:

    * A leftover Chrome from an earlier run keeps an exclusive hold on the debug
      port, and also keeps the profile directory's singleton lock. A new instance
      then fails to bind IPv4 (WSAEACCES, 0x271D) and quietly falls back to
      IPv6-only, while a new instance on the same profile dir hands off to the
      stuck one and exits. Symptom: "Chrome never exposed a page target".
      So: a fresh profile directory per run, and a port picked now rather than
      reused from a constant.
    * --no-proxy-server matters: a dev machine with a TUN/transparent proxy will
      otherwise hijack requests to private addresses and produce failures that
      look like a broken device.
    """
    if port is None:
        port = free_port()
    args = [CHROME]
    if headless:
        args += ['--headless=new', '--disable-gpu']
    args += [
        '--no-proxy-server', '--no-first-run',
        '--remote-debugging-port=%d' % port,
        '--user-data-dir=' + profile_dir,
        '--window-size=1280,900',
    ]
    args += (extra or [])
    args.append('about:blank')
    proc = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return proc, port


def free_port():
    """Ask the OS for a free TCP port, then release it for Chrome to take."""
    s = socket.socket()
    try:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]
    finally:
        s.close()


def fresh_profile(tag):
    """A per-run profile directory. Reusing one lets a stuck Chrome from a
    previous run lock us out of it."""
    d = r'C:\code\MiRemoteBridge\outputs\chrome-%s-%d' % (tag, os.getpid())
    os.makedirs(d, exist_ok=True)
    return d


def local_json(path, port):
    """GET a /json/... endpoint on the debugging port, bypassing any proxy.

    Chrome may end up listening on IPv6 loopback only (if IPv4 bind is refused),
    so try both families rather than assuming 127.0.0.1.
    """
    op = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    last = None
    for host in ('127.0.0.1', '[::1]'):
        try:
            with op.open('http://%s:%d%s' % (host, port, path), timeout=3) as r:
                return json.loads(r.read().decode())
        except Exception as e:
            last = e
    raise last


def find_page(port, tries=40):
    """Wait for a page target and return its webSocketDebuggerUrl."""
    for _ in range(tries):
        try:
            for t in local_json('/json/list', port):
                if t.get('type') == 'page' and t.get('webSocketDebuggerUrl'):
                    return t['webSocketDebuggerUrl']
        except Exception:
            pass
        time.sleep(0.5)
    return None


def board_ip(default=None):
    """The board is on DHCP, so its address moves. `wifi on` records the real one
    in build/.boardip; prefer that.

    No address is guessed when neither that file nor MRB_IP is available: a
    stale address here looks exactly like a broken device and wastes a lot of
    time, and one machine's LAN address has no business being in the repo.
    """
    if os.environ.get('MRB_IP'):
        return os.environ['MRB_IP']
    tools = os.path.dirname(os.path.abspath(__file__))          # <repo>/tests/tools
    cached = os.path.join(os.path.dirname(os.path.dirname(tools)), 'build', '.boardip')
    try:
        with open(cached) as f:
            found = re.search(r'(\d+\.\d+\.\d+\.\d+)', f.read())
        if found:
            return found.group(1)
    except OSError:
        pass
    if default:
        return default
    raise SystemExit("no board address: run the step that writes build/.boardip "
                     "(wifi on prints the URL) or set MRB_IP=192.168.x.x")


class Cdp:
    """CDP over a websocket. call() is synchronous; events collect in .events."""

    def __init__(self, url):
        m = re.match(r'ws://([^/:]+):(\d+)(/.*)', url)
        host, port, path = m.group(1), int(m.group(2)), m.group(3)
        self.s = socket.create_connection((host, port), timeout=10)
        key = base64.b64encode(os.urandom(16)).decode()
        req = ('GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\n'
               'Connection: Upgrade\r\nSec-WebSocket-Key: %s\r\n'
               'Sec-WebSocket-Version: 13\r\n\r\n' % (path, host, port, key))
        self.s.sendall(req.encode())
        buf = b''
        while b'\r\n\r\n' not in buf:
            buf += self.s.recv(4096)
        self.buf = buf.split(b'\r\n\r\n', 1)[1]
        self.next_id = 1
        self.events = []

    def _send(self, obj):
        data = json.dumps(obj).encode()
        mask = os.urandom(4)
        n = len(data)
        hdr = bytes([0x81])
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack('>H', n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack('>Q', n)
        self.s.sendall(hdr + mask + bytes(data[i] ^ mask[i & 3] for i in range(n)))

    def _frames(self):
        out = []
        while len(self.buf) >= 2:
            op = self.buf[0] & 0x0F
            ln = self.buf[1] & 0x7F
            off = 2
            if ln == 126:
                if len(self.buf) < 4:
                    break
                ln = struct.unpack('>H', self.buf[2:4])[0]
                off = 4
            elif ln == 127:
                if len(self.buf) < 10:
                    break
                ln = struct.unpack('>Q', self.buf[2:10])[0]
                off = 10
            if self.buf[1] & 0x80:
                off += 4
            if len(self.buf) < off + ln:
                break
            out.append((op, self.buf[off:off + ln]))
            self.buf = self.buf[off + ln:]
        return out

    def call(self, method, params=None, timeout=15):
        mid = self.next_id
        self.next_id += 1
        self._send({'id': mid, 'method': method, 'params': params or {}})
        deadline = time.time() + timeout
        while time.time() < deadline:
            self.s.settimeout(max(0.2, deadline - time.time()))
            try:
                chunk = self.s.recv(65536)
            except socket.timeout:
                continue
            if not chunk:
                raise EOFError('cdp closed')
            self.buf += chunk
            for op, payload in self._frames():
                if op != 0x1:
                    continue
                msg = json.loads(payload.decode('utf-8', 'replace'))
                if msg.get('id') == mid:
                    if 'error' in msg:
                        raise RuntimeError(msg['error'])
                    return msg.get('result', {})
                self.events.append(msg)
                deadline = time.time() + max(0.1, deadline - time.time())
        raise TimeoutError(method)

    def evaluate(self, expression):
        """Evaluate and decode a JSON-ish result. Returns None if it threw."""
        r = self.call('Runtime.evaluate', {'expression': expression, 'returnByValue': True})
        if 'exceptionDetails' in r:
            return None
        return r.get('result', {}).get('value')

    def drain(self, seconds):
        """Keep reading so late events land in self.events."""
        end = time.time() + seconds
        while time.time() < end:
            self.s.settimeout(max(0.1, end - time.time()))
            try:
                chunk = self.s.recv(65536)
            except socket.timeout:
                break
            except OSError:
                break
            if not chunk:
                break
            self.buf += chunk
            for op, payload in self._frames():
                if op == 0x1:
                    self.events.append(json.loads(payload.decode('utf-8', 'replace')))

    def enable_all(self):
        for domain in ('Page', 'Runtime', 'Network', 'Log'):
            self.call(domain + '.enable')

    def cookies(self, url=None):
        r = self.call('Network.getCookies', {'urls': [url]} if url else {})
        return r.get('cookies', [])


IPHONE_UA = ('Mozilla/5.0 (iPhone; CPU iPhone OS 17_5 like Mac OS X) '
             'AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.5 '
             'Mobile/15E148 Safari/604.1')

ANDROID_UA = ('Mozilla/5.0 (Linux; Android 14; Pixel 8) AppleWebKit/537.36 '
              '(KHTML, like Gecko) Chrome/126.0.0.0 Mobile Safari/537.36')


def emulate_mobile(cdp, ua=IPHONE_UA, width=390, height=844, dpr=3):
    """Make the browser look and behave like a phone: UA, viewport, touch."""
    cdp.call('Emulation.setUserAgentOverride', {'userAgent': ua})
    cdp.call('Emulation.setDeviceMetricsOverride', {
        'width': width, 'height': height, 'deviceScaleFactor': dpr, 'mobile': True,
    })
    cdp.call('Emulation.setTouchEmulationEnabled', {'enabled': True, 'maxTouchPoints': 5})
    cdp.call('Emulation.setEmitTouchEventsForMouse', {'enabled': True, 'configuration': 'mobile'})
