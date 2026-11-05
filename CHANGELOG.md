# Changelog

What changed in each release. The section for a version becomes its
[GitHub release](https://github.com/mikeneiderhauser/net2rf-led/releases) notes.

## [0.2.2] - 2026-11-04

A fix for the web UI on controllers with an OLED.

### Fixed

- **System → Firmware was blank, and the page said "offline",** on a controller with an OLED fitted (since 0.2.0).
  The Display row read a status field 0.2.0 removed, and the error stopped the whole page from updating. It now
  reads "OLED at 0x3C" (or 0x3D).

## [0.2.1] - 2026-11-04

Senders can now pace themselves to the radio without reading the settings.

### Added

- The status broadcast carries the repeat count (`rep`) and, on a coverage group master, the remotes synced now
  (`rem`) and the copies each controller sends (`cp`). A beat goes out at once when the repeats or the synced
  remotes change. The FPP plugin (fpp-net2rf) uses them to set its rate.
- `/api/stats` gives the repeat count (`repeats`), so it can be read on a controller with an admin password.

## [0.2.0] - 2026-11-02

The carrier board and the coverage group have now run real shows, and the defaults suit the devices you can buy.

### Highlights

- **Saved fade presets:** name a palette and keep up to 8 on the controller, next to the built-in ones.
- **Shows take over:** a mode started at power-up now gives way to live show input, and comes back 10 seconds after
  the show stops. A mode you start by hand still overrides a show.
- **Listen before transmit is on by default:** controllers in range now wait for a clear channel. In testing with two
  controllers competing, a third heard 59-75% of changes with it on, against 6-12% with it off.
- **Coverage groups tested on real controllers:** two controllers ran a real show over Wi-Fi with no missed changes.
- **New documentation:** a product-style README, and separate guides for running a show, building a controller and
  technical reference. Adds pages for several controllers, the external amplifier and flashing, with new wiring and
  amplifier switch drawings.

### Added

- Amplifier idle power-down: the amp powers down after a set time with no transmissions, and back up for the next one.
- Scan for networks now shows a visible list with a **Use** button.
- `/api/presets` for saved palettes.
- The controller's connection in `/api/status`, and its board and connection in the status broadcast.

### Changed

- **Protocol 1 is the default** on new and factory-reset controllers. Controllers with saved settings keep their own.
- **Listen before transmit** is on for new and factory-reset controllers. Controllers with saved settings keep their
  own.
- **Settings export and import** now include the power-up mode, the amplifier switch and saved presets.
- **Reset settings** now also clears those three, and puts zones back on protocol 1.
- The web UI, docs and wiring diagrams say "OLED" instead of chip names.
- The case's small OLED lids are renamed: `lid-small-oled.stl`, `lid-slim-small-oled.stl` and `oled-test-small.stl`, with
  `OLED="standard"` or `"small"` in the OpenSCAD source. The shapes are unchanged, and the case stays R28.

### Removed

These may affect scripts and integrations:

- **1.3" OLED support.** The firmware now supports 0.96" displays only (SSD1306 or SSD1315).
- `type` on `/api/display`.
- Writing `devices.protocol` through `/api/config`: use `devices.protocols`. It is still reported, read-only.
- A single `addr` on a zone: use `addr_p0` and `addr_p1`.
- `engine.zones[].packet` in `/api/status`: use `packets`.
- `device.display_info.type` in `/api/status`.

### Tested on hardware

- Coverage group with two controllers over Wi-Fi, following a real show.
- Listen before transmit with three controllers.
- Receiver mode on the Ra-02: both protocols, with no bad frames. Use *near* or *wide* with a transmitter this close.
- A power-up mode giving way to a real show.
- xLights *Upload Input* and the All + 4 model.
- Saved presets, settings export and import, and the SSD1315 OLED.

### Upgrade notes

- Your settings are kept, including the protocol and listen before transmit settings you already have.
- Anything that reads or writes the fields under *Removed* needs updating.
- Home Assistant examples now use `net2rf_` names instead of `bracelets_`. Existing configurations keep working, so
  there's no need to change yours.

## [0.1.0] - 2026-10-31

The R2.0 carrier board is built and tested, with the Ra-02 radio, in both printed cases.

### Added

- Fade and Flicker modes, with Halloween, Christmas and red, white and blue palettes.
- Any test mode can start at power-up.
- Amplifier power switch on IO2 (`/api/amp`).
- Printed case R28: amp and slim variants, SSD1315 lids, press-fit antenna plug and enclosure bracket.

### Changed

- The Tools page is split into standard and advanced tools.

### Tested on hardware

- Carrier board R2.0 and the Ra-02, and range across a yard with and without an amplifier.
- Wally's Lights wristbands, pucks, wand and Santa hat, from xLights and FPP.
- FPP as the show player, and its MultiSync page.
- USER button, OLED wake, network and factory resets, group tester and range walk.

## Earlier releases

See the [releases page](https://github.com/mikeneiderhauser/net2rf-led/releases).

[0.2.0]: https://github.com/mikeneiderhauser/net2rf-led/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/mikeneiderhauser/net2rf-led/compare/v0.0.10...v0.1.0
