# Method: how we investigate a closed 868 MHz device

1. **Listen first.** Flash `firmware/io_sniffer2c`. It hops between the three channels (100 ms each) and prints one line per frame
   (`Q` = decoded frame with CRC check, `A` = activity seen but not decoded). Log with `tools/capture.py`.
2. **Make a guide before testing.** Write the exact sequence (remote first, then app, wait N seconds) and run the *same* sequence each time.
   We lost time testing "at random"; a fixed sequence makes runs comparable.
3. **Find the addresses and the roles** from a handful of frames (who is dst, who is src, who answers whom and how fast).
4. **Measure, don't guess**: RSSI per device, reply delay, preamble length, how many commands got an answer (x of y).
5. **Only then transmit**, on the bench, with low power, to a device that works, and with the rate limit on.

## Safety rules we built into the transmitter

- Destination whitelist compiled into the firmware (two addresses). Anything else is refused.
- Token bucket: one transmission per 12 s (burst of 4).
- Default power 0x60 (~0 dBm); the ceiling is 0xC0 (~+10 dBm). Always go back to low power after a test.
- Kill switch: `r0` turns the repeater off.
- The 2W authentication is not attacked. Without the key nothing moves.

## Pitfalls that cost us time

- The line buffer of the serial command parser was 16 characters: a long `t...` command arrived truncated and was (correctly) refused.
  Check your buffer before blaming the radio.
- Do **not** use `pkill -f` / `pgrep -f <name>` from a script whose own command line contains `<name>`: it matches itself (killed our SSH session once, hung a wait loop once).
- Preamble length: alternating `0x55` runs also occur *inside* the sync; measure the last *long* alternating run (>= 8 bits) or you always read "2".
- Transmitting by bit-banging for 260 ms with interrupts disabled can trip the ESP32 interrupt watchdog: keep interrupts enabled for the long preamble and disable them only for the last ~400 bits.
- With the dongle next to the hub the sniffer sees the hub at -27 dBm but a distant actuator at -80 dBm; hold the channel after a frame of interest (`holdUntil`) so the 12 ms reply is not missed.
- Same sketch name must match the folder name for `arduino-cli`.

## Reporting

Please publish negative results too (what did **not** work and under which conditions). Keep device addresses and keys out of issues.
