# Net2RF LED documentation

## Running a show

| Guide | What's in it |
|---|---|
| [Show setup](guides/show-setup.md) | Adding devices to an xLights or Falcon Player show, step by step |
| [User guide](guides/user-guide.md) | The web UI, buttons, OLED, firmware updates, settings and recovery |
| [Several controllers](guides/multiple-controllers.md) | Coverage groups for bigger areas, and how they compare with an amplifier |
| [Home Assistant](guides/home-assistant.md) | Sensors, switches and automations |

## Building a controller

| Guide | What's in it |
|---|---|
| [Overview](build/README.md) | Choosing a build |
| [Carrier board](build/carrier-board.md) | The R2.0 board, its notes and photos |
| [DIY build](build/diy-build.md) | Parts, pinout and wiring for a hand-built controller |
| [XIAO ESP32-S3](build/xiao-esp32s3.md) | A small Wi-Fi-only build |
| [Flashing](build/flashing.md) | Installing the firmware the first time |
| [External amplifier](build/amplifier.md) | More range, and switching the amp from the firmware |
| [Printed case](../hardware/case/README.md) | STLs, OpenSCAD source and fitting instructions |

## Technical reference

| Document | What's in it |
|---|---|
| [Compatibility](reference/compatibility.md) | What has been tested, and how to report a device |
| [Radio notes](reference/radio.md) | Range, airtime, multiple controllers, coverage groups and the rules |
| [RF protocols](reference/protocol.md) | The 433 MHz protocols, packet by packet |
| [HTTP API](reference/api.md) | Endpoints, settings, stats and controller discovery, as an OpenAPI spec with a browsable explorer |
| [xLights files](../tools/xlights/README.md) | Custom models and the controller definition |

## Project

- [Contributing](../CONTRIBUTING.md): building from source, and reporting what you find
- [Changelog](../CHANGELOG.md): what changed in each release
- [Open items](../TODO.md): what's waiting on hardware tests or a decision
