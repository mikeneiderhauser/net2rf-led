# XIAO ESP32-S3 build

A Seeed Studio **XIAO ESP32-S3** and a CC1101 module, wired the way
[CrispyPyro/Wireless_DMX_Receiver](https://github.com/CrispyPyro/Wireless_DMX_Receiver) wires them. So a board
built for that project runs this firmware unchanged. It has the same features as the WT32-ETH01 build, receiver
mode included, except that it has **no Ethernet**: it joins Wi-Fi, or opens the setup hotspot. It builds, but hasn't been
tested on hardware yet.

| CC1101 | XIAO pad | GPIO | Notes |
|---|---|---|---|
| VCC | 3V3 | | 3.3 V only |
| GND | GND | | |
| CSN | D3 | 4 | |
| GDO0 | D4 | 5 | OOK data both ways: TX in, and RX out in receiver mode |
| GDO2 | D5 | 6 | not used (fine to leave connected) |
| SCK | D8 | 7 | |
| MISO (SO) | D9 | 8 | |
| MOSI (SI) | D10 | 9 | |

Optional parts:

| Part | XIAO pad | GPIO |
|---|---|---|
| OLED SDA / SCL | D6 / D7 | 43 / 44 |
| SX1278 (Ra-02) reset, instead of a CC1101 | D0 | 1 |
| USER button | the board's **B** (BOOT) button | 0 |
| Status LED | the board's orange LED | 21 |
| [Amplifier power switch](amplifier.md#amplifier-power-switch) | D1 | 2 |

The BOOT button works as USER once the firmware runs (short press = next OLED page, hold 5 s / 15 s = network /
factory reset). Holding it while the board resets still enters the USB bootloader.

**Flashing:** plug in USB-C and use the [browser flasher](https://mikeneiderhauser.github.io/net2rf-led/), which
picks the S3 firmware by itself. Or build and upload with `pio run -e xiao-esp32s3 -t upload`. If the port doesn't
appear, hold **B**, tap **R**, release B. Later updates work over Wi-Fi from the web UI as on the WT32-ETH01. Release
files for this board are named `net2rf-led-xiao-esp32s3-<version>.bin`, and the controller won't install the other
board's file.

