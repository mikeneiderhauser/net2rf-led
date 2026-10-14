# Radio: range, multiple controllers and the rules

## Range and power

- Mount the antenna high, clear of metal, with a view of the audience.
- Do a range walk ([SETUP.md](SETUP.md#4-check-the-range-where-the-audience-will-be)), then use the **lowest TX power** that still
  reaches the far edge.
- The CC1101 goes up to +10 dBm; the Ra-02 (SX1278) up to +17 dBm for bigger areas.
- Each update takes about 140 ms on air (a ~47 ms frame, sent 3 times by default). The controller manages roughly
  5-10 colour changes per second across all its zones, so favour bold, slow effects.
- On protocol 0 with the base layer on, zones that change to the same colour share one transmission (the address
  is a group mask), so airtime grows with the number of colours on screen, not the number of zones.
- The bracelets **hold** their last colour, so the controller only transmits when a colour changes (*Refresh
  unchanged* = 0). Re-sending unchanged colours only adds airtime and interference.

## Multiple controllers

All controllers, and both bracelet protocols, share one 433 MHz channel: the bracelets' receivers are wide enough
to hear both. Two controllers transmitting at the same moment garble each other, and a bracelet that misses an
update keeps its old colour until the next change.

- **One transmitter per area (recommended).** A single controller drives up to 16 zones and queues every update
  itself, so it never collides with itself. Add a second controller only for an area out of RF range of the first,
  or for a second bracelet protocol you can't run alongside it.
- **Listen before transmit (optional, *Devices & Radio*).** When controllers do overlap, each one listens for a
  clear channel before every update (2.5 ms), backs off a few ms if it hears another transmitter, and sends anyway
  after 250 ms. It needs no setup between controllers and also avoids key fobs and other 433 MHz traffic. It only
  helps when the controllers can hear each other, which is usually true since they're closer together than the
  bracelets are.
  - **Threshold:** default -75 dBm. Set it about 10 dB above the quiet reading shown on the page: a quiet
    channel read -84 to -87 dBm on the tested CC1101, so a threshold at the quiet level makes it wait on noise.
  - **Counters:** `/api/stats` shows how often it waited (`lbt_waits`) and how often it gave up and sent anyway
    (`lbt_forced`). Waiting on almost every update means the threshold is too low.
  - **Status:** off by default until it has been tested with two radios in range of each other.
- **Coverage group (several controllers, one show).** See [below](#coverage-group): the controllers take turns
  in time slots scheduled by one master, instead of competing for the channel.
- **Airtime per controller.** The dashboard's controller list shows each controller's airtime (`air`). Two
  neighbours that are both busy are competing for the channel.
- **TX jitter** adds a random delay before each update. It stops the same two controllers colliding over and over,
  but doesn't prevent collisions; listen before transmit supersedes it.

## Coverage group

When one controller can't reach the whole yard, place a few around it and make them a **coverage group**: one
show, several transmitters, taking turns on air.

- **Set up (*Devices & Radio* → *Coverage group*).** Tick *Sync enabled* on every controller in the group. Make one
  of them the **master** and point xLights at it; leave the others as **remotes**. Remotes need no input, no zones
  of their own and nothing else: they find the master from its heartbeat, and the master finds them from theirs.
  *Copies per controller* (1-3, master only) applies to every controller in the group.
- **What happens.** Every colour change the master would transmit is shared out: the master plans one slot per
  controller (itself and every synced remote), back to back, each `copies` frames long plus a guard gap, starting
  a little ahead of now. It sends each remote its slot start, the protocol, frequency and packet as a small
  unicast UDP message (port 4050), transmits its own slot, and waits for the plan to end before the next change.
  The order rotates by one each time, so no controller is always first. Newest state still wins: changes that
  arrive while a plan is on air are merged into the next one.
- **Why slots, not everyone at once.** Two transmitters sending the same frame at the same moment don't add up:
  their carriers beat against each other and a device between them can hear neither. Taking turns means every
  device hears each controller cleanly; one in range of any of them gets the change. Listen before transmit and
  TX jitter are skipped inside a slot (the slots already keep the controllers apart).
- **Clock sync.** Each remote sends the master a timestamped request every second (four times a second while it
  is joining) and keeps an NTP-style estimate of the master's clock, trusting the quickest round trips. Its error
  estimate (half the best round trip plus drift) sets the guard gap and lead time; a remote whose error is above
  10 ms is left out of the plans until it improves. Wired Ethernet syncs to well under a millisecond; Wi-Fi to a
  few ms.
- **Airtime.** One change costs `controllers × copies` frames of airtime, so a group of three at 2 copies takes
  about 300 ms per change. Use fewer copies as you add controllers: the copies from the other controllers stand in
  for the repeats a single controller needs. Final states (All off, input-timeout blanking) get one extra copy,
  and the master sends each zone's last colour once more about a second after it stops changing, so a device that
  missed the change catches up.
- **Network.** Only the existing heartbeat is broadcast. Group traffic is unicast and small: one 40-byte message
  per remote per change, and one sync exchange per remote per second, so it is harmless next to the DDP traffic
  going to pixel controllers on 10/100 links.
- **Without a master** a remote stays silent (its own web page's test buttons still work). A master with no synced
  remote transmits on its own, like a standalone controller, with its normal repeats.
- **One master.** Every controller tracks the masters it hears; two at once is a mistake (both would schedule the
  air) and is shown in red on every controller's dashboard and OLED. Remotes keep following the master they
  already had, or the one with the lowest MAC address.
- Each remote keeps its own TX power: turn down the one nearest the audience if it drowns the others out.

## Car key fobs

A strong 433 MHz transmitter can stop some car key fobs from working nearby (reported by users of the vendor
transmitter). Use the lowest TX power that reaches your audience and a short antenna, and keep the bracelets for
highlights rather than constant effects. The RF output switch, or vendor mode's channel 1, turns transmission off
between songs.

## Legal

The controller transmits exactly what it is sent, at the TX power you configure; it does not enforce any
regulatory limit. Staying within local rules is the responsibility of the operator (whoever configures the
controller and sends it DDP / E1.31).

433 MHz transmission is regulated and the rules differ by country. In the US, 433.92 MHz falls under FCC Part 15,
which restricts field strength and how often and how long a device may transmit; in Europe, 433.05-434.79 MHz
short-range devices are limited to 10 mW ERP and a duty cycle of 10%. Check the rules where you operate, use the
lowest TX power that covers your area, and keep transmissions to what the show needs. The radio modules this
project uses are not certified as part of this device.
