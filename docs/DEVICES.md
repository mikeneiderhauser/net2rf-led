# Tested hardware and devices

What has actually been run against real hardware, and what's only supported on paper. If you try something
that isn't listed, please [report it](#reporting-a-device): every report widens the list.

**Status key:**
- ✅ **tested:** run on real hardware.
- 🧪 **supported, untested:** implemented, but not yet run on real hardware.
- ❔ **unknown:** not checked yet.

## RF devices (bracelets, sticks, pucks)

### ✅ Banana Ball bracelet (protocol 0)

![Banana Ball bracelet, PCB SD-B15ST1K1](images/bananaball_rf_bracelet_pcb.jpeg)

| | |
|---|---|
| Where from | LED bracelet from a Banana Ball game |
| PCB marking | `SD-B15ST1K1` |
| Radio | 433 MHz receiver; 13.52127 MHz crystal (`X1`); loop antenna around the board edge; main chips unmarked |
| Controls | push button |
| Protocol | **0, Shenzen New Dody** (lights red in *Tools → Which protocol is my device?*) |
| Colours | The protocol's 10 fixed colours |
| Effects | **Fade in** (`05 AA`, back to the last colour) and **fade out** (`06 AA`, to black) work. The app's third effect packet does nothing. |
| Address | Two bracelets tested: one on **group 3** (`0008000F`), one on **group 2** (`0004000F`). Both answer the all-groups address `00FFFF0F`. Other bracelets may be on different groups. |
| Tested with | WT32-ETH01 + CC1101, xLights over DDP in pixel mode: follows the sequence |

What to know:
- **Wake it first.** It ignores the radio until its button is pressed.
- **Latches.** It holds the last colour (or off) until it gets a new command.
- **Idle red.** About 20 minutes after the last command it turns red by itself, but keeps listening. The next
  colour change brings it back. It's not yet known whether this is idle or low battery, and whether it sleeps
  later.
- **Two units tested,** on groups 2 and 3. Bracelets from the same event can be on different groups. Use the
  address probe to check yours.

### 🧪 LedGiftSupplier.com bracelets and light sticks (protocol 1)

Supported from the protocol as decoded from the Flipper Zero app and the vendor's DMX transmitter guide, with the
colour and checksum bytes matched to off-air captures of the vendor's transmitter: full 8-bit RGB, group codes,
group 0 = all. Not yet tested with a real device. This is the protocol the vendor's "DMX to RF transmitter" kits
use.

### 🧪 Wally's Lights RF products (protocol 1)

Wally's Lights' RF range (hats, pucks, bracelets and light sticks, sold with a "DMX to RF" transmitter) uses
**protocol 1**, the same protocol as the LedGiftSupplier devices above. A hat, pucks, bracelets and sticks are
on order to test here; until they arrive, nothing from this range has been driven by this controller. Only the
receiving products are on order, not the vendor's transmitter.

To be checked when they arrive: that they follow the controller's protocol 1 packets, how their groups (the
vendor's zones) map to group codes, and how they behave when idle.

### ❔ Other products

Other RF pucks, wands, hats and the like may use one of these two protocols or something else. Try *Tools →
Which protocol is my device?* and report the result.

## Controller hardware

| Part | Status | Notes |
|---|---|---|
| WT32-ETH01 (ESP32 + LAN8720) | ✅ | Ethernet and Wi-Fi both used. Not the WT32-ETH01-EVO (different chip and pinout). |
| Seeed XIAO ESP32-S3 | 🧪 | Wi-Fi only; own build (`xiao-esp32s3`), wiring in [ASSEMBLY.md, part C](ASSEMBLY.md#c-xiao-esp32-s3). |
| CC1101 433 MHz module, blue 2×4 header + SMA (AOICRIE, see [parts](ASSEMBLY.md#parts)) | ✅ | Reports chip version `0x14`; data-line self-test passes; drives the Banana Ball bracelet. |
| Ai-Thinker Ra-02 (SX1278) on a breakout | 🧪 | Driver written to the datasheet; never run. |
| SSD1306 0.96" I²C OLED | ✅ | Including detection of swapped SDA/SCL. |
| SH1106 1.3" I²C OLED | 🧪 | Selectable under *System → Display*. |
| 3.3 V USB-serial adapter (FTDI-style) | ✅ | First flash; updates are then over the network. |
| Carrier board | 🚧 | Coming soon ([ASSEMBLY.md, part B](ASSEMBLY.md#b-carrier-board-coming-soon)). |

## Software and features

| | Status |
|---|---|
| xLights → DDP, pixel mode | ✅ (controller profile WLED / WLED / Generic ESP32, Keep Channel Numbers off) |
| DDP, DMX mode | ✅ colour (with a DDP test sender); 🧪 the fade channel values (the fades themselves work when sent directly) |
| DDP, vendor mode | 🧪 |
| E1.31 (sACN) | ✅ unicast (other universes ignored); 🧪 multicast |
| xLights *Upload Output* (zone count) with the All + 15 model | ✅ from xLights 2026.17, WLED / Generic ESP32 profile |
| xLights *Upload Input*, and the Net2RF controller definition | 🧪 |
| Base layer and same-colour merging (protocol 0) | ✅ packets checked on the controller; bracelets on groups 2 and 3 |
| FPP discovery ping (found by FPP / xLights) | ✅ xLights 2026.17 *Discover* adds it by hostname; 🧪 FPP's MultiSync page |
| Falcon Player (FPP) as the show player | 🧪 (should work over DDP / E1.31 like xLights) |
| Web UI, setup hotspot, Wi-Fi join, firmware update with rollback | ✅ |
| Test mode, zone walk, address probe | ✅ |
| Several controllers (live list, `net2rf.local` election) | ✅ against simulated controllers (`tools/peer_sim.py`); 🧪 with two real ones |
| Listen before transmit | 🧪 channel sensing works (quiet CC1101 reads -84 to -87 dBm); backing off needs two radios in range |
| Home Assistant examples | 🧪 |

## Reporting a device

Open an issue with:

1. **Photos** of the device, outside and the PCB (markings, crystal, chips), like the one above.
2. **Where it came from:** event, vendor, listing.
3. **Protocol:** what *Tools → Which protocol is my device?* shows (red = 0, green = 1, nothing).
4. **Address:** what *Tools → Address probe* finds (see [SETUP.md](SETUP.md#3-decide-how-the-bracelets-fit-into-the-show)).
5. **Behaviour:** does it need a button press to listen, what does it do when idle, and how long does it last on
   its batteries?
6. Your controller hardware and firmware version (*System* page).
