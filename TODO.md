# TODO

Open items only. What has been done and tested is recorded in [docs/DEVICES.md](docs/DEVICES.md) and the git
history.

## Needs a second controller or more devices

- [ ] **Receiver mode on air**, CC1101 and Ra-02: decode range against a second controller; which tuning profile
  works on the bench (*near* expected) and across the yard (*normal*); edge interrupt load on a noisy channel
  (`edges` / `dropped` / `noise_pauses` in `/api/status`); whether the noise-floor filter drops real weak bursts.
- [ ] **SX1278 receive:** confirm the AGC setting (`RegRxConfig` 0x08, gain picked when RX starts) holds up when a
  transmitter comes and goes; otherwise fix the LNA gain or add an RSSI trigger.
- [ ] **Raw captures:** check a real protocol 0 / protocol 1 burst against the bit guess, and an `.ook` export in
  rtl_433.
- [ ] **Listen before transmit** with two radios in range: check `lbt_waits` / `lbt_forced` while both run busy
  sequences, and that the SX1278 RX path (packet mode, DIO2 = TimeOut) doesn't disturb its TX data line. Then
  decide whether to turn it on by default. (Default threshold is -75 dBm: a quiet CC1101 read -84 to -87.)
- [ ] **Protocol 1 on real devices** (Wally's Lights hat, pucks, bracelets, sticks on order): the vendor-matched
  encoding (full 8-bit colour, checksum with the group), a group above 15, and the old Flipper-style packet for
  comparison. Then a zone on both protocols, and more devices at once.
- [ ] **Protocol 1 sync timing:** CrispyPyro's captures of the vendor transmitter suggest a long mark, a ~1000 µs
  gap and two copies per burst; we send the Flipper app's 200 / 1600 µs sync. Try the vendor's only if devices
  ignore ours; if both are needed, make it a setting.

## Needs hardware checks

- [ ] **Range test** across the yard (CC1101 at +10 dBm; Ra-02 as the long-range option), with *Tools → Range walk*.
- [ ] **XIAO ESP32-S3 build** on a real board: boot, Wi-Fi, CC1101 on the documented pins, BOOT as the USER button,
  the per-board firmware file in the updater and the browser flasher.
- [ ] **USER button** on a board that has one: the status LED stages while it is held (steady, slow blink, rapid
  blink), and the network and factory resets it triggers.
- [ ] **Reset network** and **Factory reset** from the System page (only *Reset settings* was run on hardware).
- [ ] **OLED wake** on a network address change, hotspot start / stop and a radio fault.
- [ ] **Group tester** and **Range walk** from a real browser with devices awake (checked against the mock only).
- [ ] **FPP:** how its MultiSync page shows the controller.
- [ ] **Carrier board and enclosure** (in progress, tracked outside the repo for now).

## xLights and FPP

- [ ] Still to run from xLights: *Upload Input*, the All + 4 model, and the Net2RF controller definition itself
  (the Mac App Store build of xLights can't have the file added).
- [ ] Offer the controller definition to xLights: an enhancement issue first, then a PR adding
  `resources/controllers/net2rf.xcontroller`.
- [ ] A proper identity in discovery instead of "other system" (0xC0), without borrowing another product's:
  1. ask the FPP project to assign a MultiSync system type code for Net2RF LED (their `MultiSyncSystemType` list
     and docs/ControlProtocol.txt), then report that code in the ping;
  2. ask xLights to handle that code like its other DDP bridges (protocol DDP, Keep Channel Numbers off, vendor /
     model looked up from the model string `Net2RF-LED`), together with the definition PR.

  Until then a discovered controller is created as E1.31 with no vendor and has to be corrected by hand.
- [ ] DMX mode in xLights: decide how DMX fixtures attach (the definition has no serial port, and upload only sets
  the zone count in pixel mode).

## Docs

- [ ] Rework docs/SETUP.md section 5 ("Add it to xLights"), which carries an under-construction note: new
  screenshots of the controller properties and the visualiser with the Net2RF All + 15 model on port 1, and a pass
  over the steps so they lead with the ready-made model, *Upload Output* and *Discover*.
- [ ] Sequencing advice in docs/SETUP.md: it suggests letting the devices follow a group such as the floods. A
  flood sequence with fades and strobes measured 19% airtime on average and over 40% in bursts (one 94 ms packet
  per colour step). Recommend sequencing the device model by itself with held, solid colours, with those numbers.
- [ ] The docs still say "bracelet" where they mean any device (about 180 mentions); the web UI and source say
  "device".

## Ideas, not started

- [ ] **Receiver output over the network** (after the decoder is proven on air):
  1. **DDP out** of what the receiver tracks: one pixel per group (All first, then groups 1-15, the same layout
     the controller takes as input) to a configured address, sent on change plus a slow repeat. Drives WLED / FPP /
     any DDP device from someone else's transmitter, and makes a second controller a repeater with no new protocol.
     To decide: fixed RGB values for protocol 0's ten colours; fade out = black, fade in = the last colour; check
     that a receiver hearing its own relay doesn't loop.
  2. **Event feed**, later: one JSON line per decoded transmission (protocol, group, command, signal strength) over
     UDP or a live HTTP stream, for logging and Home Assistant.

  Raw pulse timings stay a download (`/api/rx/capture`): not worth streaming.
- [ ] Use the devices' own fade out / fade in from pixel mode (one packet instead of a staircase of colour steps).
- [ ] Learn a protocol from a raw capture.
- [ ] MQTT, or a Home Assistant integration beyond the REST examples.
- [ ] Larger firmware slots on boards with 8 MB flash (the layout uses 4 MB; the firmware is at about 78% of its slot).

## Parked: idle and sleep of protocol 0 bracelets

Observed on the Banana Ball bracelets: they ignore the radio until their button is pressed, and about 20 minutes
after the last command they turn red by themselves while still listening. Not a priority (2026-10-04).

- [ ] How do they go to sleep (long press, a longer idle time), and can anything over the radio wake them or put
  them to sleep?
- [ ] Is the red an idle colour or a low-battery warning? Retest with fresh batteries.
- [ ] Does re-sending the zone state every minute (`refresh_ms` = 60000) stop the red? Leave one idle for over
  25 minutes.
- [ ] If it does: a keep-alive setting (perhaps only while input is live), and whether the input timeout should
  keep sending "off". Manual sends (Send, Zone walk, raw) don't update a zone's stored state, so with refresh on
  they are replaced within one interval; decide whether that's acceptable.
