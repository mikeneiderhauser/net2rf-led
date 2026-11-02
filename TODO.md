# TODO

Open items only. What has been done and tested is recorded in [compatibility list](docs/reference/compatibility.md) and the git
history.

## Needs a second controller or more devices

- [ ] **Receiver mode beyond the bench:** the Ra-02 decodes both protocols on the bench (see the compatibility list).
  Still to check: *normal* across the yard; whether the noise-floor filter drops real weak bursts.
- [ ] **SX1278 receive:** *normal* decodes nothing from a strong nearby transmitter, so its AGC doesn't back off in time.
  Check it recovers at range when a transmitter comes and goes; otherwise fix the LNA gain or add an RSSI trigger.
- [ ] **Raw captures:** captures save, decode and export; open an `.ook` export in rtl_433.
- [ ] **Coverage group, beyond two controllers on Wi-Fi** (that case works: see `docs/reference/compatibility.md`): watch a device out of one controller's
  range to confirm it follows the other. Then master + 2 remotes: clock error
  (`group.clock.error_us`), slot timing on air (receiver mode or an SDR: each controller's copies back to back,
  no overlap), `late` / `missed` counts during a busy sequence, and a coverage walk. Then tune the guard
  (3 ms + 2 × error), the lead (15 ms + 2 × round trip) and the settle repeat (1 s); check the master stays
  responsive with 3+ remotes at 3 copies (a plan blocks the engine until it ends).

## Needs hardware checks

- [ ] **XIAO ESP32-S3 build** on a real board: boot, Wi-Fi, the radio on the documented pins, BOOT as the USER button,
  the per-board firmware file in the updater and the browser flasher.
- [ ] **Amplifier power switch** (`/api/amp`, IO2): no switch has been wired yet. Check the buck's EN polarity, that
  the amp follows the switch, and that serial flashing still works with the gate pull-down on IO2. Then the idle
  power-down: that 0.3 s of warm-up is enough for the first packet to go out at full power.
- [ ] **Fade and flicker** on bracelets of both protocols (fade has run on Wally's Lights pucks).

## xLights and FPP

- [ ] Still to run from xLights: the Net2RF controller definition itself
  (the Mac App Store build of xLights can't have the file added).
- [ ] Offer the controller definition to xLights: an enhancement issue first, then a PR adding
  `resources/controllers/net2rf.xcontroller`.
- [ ] A proper identity in discovery instead of "other system" (0xC0), without borrowing another product's:
  1. ask the FPP project to assign a MultiSync system type code for Net2RF LED (their `MultiSyncSystemType` list
     and docs/ControlProtocol.txt), then report that code in the ping;
  2. ask xLights to handle that code like its other DDP bridges (protocol DDP, Keep Channel Numbers off, vendor /
     model looked up from the model string `Net2RF-LED`), together with the definition PR.

  Until then a discovered controller is created as E1.31 with no vendor and has to be corrected by hand.

## Docs

- [ ] New xLights screenshots for the show setup guide: the controller properties and the visualiser with the Net2RF
  model on port 1.

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
