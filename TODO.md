# TODO

## Bracelet idle timeout and sleep (protocol 0 / Shenzen New Dody)

Observed on the first test bracelet:

- The bracelet ignores the radio until its button is pressed ("asleep" out of the box).
- About 20-21 minutes after the last radio command, it turns red on its own (no transmission from the
  controller). It is still listening: a colour sent afterwards is applied immediately.

To sort out:

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

- [ ] Confirm the 16-bit group-mask theory (bytes 1-2): `0000000F` should not light a group-3 bracelet;
      test a second bracelet or batch to see if it sits on another bit.
- [ ] Address probe: log the address shown about 0.5 s before "Bracelet reacted" is clicked (allowing
      for reaction time), and default the step time to 3 s.
- [ ] Update docs/PROTOCOL.md "Addressing" once confirmed.

## xLights integration

- [ ] Add a Net2RF LED controller definition to xLights (vendor "Net2RF", 1 port, DDP / E1.31, pixel and DMX
      models) and submit a PR to xLights, so users don't have to borrow the WLED / Generic ESP32 profile (8 ports,
      only port 1 used) to keep the controller visualiser. Check how xLights defines controllers (its controller
      definition XML files) and what a PR needs.
- [ ] Optionally answer the few WLED-style endpoints xLights calls, so the upload actions fail cleanly (or work)
      until that definition ships.

## Controller discovery / multiple controllers

- [ ] Flash the controller (192.168.250.230, currently offline) and test the heartbeat with
      `tools/peer_sim.py --to <ip>`: simulated peers appear with state, one goes offline after ~15 s,
      hello is answered, `net2rf.local` follows the lowest ID.
- [ ] Test listen before transmit with two radios in range: pick a sensible default threshold from the quiet
      readings, check `lbt_waits` / `lbt_forced` while both run busy sequences, verify the SX1278 RX path
      (packet mode, DIO2 = TimeOut) doesn't disturb its TX data line. Then decide whether to turn it on by
      default.
- [ ] FPP discovery: answer FPP's ping protocol (multicast 239.70.80.80, UDP 32320) so controllers show up in
      FPP's MultiSync page and xLights' controller discovery. Confirm the packet format against FPP's source.

## Hardware

- [ ] Range test across the yard (CC1101 at +10 dBm; Ra-02 as the long-range option).
- [ ] Test with more bracelets at once, and with protocol 1 (LedGiftSupplier) hardware.
- [ ] Carrier board and enclosure (in progress, tracked outside the repo for now).
