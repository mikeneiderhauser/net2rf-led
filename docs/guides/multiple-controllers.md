# Using several controllers

When one controller can't reach the whole area, you can add more and run them as a **coverage group**: one show,
several transmitters, kept in sync over the network.

> **Status:** tested with two controllers over Wi-Fi. Larger groups haven't been tried yet, so please
> [report](../../CONTRIBUTING.md) how yours goes.

This isn't the same as FPP's *MultiSync*, which keeps show players in time. A coverage group runs between Net2RF
controllers, and xLights or FPP only ever talks to one of them.

## More controllers, or an amplifier?

Try better placement first: in testing, mounting the antenna high and away from other objects made the most
difference. If you still need more coverage:

| | Coverage group | [External amplifier](../build/amplifier.md) |
|---|---|---|
| **Good for** | Areas with obstacles, or spread out in different directions | One open area, from one good antenna position |
| **Hardware** | A controller per location, each with a network connection | One amp and buck on one controller |
| **Colour changes** | Fewer: about 8 a second with 2 controllers, 4.5 with 4 | No fewer than a single controller |
| **Transmit power** | Each controller stays at its normal power | Raised: check the [licensing rules](../build/amplifier.md#licensing-and-legal) |
| **Status** | Tested with two controllers over Wi-Fi | Range tested |

To reach further with the fewest compromises, one well-placed controller is the better choice: a group spends its
airtime on coverage, not speed. Choose a group when a single position can't see the whole area.

## How it works

One controller is the **master**. It receives the show, and shares each colour change out to the others, called
**remotes**. They take turns on air in short time slots, so a device between two controllers hears each one cleanly.
A device only needs to hear one of them. If they all transmitted at once, their signals would interfere.

The remotes keep their clocks aligned with the master's over the network. Over Wi-Fi the error measured 3-8 ms, and
Ethernet should do better.

## Setting one up

Before you start, connect every controller to the same network (the same subnet), and use Ethernet where you can.
Update them all to the same firmware.

1. **Place the controllers** so that between them they reach the whole area.
2. **On every controller:** open *Devices & Radio → Coverage group* and tick **Sync enabled**.
3. **On one controller:** make it the **master**. It's the only one that needs zones.
4. **Set *Copies per controller*** on the master. Start at 1 and raise it only if devices miss changes.
5. **Point xLights or FPP at the master** only. Set it up as in the [show setup guide](show-setup.md). The remotes need
   no input and no zones, and they find the master by themselves.

## Checking it

- The master's Dashboard shows a **Coverage group** card listing each remote and how well its clock is synced.
  **Synced** is what you want. A remote whose clock error is above 10 ms is left out until it improves.
- Each remote's Dashboard shows the master it follows, and its OLED shows the group on the status page.
- Run **Test mode → Cycle** on the master and walk the area with a device, as in the
  [range check](show-setup.md#4-check-the-range-where-the-audience-will-be).
- If the nearest controller drowns out the others, turn down its TX power. Each remote keeps its own setting.

On the card, **late** counts changes that reached a remote after its slot. The remote skips those rather than risk
colliding, and the other controllers still send theirs. Over Wi-Fi, 2-4% of changes were late during a real show and test runs
of up to 7 changes a second, and more during bursts such as *All off*, when many zones change at once. Ethernet
should do better.

## Sequencing for a group

Every controller sends its own copy of each change, so changes take longer. With 1 copy each:

| Controllers | Time per change | Changes per second |
|---|---|---|
| 1 | ~50 ms | ~20 |
| 2 | ~120 ms | ~8 |
| 4 | ~220 ms | ~4.5 |
| 9 (the maximum) | ~480 ms | ~2 |

- Favour held colours and slow changes, and keep the number of different colours changing at once small.
- Use fewer copies as you add controllers: the copies from the other controllers stand in for repeats.
- A device that misses a change catches up about a second after the colour stops changing, when the master sends it
  once more.

## Good to know

- **One master only.** Two masters at once is a mistake, and shows in red on every controller's Dashboard and OLED.
- **No master:** remotes stay silent, though the test buttons on their own pages still work.
- **No remotes:** the master transmits on its own, like a standalone controller.
- **Receiver mode:** a controller in receiver mode ignores the group settings.

The full details are in the [radio notes](../reference/radio.md#coverage-group), and the settings in the
[HTTP API](../reference/api.md#settings-apiconfig--app).
