"""line_probe.py - isolate DTR and RTS, one line at a time.

Earlier attempts reasoned about the two lines as a pair and got a self-
contradictory answer: (1,1) showed no [BUTTON] press (suggesting GPIO9 high)
yet the boot banner appeared in the same window (suggesting EN was low). Both
can be true if (1,1) holds EN low *and* GPIO9 low - i.e. the two lines act
independently after all.

This walks the four combinations in order and asks the firmware directly:
after each change, send `status` and see whether it answers. No answer = the
board is not running (EN low). A `[BUTTON ] key pressed` line = GPIO9 low.

Starts and ends at (0,0). No step holds GPIO9 low for more than ~1 s, well
under the 5 s factory-reset threshold.

  python line_probe.py
"""

import time

import serial

PORT = "COM3"


def main():
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.timeout = 0.2
    s.dtr = False
    s.rts = False
    s.open()

    buf = b""

    def drain(seconds):
        nonlocal buf
        t0 = time.time()
        while time.time() - t0 < seconds:
            buf += s.read(4096)

    def step(label, dtr, rts):
        nonlocal buf
        s.dtr = dtr
        s.rts = rts
        time.sleep(0.35)
        buf = b""
        s.write(b"status\r\n")
        drain(1.8)
        text = buf.decode("utf-8", "replace")
        answered = "firmware" in text or "rc003 state" in text
        pressed = "key pressed" in text
        booted = "MiRemoteBridge console ready" in text
        print(f"{label:14s} (dtr={int(dtr)}, rts={int(rts)})  "
              f"answered={str(answered):5s} boot_banner={str(booted):5s} "
              f"key_pressed={pressed}")

    try:
        drain(1.5)
        step("baseline", False, False)
        step("DTR only", True, False)
        step("DTR + RTS", True, True)
        step("RTS only", False, True)
        step("both off", False, False)
    finally:
        s.dtr = False
        s.rts = False
        time.sleep(0.3)
        buf = b""
        drain(2.0)
        tail = buf.decode("utf-8", "replace")
        s.close()
        print("\n--- console after the final release ---")
        print(tail[-700:] if tail.strip() else "(nothing)")


if __name__ == "__main__":
    main()
