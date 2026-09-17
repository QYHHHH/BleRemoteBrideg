"""press_probe.py - regression test for the removed Improv-session guard.

Drives the control lines to (1,0), which pulls GPIO9 low and is electrically
identical to holding the BOOT key (measured 2026-09-17; truth table in
docs/PITFALLS.md). Held ~1.2 s that is a *short press*, so the firmware should
open the 30-minute Wi-Fi window.

The interesting step is the second one: it sends a valid Improv RPC identify
frame *before* pressing. The old firmware used any received frame to arm a
five-minute "session" that made reset_button swallow every press - that is the
bug that ate five consecutive presses while the config page was open. The new
firmware must ignore it entirely, so both presses must produce the same two
log lines.

Safety: GPIO9 is never held low longer than ~1.2 s, four times under the 5 s
factory-reset threshold, and the finally block always returns the lines to
(0,0). A leftover (1,0) would look like a stuck key and wipe the board.

  python tests/tools/press_probe.py
"""

import sys
import time

import serial

PORT = "COM3"
PRESS_S = 1.2       # well under kShortPressMaxMs (3 s)
HARD_CAP_S = 3.5    # warns if a hold drifts toward kFactoryHoldMs (5 s)


def improv_identify_frame():
    """A valid client->device frame: magic, version, RPC type, len, cmd, data, sum."""
    body = bytes([
        0x01,        # protocol version
        0x03,        # kPktRpcCommand (client -> device)
        0x02,        # data length: command byte + payload length byte
        0x02,        # kCmdIdentify - "report your state"
        0x00,        # zero bytes of payload
    ])
    frame = b"IMPROV" + body
    return frame + bytes([sum(frame) & 0xFF])


def main():
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False           # preset (0,0) before open, so opening does not reset
    s.rts = False
    s.open()

    def drain(seconds):
        buf = b""
        t0 = time.time()
        while time.time() - t0 < seconds:
            buf += s.read(4096)
        return buf

    def press(seconds=PRESS_S):
        """(1,0) for `seconds`, then back to (0,0). Returns how long GPIO9 was low."""
        t0 = time.time()
        s.dtr = True
        try:
            time.sleep(seconds)
        finally:
            s.dtr = False
        held = time.time() - t0
        if held > HARD_CAP_S:
            print("  !! held %.2f s - too close to the 5 s factory reset" % held)
        return held

    def report(label, held, text):
        pressed = "key pressed" in text
        # The window shows up under three different lines depending on the path:
        #   already enabled, triggerRejoin() -> "BOOT short press: window refreshed"
        #   already enabled, enable()        -> "config UI window refreshed"
        #   had to bring Wi-Fi up            -> "config UI ready at <ip> ..."
        refreshed = ("BOOT short press: window refreshed" in text
                     or "config UI window refreshed" in text)
        opened = "config UI ready at" in text
        swallowed = "short press ignored" in text
        print("  %-34s held %.2f s  key_pressed=%-5s window=%s swallowed=%s"
              % (label, held, pressed,
                 "refreshed" if refreshed else "opened" if opened else "NO",
                 swallowed))
        for line in text.splitlines():
            if "[WIFI" in line:
                print("      " + line.strip())
        return pressed, (opened or refreshed), swallowed

    ok = True
    try:
        drain(1.2)

        print("press 1 - baseline, window off, no Improv traffic:")
        held = press()
        r1 = report("baseline", held, drain(2.5).decode("utf-8", "replace"))

        print("\npress 2 - window already open, after a valid Improv identify frame")
        print("          (this is the real-world case: config page connected):")
        s.write(improv_identify_frame())
        s.flush()
        pre = drain(1.5)
        print("  device answered the frame with %d byte(s)" % len(pre))
        held = press()
        r2 = report("after Improv frame", held, drain(2.5).decode("utf-8", "replace"))

        print("\npress 3 - window closed again, then a valid Improv frame:")
        print("          (stale session, no page open - the old firmware's worst case):")
        s.write(b"wifi off\r\n")
        s.flush()
        drain(1.5)
        s.write(improv_identify_frame())
        s.flush()
        drain(1.5)
        held = press()
        r3 = report("stale session, window off", held, drain(2.5).decode("utf-8", "replace"))

        for name, (pressed, window, swallowed) in (
                ("press 1", r1), ("press 2", r2), ("press 3", r3)):
            if swallowed:
                print("\nFAIL %s: firmware still swallowed the press" % name)
                ok = False
            elif not pressed:
                print("\nFAIL %s: no key press detected (did GPIO9 move?)" % name)
                ok = False
            elif not window:
                print("\nFAIL %s: pressed but the Wi-Fi window did not open" % name)
                ok = False

        # The long-press path, stopped well short of the wipe: 2.0 s is three
        # seconds under kFactoryHoldMs, so this only proves the countdown works.
        # Actually reaching 5 s would erase every bond, so it is not automated.
        print("\nhold 4 - 2.0 s, to see the factory-reset countdown (stops 3 s short):")
        held = press(2.0)
        text = drain(1.5).decode("utf-8", "replace")
        countdown = "keep holding" in text
        print("  %-34s held %.2f s  countdown_logged=%s" % ("long-ish hold", held, countdown))
        for line in text.splitlines():
            if "[BUTTON" in line:
                print("      " + line.strip())
        if not countdown:
            print("\nFAIL hold 4: no countdown line - the user could not see it coming")
            ok = False

        print("\n" + ("PASS - every press fired; Improv traffic is ignored"
                      if ok else "FAIL - see above"))
    finally:
        s.dtr = False
        s.rts = False
        time.sleep(0.3)
        s.close()

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
