# Somfy io-homecontrol on a CC1101

Status: **proof of concept (v0)**. What is below was measured with the firmware in this repo; "hypothesis" means not proven.

## Radio

- 868.25 / 868.95 / 869.85 MHz, 2-FSK, 38.4 kbaud, deviation ~19 kHz.
- Bytes are UART-framed on the air (start 0, 8 data bits LSB first, stop 1).
- Frame on the air: preamble of `0x55` bytes, sync `0xFF 0x33`, frame, then silence.
- Preamble length differs by sender: commands from the hub to a sleeping (solar) actuator use a long one (~9800 bits,
  ~255 ms, measured); replies from the actuator use ~100 bits.

## Frame (confirmed: 50+ frames with valid CRC)

```
b0 b1 | dst[3] | src[3] | cmd | params... | crc_lo crc_hi
b0 & 0x1F = N    total length = N + 3 bytes
CRC-16/KERMIT (reflected 0x8408, init 0x0000) over everything before the CRC, sent low byte first
```

Verified example: bytes `4E 20 <dst3> <src3> 00 01 E7 00 00 00 00` give CRC `0x08A4` on the air as `A4 08`.
See `docs/captures/example-2w-exchange.txt`.

## Two-way (2W) exchange

`cmd 00` (command) -> `cmd 3C` (actuator's challenge) -> `cmd 3D` (hub's authenticated answer) -> `cmd 04` (status).
The challenge answer is keyed, so **you cannot make an actuator move without the key**, and this repo does not try.
Measured on a working actuator: the `3C` reply comes ~12 ms after the command, on the same channel, every time (7/7).
If the hub gets no answer it repeats the command about 5 times, ~0.37 s apart.

## What we learned from one stubborn actuator

A solar blind motor that the hub lost "all the time":

1. The actuator *did* hear the hub and answered with `3C` (RSSI at the dongle: -79..-81 dBm). A second, healthy actuator answered at -74..-75 dBm.
   The hub never sent the `3D`. Hypothesis then: the uplink (actuator -> hub) was marginal.
2. We built a verbatim **repeater** (`firmware/io_relay1`): it repeats CRC-valid frames from the actuator to the hub. Because authentication is end to end,
   repeating a frame verbatim needs no key (added latency ~9-10 ms).
3. Later the actuator stopped answering the hub **at all**, even with the dongle a few centimetres from the hub (6 commands, 0 answers; earlier 7/7).
   Replacing the repeater firmware with the plain sniffer gave the same result, so it is not our firmware.
4. A direct command from the dongle at +10 dBm with the long wake-up preamble also got no `3C`, while the **wall remote still moved the blind**.
   The remote uses 1-way (1W) frames that need no answer.

So: the motor is alive and hears 1W frames, but at that moment it did not answer 2W challenges from anyone. Open hypotheses
(not tested): the solar motor sleeps / protects its battery and only answers 2W for a while after being woken; a pairing/key mismatch;
a marginal radio on the motor. If you have seen this, please open an issue.

## Safe bench test of the transmitter

`tools/tx_test.py` sends **one** replayed command to a *working* actuator. The actuator answers with a fresh `3C` challenge and does not move
(we got `3C` 9 ms after the end of our transmission, CRC valid, RSSI -69 dBm). This proves TX and RX work end to end.

## Official alternatives

The Somfy Repeater Box io is documented as not working with solar motors (retail sources, not the manual). A second hub can control an
actuator only after the io key is copied to it. The open-source projects `rspaargaren/iohomecontrol` and `Velocet/iown-homecontrol`
are SX1276-based and mostly 1W-focused; the Velocet docs were our main radio reference.
