#!/usr/bin/env python3
"""Send ONE frame with the relay firmware's `t` command and print what comes back.

usage: tx_test.py /dev/ttyACM0 <c1|c2|c3> <preamble_bytes> <frame_hex_with_crc> [PA_hex]

The firmware only accepts frames addressed to the two destinations compiled into it, limits the rate
(1 per 12 s) and defaults to low power (PA 0x60). Without the device's key the actuator only answers
with its 3C challenge: nothing moves. Read docs/method.md before using.
"""
import datetime
import sys
import time

import serial


def log(line):
    print(datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3], line, flush=True)


def listen(s, secs):
    t = time.time()
    while time.time() - t < secs:
        line = s.readline().decode(errors="replace").strip()
        if line:
            log(line)


port, chan, pre, frame = sys.argv[1:5]
pa = sys.argv[5] if len(sys.argv) > 5 else "60"
s = serial.Serial(port, 115200, timeout=0.2)
time.sleep(1.5)
for c in ("r0", chan, "p" + pa):                  # relay off, fix channel, set power
    s.write((c + "\n").encode()); listen(s, 0.6)
log("--- transmitting once")
s.write(("t@%s:%s\n" % (pre, frame)).encode())
listen(s, 6)
s.write(b"p60\n"); listen(s, 0.6)                 # always go back to low power
