# Contributing

Reports from real hardware are the most useful thing you can send. New bracelets, batches and transmitters all widen
what's known: [Compatibility](docs/reference/compatibility.md#reporting-a-device) lists what helps. Open items are in
[TODO.md](TODO.md).

## Building from source

The firmware uses [PlatformIO](https://platformio.org/).

```bash
pio run -e wt32-eth01                  # build firmware.bin and firmware.factory.bin
pio run -e xiao-esp32s3                # the same firmware for a Seeed XIAO ESP32-S3
pio test -e native                     # unit tests
python3 tools/mock_server.py           # develop the web UI against a fake API at http://127.0.0.1:8765/
python3 tools/ddp_test.py <ip> cycle   # send DDP without xLights
python3 tools/peer_sim.py --to <ip>    # simulate other controllers on the network
```

## Repository layout

| Path | What it is |
|---|---|
| `src/` | The firmware: engine (zones, scheduling, RF), network, web server, OLED and button, peers, updater |
| `web/index.html` | The web UI (gzipped into the firmware at build time) |
| `lib/net2rf_protocol` | Packet encoder and decoder, receiver zone tracking, DDP / E1.31 parsers (pure C++, unit tested) |
| `lib/cc1101_ook`, `lib/sx1278_ook` | Minimal radio drivers for OOK transmit and listen before transmit |
| `lib/net2rf_heartbeat`, `lib/net2rf_fpp` | Controller heartbeat and FPP discovery ping formats (unit tested) |
| `lib/net2rf_wled` | The WLED config requests xLights' upload uses (unit tested) |
| `lib/net2rf_version` | Release version comparison for the update notice (unit tested) |
| `tools/xlights` | xLights models and controller definition, and the script that generates the models |
| `hardware/case` | The printed case: STLs and OpenSCAD source |
| `flasher/` | The browser flasher, deployed to GitHub Pages at each release |

## Releases

Add a section to [CHANGELOG.md](CHANGELOG.md) for the new version first: it becomes the release notes. Pushing a `v*` tag
then runs the tests, builds both boards, publishes a GitHub release with the firmware, and redeploys the browser flasher.
