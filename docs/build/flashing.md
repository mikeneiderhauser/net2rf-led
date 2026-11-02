# Flashing the firmware

The first flash goes over USB. After that, updates install over the network from the [web UI](../guides/user-guide.md#firmware-updates).

This page covers the WT32-ETH01, on a DIY build or the carrier board. The XIAO ESP32-S3 flashes over its own
USB-C port: see [its guide](xiao-esp32s3.md).

## Choosing a programmer

The WT32-ETH01 has no USB port, so the first flash needs one of these:

- **USB-serial adapter:** any 3.3 V FT232, CH340 or CP2102 board. The carrier board has pins exposed for one.
- **Dedicated programmer:** a [WT32-ETH01 adapter board](https://www.aliexpress.us/item/3256810144326285.html) with a
  CH340C USB chip. The module plugs straight in, with no wiring.

## WT32-ETH01: first time (serial)

1. Wire the USB-serial adapter, set to **3.3 V logic**:

   | Adapter | WT32-ETH01 |
   |---|---|
   | GND | GND |
   | TX | RXD0 (IO3) |
   | RX | TXD0 (IO1) |

   Power the WT32-ETH01 from its own 5 V supply: an adapter's 3.3 V pin can't supply enough current.
2. Enter the bootloader: **hold BOOT, tap EN, release BOOT**.
3. Flash, either way:
   - **Browser:** open the [web flasher](https://mikeneiderhauser.github.io/net2rf-led/) in Chrome or Edge and click **Install**. It always
     flashes the latest release.
   - **Command line:**
     ```bash
     python3 -m esptool --chip esp32 write-flash 0x0 net2rf-led-<version>.factory.bin
     ```
     Download it from the [latest release](https://github.com/mikeneiderhauser/net2rf-led/releases/latest). The `.factory.bin` contains the bootloader, partition
     table and app, so it goes at address `0x0`. Building from source: `pio run -e wt32-eth01` writes
     `firmware.factory.bin` to `.pio/build/wt32-eth01/`.
4. Tap **EN** to run the new firmware.

## After the first flash

Screw the antenna on **before** powering up, then see the [user guide](../guides/user-guide.md) for connecting to the controller,
the buttons, LED and OLED, and firmware updates over the network (no cable needed after the first flash).

