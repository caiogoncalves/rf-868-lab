#!/usr/bin/env python3
"""Log the dongle's serial output with timestamps.

usage: capture.py /dev/ttyACM0 300 out.log [cmd ...]
  e.g. capture.py /dev/ttyACM0 300 run1.log r0 h     (relay off, hop channels)
"""
import datetime
import sys
import time

import serial

port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
s = serial.Serial(port, 115200, timeout=0.3)
time.sleep(1.5)                                  # the board resets when the port opens
for c in sys.argv[4:]:
    s.write((c + "\n").encode()); time.sleep(0.4)
t0 = time.time()
with open(out, "w") as f:
    while time.time() - t0 < secs:
        line = s.readline().decode(errors="replace").strip()
        if line:
            f.write("%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3], line)); f.flush()
