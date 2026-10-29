# rf-868-lab

Notes, firmware and tools from reverse-engineering two closed 868 MHz home devices with one **ESP32-S3 + CC1101** dongle:

- **Somfy io-homecontrol** blinds that keep losing their TaHoma hub (`docs/io-homecontrol.md`)
- **Duco** ventilation box that looks like an **Itho** platform (`docs/duco.md`)

Status: **proof of concept (v0)**. Everything is versioned and nothing is final; each doc says what is confirmed and what is a hypothesis.

## What works today

- A **sniffer** that decodes io-homecontrol frames on a CC1101 (async transparent mode, software framing): 50+ frames with valid CRC-16/KERMIT.
- Frame format, addresses and the 2-way challenge flow, with measured timings and RSSI.
- A **safe transmitter** (address whitelist, rate limit, low power) that proved TX and RX work: it sent a replayed command and the actuator answered with its challenge.
- A verbatim **repeater** prototype (end-to-end authentication means a verbatim repeat needs no key).
- A method and a list of pitfalls (`docs/method.md`).

## What does not (yet)

- We **cannot and do not try** to move an actuator without its key. 2W authentication is untouched.
- The repeater did not get a real-world test: the stubborn blind stopped answering anywhere (see the doc).
- The Duco box is desk research: nothing has been connected to it yet.

## Layout

| Path | What |
|---|---|
| `firmware/io_sniffer2c` | listen-only sniffer, hops over the 3 channels |
| `firmware/io_relay1` | sniffer + repeater + `t` command (one safe test transmission) |
| `tools/` | `capture.py` (log serial), `tx_test.py` (one test transmission) |
| `hardware/` | pins and CC1101 registers |
| `docs/` | protocol notes, method, Duco, example capture |

Code comments are in Portuguese for now; the docs are in English. Translations welcome.

## Quick start

```
arduino-cli compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc firmware/io_sniffer2c
arduino-cli upload  --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc --port /dev/ttyACM0 firmware/io_sniffer2c
python3 tools/capture.py /dev/ttyACM0 120 run1.log      # then use your remote/app and read run1.log
```

Serial commands (115200): `h` hop, `c1`/`c2`/`c3` fix channel, `v` stats; relay build also `r0`/`r1`, `pXX` (power) and `t@PRE:HEX`.

## Safety and legal

Radio: only transmit what you are allowed to. The ETSI limits for these sub-bands are 25 mW ERP with duty-cycle limits; check your country.
Mains: the Duco box and similar units have mains voltage and a mains-referenced ground inside. Unplug first; leave 230 V wiring to an electrician.
Use this on **your own** devices only. Do not publish device addresses or keys from your home.

## Help wanted

Captures from other Somfy io / Itho / Duco models, a CC1101 or SX1276 comparison, and anyone who has seen a solar io motor stop answering 2W while still obeying the remote.
Open an issue and say what you measured. I was helped a lot by other people's repos; this is my way of giving back.

License: MIT.
