# Net2RF LED

**Bring the wristbands, pucks and wands from concerts and stadiums into your own light show.**

Net2RF LED is an open-source controller that turns show data (pixel data in DDP format) into 433 MHz radio commands. It connects to your show
network like any other controller and drives wireless LED wristbands, pucks, wands and hats from **xLights**,
**Falcon Player** or any DDP  source.

<p>
<a href="docs/images/carrier/amp-closed.jpg"><img src="docs/images/carrier/amp-closed.jpg" width="260" alt="Controller in its printed case, running, with the antenna fitted"></a>
<a href="docs/images/carrier/board-assembled.jpg"><img src="docs/images/carrier/board-assembled.jpg" width="260" alt="Carrier board with the WT32-ETH01 and Ra-02 fitted"></a>
<a href="docs/images/carrier/slim-ext-closed.jpg"><img src="docs/images/carrier/slim-ext-closed.jpg" width="260" alt="Slim case, closed, with an external antenna"></a>
</p>

```
xLights / FPP ── DDP ──▶ Net2RF LED ──433 MHz──▶ wristbands, pucks, wands, hats
```

**▶ See it working:** [prototype demo on YouTube](https://youtube.com/shorts/mfn99Rj19TI)

## Why Net2RF LED

- **Sequences like a pixel.** A group of devices is a single pixel in xLights. Import the [ready-made model](tools/xlights/README.md), put
  it on the controller, and sequence *All Zones* or each zone by name.
- **Up to 16 zones.** Light the whole crowd at once, or give each group its own colour.
- **Several controllers, one show.** To cover a bigger area, a [coverage group](docs/guides/multiple-controllers.md)
  keeps up to 9 controllers in sync over the network. Their clocks stay aligned and they take turns on air, so every
  device gets each change cleanly. Tested so far with two controllers over Wi-Fi.
- **Runs without a show.** Palette fades, candle flicker and your own saved presets, started at power-up if you like.
  A show takes over when it starts.
- **Set up from a browser.** Everything is in the built-in web UI, with tools to identify your devices, find their
  groups and walk the range.
- **Built for show night.** An RF output switch, *All off*, and automatic blanking when the show stops.
- **Open.** GPL-licensed firmware, a JSON API, Home Assistant examples, and a printable case.

![Dashboard](docs/images/dashboard.png)

## What it works with

| | |
|---|---|
| **Devices** | Wally's Lights wristbands, pucks, wands and hats · Shenzen New Dody bracelets, including Banana Ball giveaways |
| **Show software** | xLights · Falcon Player · any DDP source |
| **Network** | Ethernet or Wi-Fi, with a setup hotspot for first-time configuration |
| **Integrations** | JSON API · Home Assistant |

LedGiftSupplier.com devices use the same protocol as Wally's Lights, but haven't been tested yet. Have something else?
*Tools → Which protocol is my device?* tells you in seconds. See the full
[compatibility list](docs/reference/compatibility.md).

## Get one

Net2RF LED is a build-it-yourself project. Choose how you'd like to build it:

| Build | What it is | Status |
|---|---|---|
| [**Carrier board**](docs/build/carrier-board.md) | WT32-ETH01 and Ra-02 plug into one board, with a [printed case](hardware/case/README.md) | Tested |
| [**DIY build**](docs/build/diy-build.md) | WT32-ETH01 and a radio module, wired by hand | Tested |
| [**XIAO ESP32-S3**](docs/build/xiao-esp32s3.md) | A small Wi-Fi-only build | Builds, not yet tested |

**Prefer one ready-made?** I may have prebuilt controllers on hand:
[get in touch on Facebook](https://www.facebook.com/mdn15/).

## Get started

1. **Build a controller:** [compare the builds](docs/build/README.md).
2. **Install the firmware:** the [browser flasher](https://mikeneiderhauser.github.io/net2rf-led/) (Chrome or Edge)
   installs the latest release over USB, with no tools to set up.
3. **Add it to your show:** the [show setup guide](docs/guides/show-setup.md) goes from first connection to a
   sequence playing.

## Documentation

- **[Running a show](docs/README.md#running-a-show):** show setup, the user guide, several controllers and Home Assistant
- **[Building a controller](docs/README.md#building-a-controller):** parts, wiring, flashing, the case and the amplifier
- **[Technical reference](docs/README.md#technical-reference):** the HTTP API, RF protocols, radio notes and compatibility

## Project status

Net2RF LED is an early project, and works end to end on real shows. Expect rough edges, and please report what you find.

**Coming soon:**

- **FPP plugin:** each zone follows FPP models live, one colour per zone, within what the radio can send, so existing
  shows drive the devices with no extra sequencing.
- **AI sequencing assistant:** adds the devices to your existing xLights sequences.
- **Next carrier board revision:** fixes R2.0's known issues.

## Contributing

Reports from real hardware are especially welcome, along with captures and test results. See
[CONTRIBUTING.md](CONTRIBUTING.md).

## License and legal

Copyright (C) 2026 Mike Neiderhauser and contributors.

Licensed under the **GNU General Public License v3.0 or later** ([LICENSE](LICENSE)). You may use, modify and sell it,
including in products. If you distribute it, modified or not (for example in a controller you sell), you must make the
corresponding source available under the same licence.

The device protocols were worked out from the behaviour of a Flipper Zero app; no third-party code from it is included.
Libraries used at build time (ArduinoJson and the ThingPulse OLED driver, MIT; Arduino-ESP32 and ESP-IDF) and the browser
flasher (ESP Web Tools) carry their own licences.

433 MHz transmission is regulated. The controller transmits what it's sent, at the power you set; staying within local
rules is up to the operator. See [Legal](docs/reference/radio.md#legal).
