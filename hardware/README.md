# Hardware

Everything here runs on one cheap dongle: an **ESP32-S3** wired to a **CC1101** 868 MHz transceiver module
(a "RAMSES-ESP"-style USB dongle works; any ESP32-S3 + CC1101 board does).

| Signal | ESP32-S3 pin |
|---|---|
| MOSI | 35 |
| SCK | 36 |
| MISO | 37 |
| CSN | 38 |
| GDO0 | 39 (raw data in/out) |

Build: `arduino-cli`, core `esp32:esp32` 3.3.x, FQBN `esp32:esp32:esp32s3:CDCOnBoot=cdc`.

## CC1101 settings used (io-homecontrol, 38.4 kbaud 2-FSK)

| Register | Value | Meaning |
|---|---|---|
| IOCFG0 | 0x0D | GDO0 = serial data (asynchronous, transparent) |
| PKTCTRL0 | 0x32 | asynchronous serial mode, no packet engine |
| MDMCFG4 / MDMCFG3 | 0xCA / 0x83 | ~38.4 kbaud, ~100 kHz channel filter |
| MDMCFG2 | 0x00 | 2-FSK, no sync word (we find the sync in software) |
| DEVIATN | 0x34 | ~19 kHz deviation |
| MCSM0 / FSCTRL1 | 0x18 / 0x06 | auto-calibrate when leaving IDLE |

AGC, FSCAL and TEST registers follow the SmartRF Studio export. Frequencies: 868.25, 868.95 and 869.85 MHz.

## Why software framing

The SX1276 has a hardware "IoHome" mode; the CC1101 does not. On the air every byte is UART-framed (start bit 0,
8 data bits LSB first, stop bit 1) and frames start with a `0x55` preamble and the sync `0xFF 0x33`. The CC1101 is
therefore used in *asynchronous transparent mode*: we sample GDO0 on every edge in an interrupt and decode the
bit stream in software; to transmit we bit-bang GDO0 at 38 400 baud timed with the CPU cycle counter.
The open io-homecontrol projects we looked at (`rspaargaren/iohomecontrol`, `cridp`, `Velocet/iown-homecontrol`) target the SX1276;
we did not find a CC1101 one. If you know one, please open an issue.

## Power and duty cycle

PATABLE 0x60 is about 0 dBm, 0xC0 is the CC1101 maximum at 868 MHz (about +10 dBm). Both are below the 25 mW ERP limit of the
ETSI sub-bands used, but check your own country's rules and the duty cycle (868.0-868.6 MHz: 1 %; 868.7-869.2 MHz: 0.1 %).
