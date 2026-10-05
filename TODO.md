# TODO

## Bracelet idle timeout and sleep (protocol 0 / Shenzen New Dody)

Observed on the first test bracelet:

- The bracelet ignores the radio until its button is pressed ("asleep" out of the box).
- About 20-21 minutes after the last radio command, it turns red on its own (no transmission from the
  controller). It is still listening: a colour sent afterwards is applied immediately.

Parked for now (2026-10-04): not a priority. To sort out when it is:

- [ ] Find out how it goes to sleep (long press? after a longer idle time?) and whether anything over the
      radio can wake it or put it to sleep.
- [ ] Confirm that red is an idle colour and not a low-battery warning (retest with a fresh battery).
- [ ] Test `refresh_ms` = 60000: does resending the zone state every minute stop the red? Leave it idle
      for over 25 minutes.
- [ ] Decide on a default: a keep-alive refresh (maybe only while input is live, or a separate
      "keep-alive" setting), and whether the input timeout should keep sending "off".
- [ ] Manual sends (Send, Zone walk, raw) don't update the zone's stored state, so with refresh on they
      get replaced within one interval. Decide if that's acceptable.
- [ ] Document the findings in docs/PROTOCOL.md and SETUP.md (wake with button; idle behaviour). Done for what is known so far.

## Protocol 0 addressing

- [x] 16-bit group mask (bytes 1-2) confirmed with two bracelets on groups 2 and 3 (2026-10-04): a combined
      mask (`000C000F`) lights both, an empty one (`0000000F`) neither, and "all except one group"
      (`00FBFF0F`, `00F7FF0F`) skips just that group.
- [x] Used for All Zones as a base layer: it addresses "every group except the zones showing their own
      colour", so an All Zones change or "off" no longer overrides a zone that holds a colour.
- [x] docs/PROTOCOL.md "Addressing" updated with the confirmed mask behaviour.
- [x] Built-in effects (2026-10-04, two bracelets): `05 AA` fades in to the last colour, `06 AA` fades out to
      black (`06 10` / `06 FF` do nothing). The `D0 FF FF FF 55 00 22` packet gets no reaction.

## xLights integration

- [x] Controller definition (`tools/xlights/net2rf.xcontroller`: one port, DDP / E1.31, upload through xLights'
      WLED driver) and custom models (All + 4, All + 15). The controller answers the WLED config requests
      xLights sends (`/json/info`, `/json/cfg`).
- [x] *Upload Output* from xLights 2026.17 with the All + 15 model on the WLED profile: 16 zones created.
- [ ] Still to run from xLights: *Upload Input*, the All + 4 model, and the Net2RF definition itself (an App
      Store install of xLights can't have the file added).
- [ ] Offer the definition to xLights (enhancement issue first, then a PR adding
      `resources/controllers/net2rf.xcontroller`).
- [ ] DMX mode in xLights: decide how DMX fixtures attach (the definition has no serial port).

## Controller discovery / multiple controllers

- [x] Heartbeat tested on hardware with `tools/peer_sim.py --to <ip>` (2026-10-02): simulated peers appear
      with state, one goes offline after ~15 s, hello is answered (also across subnets), `net2rf.local`
      follows the lowest ID and is reclaimed when it leaves.
- [ ] Test listen before transmit with two radios in range (default threshold now -75 dBm: a quiet CC1101
      read -84 to -87, so -85 waited on noise every time), check `lbt_waits` / `lbt_forced` while both run busy sequences, verify the SX1278 RX path
      (packet mode, DIO2 = TimeOut) doesn't disturb its TX data line. Then decide whether to turn it on by
      default.
- [x] FPP discovery: answers FPP's discover ping (UDP 32320, format from FPP's docs/ControlProtocol.txt).
- [x] xLights' *Discover* finds it and adds it by hostname (2026-10-04).
- [ ] Get a proper identity instead of "other system" (0xC0), without borrowing another product's:
      1. ask the FPP project to assign a MultiSync system type code for Net2RF LED (their `MultiSyncSystemType`
         list and docs/ControlProtocol.txt), then report that code in the ping;
      2. ask xLights to handle that code in discovery like its other DDP bridges (protocol DDP, Keep Channel
         Numbers off, vendor / model looked up from the model string `Net2RF-LED`), together with the
         controller definition PR.
      Until then a discovered controller is created as E1.31 with no vendor and has to be corrected by hand.
- [ ] Check how FPP's MultiSync page shows it.

## Hardware

- [ ] Range test across the yard (CC1101 at +10 dBm; Ra-02 as the long-range option).
- [ ] Test with more bracelets at once, and with protocol 1 (LedGiftSupplier) hardware.
- [ ] Carrier board and enclosure (in progress, tracked outside the repo for now).
