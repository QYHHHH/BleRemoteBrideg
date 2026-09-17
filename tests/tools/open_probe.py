"""open_probe.py - does a plain pyserial open reset the board?

rts_probe.py test A proved that opening with the lines *pre-set* to (0,0)
never resets. That test cannot distinguish "open is harmless" from "pre-setting
the lines is what made it harmless".

This one separates the two:
  A) open with (0,0) pre-set   -> proves the board is running (status answers,
                                  no boot banner)
  B) close, then reopen with nothing pre-set (pyserial's own defaults)
     -> a boot banner here means the open itself reset the board

close() does not release the control lines (measured), so the state carried
into step B is whatever step A left behind.

  python open_probe.py
"""

import time

import serial

PORT = "COM3"


def run(label, preset):
    s = serial.Serial()
    s.port = PORT
    s.baudrate = 115200
    s.timeout = 0.2
    if preset is not None:
        s.dtr, s.rts = preset
    s.open()

    buf = b""
    t0 = time.time()
    while time.time() - t0 < 1.2:
        buf += s.read(4096)

    s.reset_input_buffer()
    s.write(b"status\r\n")
    t0 = time.time()
    while time.time() - t0 < 2.0:
        buf += s.read(4096)

    text = buf.decode("utf-8", "replace")
    banner = "MiRemoteBridge console ready" in text
    answered = "firmware" in text or "rc003 state" in text
    print(f"{label:22s} open->dtr={int(s.dtr)},rts={int(s.rts)}  "
          f"boot_banner={str(banner):5s} answered={answered}")

    # Leave the lines in a known state before the next step.
    s.dtr = False
    s.rts = False
    time.sleep(0.4)
    s.close()
    return banner


def main():
    print("step A: lines pre-set to (0,0)")
    run("A  preset (0,0)", (False, False))

    print("\nstep B: nothing pre-set -> pyserial defaults")
    b = run("B  defaults", None)

    print("\n--- verdict ---")
    print("open resets the board" if b else "open does NOT reset the board")


if __name__ == "__main__":
    main()
