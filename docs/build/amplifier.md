# External amplifier

An optional 433 MHz amplifier adds range beyond what the Ra-02 reaches alone. It sits between the radio and the
antenna, and has its own supply. Before adding one, try better placement: in testing, mounting the antenna high and
away from other objects made the most difference ([radio notes](../reference/radio.md#range-and-power)).

**An alternative to an amplifier:** several controllers around the area, running as a
[coverage group](../guides/multiple-controllers.md). This keeps each one at its normal power, at the cost of fewer
colour changes a second. The guide [compares the two](../guides/multiple-controllers.md#more-controllers-or-an-amplifier).

## Parts

| Part | Notes |
|---|---|
| 433 MHz amplifier | [AB-IOT-433](https://www.amazon.com/dp/B09T39P1C1), the one used here. It fits the [amp case](../../hardware/case/README.md) |
| 12 V to 5 V buck module | Powers the amp. A 3 A module with an **EN** pin also allows the power switch below |
| 12 V supply | Feeds the controller and the buck |

The amp needs no control wiring: it listens by default and switches to transmit by itself when it detects the radio's
signal. For that to be reliable the seller specifies at least 1 dBm in, which any Ra-02 power setting meets (+2 dBm is
the lowest). Its transmit gain is adjustable: set it as low as covers your area.

On the carrier board the buck takes 12 V from J6. In the amp case, the amp's OUT SMA goes through the far wall and
the buck is strapped to the tie bridge: see the [case guide](../../hardware/case/README.md#assembly).

## Licensing and legal

**An amplifier is likely to take you over the limits for unlicensed use. Check before you transmit.** This is general
information, not legal advice.

- **You are responsible.** The operator has to stay within the rules where the controller is used. The firmware doesn't
  enforce any limit, and an amplifier adds power on top of whatever it's set to.
- **United States:** unlicensed 433 MHz devices fall under FCC Part 15, which sets strict limits on field strength and
  on how often and how long a device may transmit. An amplified transmitter is unlikely to meet them.
- **Amateur radio licence:** 433 MHz is inside the US 70 cm amateur band (420-450 MHz), but a licence doesn't
  automatically cover this use. FCC Part 97 has its own rules on station identification and what may be sent, and a
  licensee must operate within them. Other countries differ.
- **Europe:** 433.05-434.79 MHz short-range devices are limited to 10 mW ERP and a 10% duty cycle.
- **Other countries:** check with your national regulator.

Use the lowest power that covers your area, and transmit only what the show needs. A strong 433 MHz signal can also
stop [car key fobs](../reference/radio.md#car-key-fobs) working nearby. More in [Legal](../reference/radio.md#legal).

## Amplifier power switch

The firmware can power an external RF amplifier up and down from **IO2**, the WT32-ETH01's only free output (on
the XIAO, D1). It's optional: until you enable it, IO2 stays the status LED's pin.

![Amplifier power switch: an N-FET on the buck's EN pin, or a high-side P-FET](../images/amp-switch.svg)

**A. Buck module with an EN pin.** The simplest switch uses the amp's 12 V → 5 V buck module:

- A small N-channel FET (2N7002 or similar): **drain to the buck's EN, source to GND, gate to IO2** through about
  100 Ω, with **100k from the gate to GND**.
- IO2 high pulls EN low and the buck, so the amp, switches off. IO2 low lets the buck's own pull-up switch it on.
- Check the module first: power it from 12 V and measure EN. About 12 V or 5-6 V means it is pulled up, so this
  circuit is right. Near 0 V means an active-low EN (LM2596 type), which needs a different circuit.
- **Then, unpowered, measure the resistance from EN to the 12 V input.** Close to 0 Ω means EN is tied straight to
  the input, usually by a solder jumper (often labelled B) that leaves EN unused: the FET would short 12 V to
  ground. Cut the jumper and measure again (it should read open). Many modules then hold EN high themselves: check
  the output comes on with nothing connected to EN. If it doesn't, add a 100k from the input to EN.
- A 2N7000 (TO-92: source, gate, drain from the left, flat face toward you) works as well as a 2N7002.

Don't connect IO2 to EN directly: EN is usually pulled up to the 12 V input, and IO2 must be low at reset for
serial flashing. The FET and its gate pull-down take care of both.

**B. No EN pin.** Switch the supply with a high-side P-FET (e.g. AO3401, 10k gate to source) whose gate a 2N7002 on
IO2 pulls low. Use it on the 5 V side: the AO3401's gate is only rated to ±12 V, which leaves no margin on a 12 V rail.
Never switch the amp's ground: its RF ground also runs through the coax.

Then, on the web UI's **Tools** page, open **Amplifier power → Switch wiring** and tick *A power switch is wired to
IO2*. Leave **Active low** ticked for circuit A, and untick it for circuit B. **Use amplifier** then switches the
amp into use, and the setting is kept across restarts. *Power* below it, and *Amplifier power* on the dashboard,
show whether the amp is powered right now, and why not. Over HTTP it's `/api/amp`
([API: Output](https://mikeneiderhauser.github.io/net2rf-led/api/#/Output)).

- **Use amplifier off holds every transmission,** so the radio never sends into an unpowered amp. An unpowered amp
  is not a dummy load: depending on its circuit, much of the power can reflect back into the radio.
- **Follows RF output (on by default):** the amp is powered only while the dashboard's *RF output* is on. Untick
  *Power the amp only while RF output is on* to keep it powered with RF output off, e.g. to receive through it in
  receiver mode.
- **Idle power-down:** set *Power down after N minutes* and the amp powers down after that long without a
  transmission. 0 keeps it on.
- Whenever it's been powered down for RF output or the idle timer, the amp is powered back up 0.3 s ahead of the
  next transmission.

### Checking the switch

With the amp wired and the switch ticked, before closing the case:

| RF output | IO2 | Buck EN | Amp |
|---|---|---|---|
| Off | about 3.3 V | about 0 V | off |
| On | about 0 V | high (the buck's pull-up) | powered |

- Toggle RF output a few times: the amp follows it each time, and *Amplifier power* on the dashboard agrees.
- The 100 Ω gate resistor stays cold in both states. The gate draws no current, so a warm resistor means a fault.
- Tap and flex the case in both states: the amp must not change. Strain-relieve the wires to the buck and the FET.
- The amp is powered for a moment at every boot: IO2 is low until the firmware starts. That's expected.

### Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| The amp blinks twice a second (or in step with the status LED) | The switch isn't ticked, so IO2 is still the status LED's pin and its blink pattern switches the amp. Two blinks a second is the setup hotspot's pattern | Tick *A power switch is wired to IO2* under *Switch wiring* |
| RF output doesn't switch the amp | The switch isn't ticked, or *Power the amp only while RF output is on* is unticked | Tick both. *Power* on the Tools page shows what the firmware wants |
| The amp stays on with RF output off | EN is tied to the 12 V input (the solder jumper isn't fully cut, or got bridged again while soldering the EN wire), so the FET can't pull it low | Unpowered, measure EN to the 12 V input: it must read open |
| The gate resistor gets hot | Something connects IO2's side of the 100 Ω to EN, the 12 V side or ground: a solder bridge or a bent lead between the FET's gate and drain, or a 100 Ω fitted where the 100k pull-down goes | Unpowered: gate pad to drain pad must be open, gate pad to GND about 100k. Power off until it's fixed: the current flows through IO2 |
| The firmware says powered but the amp is off, or it comes on when the case is moved | A loose wire or cracked joint: the buck's 12 V, GND or 5 V wires, the EN wire or the FET's leads | Reflow, then strain-relieve. Measure EN while flexing each wire to find it |
| *Power: off (switched off)* and sends return 409 | *Use amplifier* is off: transmissions are held so the radio never sends into an unpowered amp | Switch it on, or untick the switch if none is wired |

## Interference with nearby audio

Speakers and audio gear close to the antenna may buzz or chirp in time with the transmissions. Their amplifiers pick
up the 433 MHz bursts and turn them into sound. It's not a fault and does no harm, but an amp makes it more likely.
If the noise is there with nothing transmitting, it's something else: a steady whine is usually the buck's
switching noise reaching the audio through a shared supply (fit the input capacitor the buck module calls for), and
noise or nearby radios jammed with no transmissions at all can mean the amp is oscillating. Never run the amp
without an antenna or a 50 Ω dummy load.

To reduce it:

- **Distance first.** Mount the antenna a few metres from speakers and their cables, and don't run audio cables
  alongside the antenna or controller cables. Pickup drops off quickly with distance.
- **Less power.** Use the lowest transmit power and amp gain that reach every device. On the bench, use a dummy load.
- **Ferrites.** Clip-on ferrite chokes (type 31 or 43 mix) on the audio cables near the speaker or its amplifier,
  with a few turns through the core.
- **Less airtime.** Fewer repeats, and *Refresh unchanged (ms)* at 0 (send on change only), mean fewer bursts.

Before a show, play audio and run a busy sequence, then listen at each speaker.
