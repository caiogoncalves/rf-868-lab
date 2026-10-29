# DucoBox Basic 325 RF (ventilation unit)

Status: **idea / paused**, mostly desk research. Every claim is marked *confirmed* (we measured or the maker/seller states it) or *hypothesis*.

**WARNING:** these boxes have mains voltage inside and the ground of the electronics is referenced to the mains. Unplug the unit before opening it,
never connect USB to the board while it is powered, and leave 230 V wiring to a qualified electrician.

## The unit looks like an Itho CVE-S ECO

Hypothesis (several independent hints, not proven): the DucoBox Basic 325 RF is the Itho CVE-S ECO platform sold under another name.

- Same specs (325 m3/h, 150 Pa, 125 mm, 4 inputs, Perilex, two potentiometers) and a very similar board layout (2x4 pin header next to four resistors,
  a coil, a white 6-way connector, crystal and radio chip on the left).
- Same pairing procedure (two buttons diagonally within 2 minutes of power-up; four buttons to unpair).
- RAMSES-II traffic (Itho/Orcon dialect: CO2 `1298`, demand `31E0`, fan state `31D9`) is present on 868 MHz around the house, and one frame identifies a sensor as `VMS-12C39`. We could not prove it comes from the Duco system: a test that switched our unit off did not stop any of the fans we heard, so those are the neighbours'.
- The Itho add-on community already sells a Wi-Fi add-on for the 2x4 header. The seller lists **"Duco 325/425RF"** among the models that work
  (*confirmed by the seller's listing*, not tested by us). The add-on only works if the board has the 2x4 interface; ours does.

## What the 2x4 header probably is (hypothesis, from the ithowifi project)

Seen with the potentiometers at the top: row 1 = SCL (left) and SDA (right); row 2 right = ground; row 4 left = Vin (about 16 V DC).
Do **not** connect 3.3 V logic straight to it. Check with a multimeter, with the unit unplugged, before connecting anything.

## Control options from the maker's documents (confirmed)

The Duco manuals describe wired control through a plug-in "PWM print" plus an external IQ unit (2-wire, 10 V/PWM), or an internal IQ unit.
We could not find the part number, price or signal levels of the PWM print online. The IQ unit also speaks Modbus RTU over RS485.

## Mains-side control (confirmed from the Duco quick-start drawing)

The board has screw terminals L1 (always 230 V), L2 (230 V in medium speed only), L3 (230 V in high speed only); L2 and L3 must never be live together.
A two-relay module with interlock can replace the manual switch. (A Zigbee dual relay module is the candidate; its interlock is not in the manuals, so do it in software and test with lamps first.)

## RF notes

- We captured no frame from the Duco remote (RF-C) in 868 or 433 MHz. Its battery drains quickly and one button had a leak. The radio chip looks like a transmitter-only CC1150 (hypothesis).
- A community project (reportedly `duco-rf-bridge`) says it decoded the Duco RF protocol; we have not checked it and pairing is not documented there.
