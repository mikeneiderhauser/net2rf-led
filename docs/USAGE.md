# User guide

How the controller behaves and how to use it day to day. Building and first-time flashing:
[ASSEMBLY.md](ASSEMBLY.md). Adding the bracelets to your show: [SETUP.md](SETUP.md).

## Powering up

- **Antenna first.** Screw the antenna on before applying power.
- **What happens at boot:** the network and web UI come up first, and the radio is checked in the background. The
  dashboard shows *radio initializing*, then *ready*, or *not detected* if the module doesn't answer (it keeps
  retrying every 10 s).
- **Settings are kept** across power cycles and firmware updates.

## Connecting

The controller prefers **Ethernet**. Without an Ethernet link it uses **Wi-Fi** if one is configured. If it has no
connection after 15 s, it opens its own **setup hotspot**.

| How | Address |
|---|---|
| Ethernet / Wi-Fi | `http://net2rf.local/` (one controller on the network answers it), its own `http://net2rf-xxxx.local/`, or its IP (shown on the OLED, or in your router's client list) |
| Setup hotspot | Join **NET2RF-XXXX** (password `net2rf1234`). The setup page opens by itself, or browse to `http://192.168.4.1/` |

### The setup hotspot

- It opens when there's no Ethernet link and no Wi-Fi connection for 15 s. It closes again once the controller
  is connected and nobody is using the hotspot.
- To join your Wi-Fi: go to **Network**, then **Scan for networks**, pick yours, enter the password and choose
  **Save & reboot**. After the reboot, reconnect your phone or laptop to your normal Wi-Fi and open
  `http://net2rf.local/`. If it couldn't join, the hotspot comes back within about 20 s and the Network page
  shows why: network not found (the ESP32 only does **2.4 GHz**), wrong password, or weak signal.
- **Phones:** the sign-in window that pops up when you join the hotspot works for settings, but can't pick files.
  For a firmware update or a settings import, open `http://192.168.4.1/` in Safari or Chrome instead.
- Under **Network → Setup access point** you can keep the hotspot always on, or turn it off completely. It
  can't be turned off while neither Ethernet nor Wi-Fi is configured.

### Network settings

**Network** sets the hostname, Ethernet on/off, Wi-Fi network, DHCP or a static IP (used on whichever
interface is active), and the hotspot mode and password. Saving reboots the controller.

For a show, give the controller a **static IP** or a DHCP reservation, so xLights / FPP always find it.

## The web UI

| Page | What's there |
|---|---|
| **Dashboard** | Input rate and packet counters, RF airtime and counters, radio status, device info with free memory and flash; a notice when newer firmware is available; the last colour sent to each zone; other controllers on the network with their live state; **Test mode** (off, solid colour, or cycle R/G/B/W); the **RF output** and **Radio power** switches, and **All off** |
| **Zones** | Add, name, enable and address zones; choose whether All Zones is a base layer under the others; send a colour, off, a fade in or a fade out to one zone; **Zone walk** lights one zone at a time so you can see which bracelets belong where |
| **Devices & Radio** | Controller name, bracelet protocol, input mode (pixel / DMX / vendor), colour order and start channel; radio module, TX power, frequencies, frames per update, refresh, TX jitter, listen before transmit; DDP / E1.31 input and the input timeout |
| **Network** | Current connection and the settings above |
| **Tools** | Testing and diagnostics, described [below](#tools) |
| **System** | Firmware info (version, free memory, free flash) and **Update firmware** (from GitHub releases or a file); an **Advanced** section with the flash partition breakdown; OLED display type and an I²C scan; **admin password**; settings **export / import**; reboot and factory reset |

The pill row at the top of every page shows the radio, input and network state at a glance.

![Dashboard](images/dashboard.png)

### Tools

| Tool | What it's for |
|---|---|
| **Which protocol is my device?** | Alternates the two bracelet protocols: the device lights red for protocol 0, green for protocol 1 |
| **Send raw packet** | Send any 7-byte packet, with presets |
| **Address probe** | Steps through the groups one by one, to find which group a device is on |
| **Group tester** | Tick any combination of the 16 groups and send a colour, off, fade in or fade out to exactly those. Shows the address it uses (`00FBFF0F` = all except group 2). |
| **Range walk** | Cycles red, green, blue, white every 2 s on all zones while you walk the area with a device, with the TX power setting next to it |
| **Input monitor** | The channel values arriving from xLights / FPP right now, per zone: shows a wrong start channel or colour order at a glance |
| **Transmit log** | The last 24 RF packets: when, to which groups, what command. Shows the base layer at work (for example *all except groups 2, 3*: red). |
| **Channel meter** | Live signal strength on the bracelet frequency, measured between transmissions: spot other transmitters and pick the listen-before-transmit threshold |
| **Connection check** | Whether the controller itself can look up, reach and talk to GitHub, which the update check and install need |
| **Identify** | Flickers the status LED and flashes *THIS ONE* on the OLED for 15 s, to tell controllers apart |
| **xLights setup** | The exact controller and model settings for the current zones |

### Controlling output

- **Test mode** overrides the show input for every enabled zone until it's switched off. Use it to check range
  and wiring without running xLights.
- **RF output off** stops all transmission while still receiving and counting the show data. Turning it back on
  re-sends the current colours. The radio already sits idle between updates, so this changes nothing on air
  beyond stopping the updates.
- **Radio power off** also puts the radio chip to sleep, to save a little power when the controller isn't in
  use. Nothing is sent until you switch it back on, which restarts the radio and re-sends the current colours.
  The setting survives a reboot.
- **All off** blanks every bracelet once. They stay dark until the show input changes a colour.
- **Input timeout:** if no show data arrives for 5 minutes (configurable, or 0 for never), the bracelets are
  switched off.

### Receiver mode

The **Receiver mode** switch on the Dashboard turns the controller into a listener: it stops transmitting and
instead decodes every bracelet command it hears on 433 MHz, from this project's controllers, the vendor's DMX
transmitter or a Flipper. Both protocols are decoded at once (the radio listens between their two frequencies).
Use it to check what a transmitter is really sending, to walk the yard and see where commands still arrive (the
signal strength is shown per transmission), or as a stand-in bracelet while you have none.

- The Dashboard's **Heard on air** card lists every group with the colour it was last told, how many commands it
  got, their signal strength and when; below it, the latest transmissions (repeats of one transmission are
  counted as copies). Protocol 1 packets with the old checksum (Flipper app, firmware up to v0.0.5) are marked.
- A command to every group (protocol 0 mask `FFFF`, protocol 1 group 0) updates the *every group* row and every
  group already listed.
- The OLED shows a Receiver page and the heard groups, 4 per page (see [OLED](#oled)).
- Nothing is transmitted while it is on: show input is still counted, and sends from the Tools page are refused.
  Switching it off sends the current colours again.
- Works with both radios and needs no extra wiring: the received signal comes back on the same data line that
  carries the transmit signal (CC1101 GDO0, or SX1278 / Ra-02 DIO2).
- **Tuning** (on the card): *normal* (162 kHz on the CC1101, 167 kHz on the SX1278) is the most sensitive.
  *Near-field* caps the receiver's gain: use it when the transmitter is within a few metres, for example a second
  controller on the same bench, since a strong signal overloads the receiver and frames stop decoding while the
  signal level reads high. *Wide band* (325 / 250 kHz) is for transmitters that are well off frequency.
- **Raw captures:** every burst of remote-control-like pulses that is louder than the noise floor is kept, whether
  it decoded or not. The last 4 are kept, and undecoded ones are kept longest. *View* draws the waveform, groups
  the mark and space widths, and, if the signal is pulse-width coded, reads its bits as hex: a quick way to see
  what an unknown transmitter or remote sends. *.ook* downloads it in [rtl_433](https://github.com/merbanan/rtl_433)'s
  pulse format; `rtl_433 -r capture.ook -A` runs rtl_433's pulse analyser on it.
- Bracelets that were already lit before the receiver started only appear once something changes them.

## Front panel

### Buttons

| Button | Action |
|---|---|
| **USER**, short press | Next OLED page |
| **USER**, hold 5 s | **Network reset:** DHCP, Ethernet on, setup hotspot on fallback, admin password cleared, then reboot. Zones and bracelet settings are kept. |
| **USER**, hold 15 s | **Factory reset:** everything erased, then reboot |
| **EN** | Reset (reboot) |
| **BOOT** + EN | Hold BOOT, tap EN, release BOOT: serial bootloader, for flashing ([ASSEMBLY.md](ASSEMBLY.md#flashing)) |

While USER is held, the OLED shows what will happen on release. The USER button is only active if its pull-up
resistor is fitted (IO39 reads high at boot), so a missing button can't trigger resets.

### Status LED

The WT32-ETH01's onboard LED (IO2):

| Pattern | Meaning |
|---|---|
| Short blink every 2 s | Running, radio ready |
| Fast blink | Radio not detected or still starting |
| Double blink | Setup hotspot active, no network connection |
| Rapid flicker | *Identify* is running (Tools page) |

### OLED

Pages advance with a short press of USER, or rotate every 5 s without a button. The header shows the controller
name and page number.

| Page | Shows |
|---|---|
| Status | IP and interface (ETH / WiFi / AP / offline), hostname (or hotspot name), radio state, firmware version (and a newer release, if one was found) |
| Input | DDP / E1.31 state (live, nothing yet, timed out, TEST MODE, OUTPUT OFF), frame rate, packet count, time since the last packet, enabled inputs |
| Zones | Zone number, name and current colour (hex), 4 per page |
| Receiver *(receiver mode, instead of Input)* | Listening state, frequency and channel level, commands / frames / bad frames, time since the last one |
| Heard *(receiver mode, instead of Zones)* | Protocol and group (`All` = every group), colour (palette name or hex) and age, 4 per page |

- **Update marker:** a **U** in front of the page number (top right) means a newer firmware release is
  available; the Status page's version line says which.
- **Sleep:** the display switches off after 10 minutes without a USER press (*System → Display*: 1 to 240
  minutes, 0 = never). It wakes on the next USER press (which only wakes it), from *Wake display* on the System
  page, after a reboot, and by itself when the network address changes, the setup hotspot starts or stops, or
  the radio fails. A controller without a USER button sleeps too, and relies on those.

An OLED that isn't fitted, or stops answering, is simply ignored; the controller keeps looking for one every 5 s.

## Admin password

Off by default. Set one under **System → Admin password** (8-64 characters). Then the settings, tests and firmware
updates ask for user `admin` and that password, while the dashboard and `/api/stats` stay readable. After 5 wrong
passwords, logins are refused for 30 s. Forgot it? Hold USER for 5 s (network reset), which also clears the
password.

## Firmware updates

Two ways, both under **System → Update firmware**. RF output pauses while the firmware is written, then the
controller reboots.

- **Automatic check:** about 30 s after boot, and then every 12 hours, the controller asks GitHub which
  release is the latest. When it's newer than the running firmware, the **Dashboard** shows *Firmware vX is
  available*, the OLED's status page shows it next to the version, and `/api/stats` reports it. Nothing is
  installed automatically. The check can be switched off, and its period changed (1 to 168 hours), on the same
  card. Without internet access it simply finds nothing and tries again later.
- **From GitHub:** press **Check for updates**. Your browser asks GitHub for the latest release and shows
  whether it's newer than what's running. **Install** then has the controller download and flash it itself,
  so the controller needs internet access. If it can't reach GitHub, the page says so and gives a download link
  for the manual way.
- **Upload a file:** choose `net2rf-led-<version>.bin` from the [latest release](https://github.com/mikeneiderhauser/net2rf-led/releases/latest) (or
  `firmware.bin` if you built it), not the `.factory.bin` used for the first flash.

![System page: firmware version, free memory and flash, and the update card](images/system.png)

**Release source** on the same card sets which GitHub repository is checked (`owner/name`, default
`mikeneiderhauser/net2rf-led`). Change it only if you run firmware from a fork.

**Automatic rollback:** a new build is only kept once the controller has come back up and stayed reachable for
30 s. If it crashes or never becomes reachable within 3 minutes, it returns to the previous firmware by itself,
and the System page says so.

## Backing up settings

**System → Configuration → Export settings** downloads the settings as a JSON file (the Wi-Fi password is never included).
**Import** restores one, optionally with its network settings, then reboots. This is handy for setting up a
second controller the same way.

## Finding the controller from FPP or xLights

The controller answers Falcon Player's discovery, so it appears in FPP's *MultiSync* list and in xLights'
controller *Discover*, by hostname, with its IP, firmware version and channel count. Discovery uses multicast
and broadcast, so it only works within one subnet (xLights also asks controllers it already knows by IP).
xLights doesn't know this type of device yet, so it adds it as an **E1.31** controller with no vendor: change
the protocol to **DDP**, untick *Keep Channel Numbers* and pick the vendor and model
([SETUP.md](SETUP.md#5-add-it-to-xlights)).

## Several controllers

Each controller lists the others on its dashboard, with their state and airtime, and exactly one of them answers
at `net2rf.local`. Controllers in radio range of each other share the 433 MHz channel: see
[RF.md](RF.md#multiple-controllers) before running more than one.

## Recovering a controller

| Problem | Fix |
|---|---|
| Can't find it after a network change (bad static IP, wrong Wi-Fi) | Hold USER 5 s (network reset), then connect via Ethernet/DHCP or the setup hotspot |
| Forgot the admin password | Hold USER 5 s (network reset) |
| Everything is wrong | Hold USER 15 s (factory reset), or *System → Factory reset* |
| A firmware update misbehaves | It rolls back by itself; otherwise flash the `.factory.bin` over serial ([ASSEMBLY.md](ASSEMBLY.md#flashing)) |
| Radio *not detected* | Check the radio's wiring and 3.3 V, and that the right module is selected under *Devices & Radio* |
| Radio *data line fault* | The CC1101's GDO0 isn't reaching IO33: check that wire |
| Bracelets don't react | Press their button (they may be asleep), check they're on the right protocol and group, move closer, raise TX power |
