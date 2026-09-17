"""Ask the board, over serial, whether the Improv Wi-Fi serial dialect still
answers - specifically *after* the station is up and the reported state should
be PROVISIONED.

The question this answers: "does connecting Wi-Fi switch the serial protocol
off?". The firmware's own state machine (improv_serial.cpp loop()) reports
PROVISIONED whenever the station is online, and esp-web-tools shows a
"Change Wi-Fi" entry for a PROVISIONED device - so if the browser shows no
Wi-Fi entry at all, the handshake must be failing, not the state.

Usage:
    python improv_probe.py COM3            # wifi off, then wifi on, identify both times
    python improv_probe.py COM3 --no-wifi  # only probe with the radio as it is

Careful: opening the port resets the board (RTS -> EN), which is why the radio
is brought up *after* the port is open and never before.
"""

import sys
import time

import serial

MAGIC = b"IMPROV"
PKT_CURRENT_STATE = 0x01
PKT_ERROR_STATE = 0x02
PKT_RPC_RESULT = 0x04
CMD_IDENTIFY = 0x02

STATE_NAMES = {0x02: "AUTHORIZED", 0x03: "PROVISIONING", 0x04: "PROVISIONED"}


def build_identify():
    body = bytes([CMD_IDENTIFY, 0x00])
    payload = MAGIC + bytes([0x01, 0x03, len(body)]) + body
    return payload + bytes([sum(payload) & 0xFF])


def decode(buf, out):
    """Pull every well-formed packet out of buf and return the unconsumed tail.

    The tail has to come back to the caller: passing a list in and mutating it
    is fine, but a bytes object is immutable, so an in-place rebind inside this
    function would leave the caller re-scanning the same bytes on every read
    and printing each packet once per later chunk.
    """
    while True:
        start = buf.find(MAGIC)
        if start < 0:
            return b""
        if len(buf) < start + 9:
            return buf[start:]
        pkt = buf[start:]
        length = pkt[8]
        if len(pkt) < 9 + length + 1:
            return pkt
        body = pkt[9:9 + length]
        checksum = pkt[9 + length]
        if (sum(pkt[:9 + length]) & 0xFF) != checksum:
            out.append("  <bad checksum, dropping one byte and rescanning>")
            buf = pkt[1:]
            continue
        kind = pkt[7]
        if kind == PKT_CURRENT_STATE:
            state = body[0] if body else -1
            out.append("  <- CURRENT_STATE 0x%02X (%s)" % (state, STATE_NAMES.get(state, "?")))
        elif kind == PKT_ERROR_STATE:
            out.append("  <- ERROR_STATE 0x%02X" % (body[0] if body else -1))
        elif kind == PKT_RPC_RESULT:
            text = body[2:].decode("utf-8", "replace") if len(body) > 2 else ""
            out.append("  <- RPC_RESULT cmd=0x%02X %r" % (body[0] if body else -1, text))
        else:
            out.append("  <- packet type 0x%02X len %u" % (kind, length))
        buf = pkt[9 + length + 1:]


def pump(s, seconds, lines, buf):
    end = time.time() + seconds
    while time.time() < end:
        chunk = s.read(256)
        if not chunk:
            continue
        buf += chunk
        buf = decode(buf, lines)
        # A console line mentioning IMPROV would otherwise be held forever
        # waiting for a length byte that never comes.
        if len(buf) > 4096:
            buf = buf[-512:]
    return buf


def rpc(cmd, data=b""):
    body = bytes([cmd, len(data)]) + data
    payload = MAGIC + bytes([0x01, 0x03, len(body)]) + body
    return payload + bytes([sum(payload) & 0xFF])


def identify(s, label, lines):
    """Mimic what the browser client does, command by command.

    initialize() sends REQUEST_CURRENT_STATE (0x02), waits for the state packet
    and - when the device reports PROVISIONED - for the RPC result that carries
    nextUrl. It then sends REQUEST_DEVICE_INFO (0x03). GET_NETWORK_STATE (0x07)
    is optional but esp-web-tools shows the device URL from it.
    """
    print("--- %s ---" % label)
    for cmd, name in ((0x02, "REQUEST_CURRENT_STATE"), (0x03, "REQUEST_DEVICE_INFO"),
                      (0x07, "GET_NETWORK_STATE")):
        before = len(lines)
        buf = b""
        s.write(rpc(cmd))
        s.flush()
        buf = pump(s, 1.5, lines, buf)
        got = lines[before:]
        print("  0x%02X %s -> %s" % (cmd, name, " / ".join(x.strip() for x in got) if got else "NO REPLY"))
    lines.clear()


def main():
    port = sys.argv[1]
    do_wifi = "--no-wifi" not in sys.argv

    s = serial.Serial(port, 115200, timeout=0.2, dsrdtr=False, rtscts=False)
    try:
        s.dtr = False
        s.rts = False
    except Exception:
        pass
    time.sleep(0.3)
    s.reset_input_buffer()

    lines = []
    buf = pump(s, 3.0, lines, b"")   # boot banner
    lines.clear()

    identify(s, "radio off", lines)

    if do_wifi:
        s.write(b"wifi on\n")
        s.flush()
        deadline = time.time() + 20
        seen = ""
        while time.time() < deadline and "config UI ready at" not in seen:
            chunk = s.read(256)
            if chunk:
                seen += chunk.decode("utf-8", "replace")
        for row in seen.splitlines():
            if "WIFI" in row:
                print(row.strip())
        if "config UI ready at" not in seen:
            print("!! the station never came up; probing anyway")
        else:
            print("station is up")
        identify(s, "station up", lines)

    s.close()


if __name__ == "__main__":
    main()
