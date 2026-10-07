# Bracelet RF protocol

Reverse engineered from the Flipper Zero `bracelet_led.fap` app ("App by RGB_Lights"), whose Setup screen switches between
two targets: **Shenzen New Dody** (Tech Co.) and **LedGiftSupplier.com** (the app's default). Protocol 1 was then
confirmed against the community *DMX Interactive Products* guide for the LedGiftSupplier DMX transmitter, and its
colour and checksum bytes corrected against RTL-SDR captures of that transmitter (see [Protocol 1](#protocol-1-rgb-with-group-codes)).
Encoder and tests: [`lib/net2rf_protocol`](../lib/net2rf_protocol/).

Both work with bracelets (2 × CR1632, 2 LEDs) and light sticks (3 × AAA, 5 LEDs). The LEDs are not individually
addressable: a device is one colour. Devices **latch**: they hold the last colour received until told otherwise.

| | Protocol 0 (Shenzen New Dody) | Protocol 1 (LedGiftSupplier.com) |
|---|---|---|
| Tested with real devices | **Yes**, two bracelets (Banana Ball giveaway, board `SD-B15ST1K1`) driven from xLights | Not yet: encoding matches 61 off-air captures of the vendor's DMX transmitter |

Device details and photos: [DEVICES.md](DEVICES.md).

## Device behaviour (protocol 0 bracelet, as observed)

- **Wake with the button.** Out of the package the bracelet ignores the radio until its button is pressed. After
  that it follows commands.
- **Latching.** It holds the last colour (or off) indefinitely, so the controller only transmits on change.
- **Idle colour.** About 20 minutes after the last command it turns **red by itself**, but keeps listening: the
  next command is applied immediately. Whether that's an idle indicator or a low-battery warning, and how (or
  whether) it falls back asleep, is still open ([TODO.md](../TODO.md)).

## Radio layer

OOK (carrier on/off), a sync pulse followed by 56 bits (7 bytes), MSB first, pulse-width encoded. Frames are sent
back to back; each is ~46.6 ms. The app repeats a frame 1-9 times per command; the controller sends 3 by default.
The two frequencies are only 31 kHz apart and the bracelets' receivers are wide, so in practice both protocols
share one channel ([RF.md](RF.md#multiple-controllers)).

| | Protocol 0 (Shenzen New Dody) | Protocol 1 (LedGiftSupplier.com) |
|---|---|---|
| Frequency | 433.889 MHz | 433.920 MHz |
| Sync | H 2000, L 1000, H 1000, L 550 µs | H 200, L 1600 µs |
| Bit 1 | H 500, L 250 µs | H 600, L 200 µs |
| Bit 0 | H 250, L 500 µs | H 200, L 600 µs |

## Protocol 0: fixed commands

`A0 A1 A2 A3 CMD ARG SUM`, where SUM = (sum of bytes 0-5) & 0xFF. Default address `00 FF FF 0F`.

| CMD ARG | Meaning |
|---|---|
| `01 nn` | colour nn: 00 red, 01 green, 02 blue, 03 pink, 04 white, 05 yellow, 08 violet, 09 orange, 0A indigo, 0B cyan (06/07 unused by the app) |
| `00 AA` | off |
| `05 AA` | **fade in** to the colour the bracelet last showed (no visible change if it is already lit) |
| `06 AA` | **fade out** to black. Only with `AA`: `06 10` and `06 FF` did nothing, so the second byte is not a speed. |
| full packet `D0 FF FF FF 55 00 22` | sent by the Flipper app as a third effect; no reaction from the tested bracelets, lit or dark |

## Protocol 1: RGB with group codes

`55 GP RR GG BB CK A1`, where GP = group code, each colour byte is the **inverted 8-bit DMX value** (`~v`: `00` =
full, `FF` = off) and

    CK = GP ^ RR ^ GG ^ BB ^ 0x55

(on the packet bytes as sent; in DMX terms that is `0xAA ^ group ^ R ^ G ^ B`).

This comes from RTL-SDR / Universal Radio Hacker captures of the vendor's DMX-to-RF transmitter, made by
[CrispyPyro/Wireless_DMX_Receiver](https://github.com/CrispyPyro/Wireless_DMX_Receiver) (`docs/gflai-protocol.md`,
MIT licence): 61 payloads across groups 0-83 and many colours, all predicted by the formula above. They are the
unit test `test_p1_vendor_captures`. That project counts the pulses differently (an 11-pulse "preamble" is our
`55` byte plus the top bit of the group byte, so it calls the group 7 bits wide), but the bits are the same.

The Flipper app, and this firmware up to v0.0.5, sent 16 levels per channel (low nibble `F`) with
CK = RR ^ GG ^ BB ^ 0x5A. That agrees with the vendor's checksum only in the high nibble, and only for groups 0-15, so
it worked at best if the bracelets check just the high nibble. The firmware now sends exactly what the vendor's
transmitter sends; its receiver mode still accepts both kinds (the log marks the old one).

The same captures show the vendor's transmitter sending each payload twice per burst (~95 ms), and measure the sync
as a long mark and a ~1000 µs gap rather than the app's 200 / 1600 µs. The receiver accepts both; the controller
keeps the app's sync timing until a real bracelet says otherwise.

The vendor's DMX transmitter maps a DMX universe straight onto this packet:

| DMX channel | Packet byte | Meaning |
|---|---|---|
| 1 | `55` | "boot code": must be **85** (0x55); the transmitter only sends while it is |
| 2 | `GP` | **group code**: 0 = all groups; bracelets ship pre-assigned to a group (often 1) |
| 3, 4, 5 | `RR GG BB` | colour, 0-255 (sent as `~value`, full 8 bits) |

Byte 6 (`A1`) has always been `FF`. The controller's *Vendor DMX transmitter* input mode uses exactly this
5-channel layout, so sequences built for the vendor transmitter drive this controller unchanged.

## Addressing

- **Protocol 1:** addressing is the group code in byte 1 (0 = broadcast to all groups). A zone's address is its
  group number.
- **Protocol 0:** a **16-bit group mask** in bytes 1-2, confirmed with two bracelets. The app always sends
  `00 FF FF 0F` (every bit set, reaching every bracelet). Walking single bits with the web UI's **Address probe**,
  one bracelet answers to `00 08 00 0F` (bit 3 of byte 1, **group 3**) and a second to `00 04 00 0F`
  (**group 2**). Groups 0-7 are the bits of byte 1 and groups 8-15 those of byte 2; the probe shows the group
  next to each address. Other bracelets may be on other groups.
  - A zone address is `00` + mask + `0F`.
  - A bracelet answers when its group's bit is set.
  - Masks combine: `00 0C 00 0F` (groups 2 and 3) lit both bracelets.
  - An empty mask reaches nobody: `00 00 00 0F` lit neither.
  - "Everyone except" works: `00 FB FF 0F` (all but group 2) changed only the group 3 bracelet, and
    `00 F7 FF 0F` (all but group 3) only the group 2 one. The controller's *base layer* option uses this
    ([SETUP.md](SETUP.md#all-zones-as-a-base-layer-protocol-0)).

  Bytes 0 and 3 (`00`, `0F`) haven't been explored, and only groups 2 and 3 have been seen on real
  bracelets. Reports from other batches are welcome.

## Interference

The vendor's transmitter was strong enough to stop some car key fobs from working nearby. Use a short antenna and
the lowest power that reaches the audience, don't re-send unchanged colours (the bracelets latch), and switch the
transmitter off between bracelet moments (vendor mode: channel 1 ≠ 85, or the RF output switch).
