# Radio: range, multiple controllers and the rules

## Range and power

- Mount the antenna high, clear of metal, with a view of the audience.
- Do a range walk ([SETUP.md](SETUP.md#4-check-the-range-where-the-audience-will-be)), then use the **lowest TX power** that still
  reaches the far edge.
- The CC1101 goes up to +10 dBm; the Ra-02 (SX1278) up to +17 dBm for bigger areas.
- Each update takes about 140 ms on air (a ~47 ms frame, sent 3 times by default). The controller manages roughly
  5-10 colour changes per second across all its zones, so favour bold, slow effects.
- The bracelets **hold** their last colour, so the controller only transmits when a colour changes (*Refresh
  unchanged* = 0). Re-sending unchanged colours only adds airtime and interference.

## Multiple controllers

All controllers, and both bracelet protocols, share one 433 MHz channel: the bracelets' receivers are wide enough
to hear both. Two controllers transmitting at the same moment garble each other, and a bracelet that misses an
update keeps its old colour until the next change.

- **One transmitter per area (recommended).** A single controller drives up to 16 zones and queues every update
  itself, so it never collides with itself. Add a second controller only for an area out of RF range of the first,
  or for a second bracelet protocol you can't run alongside it.
- **Listen before transmit (optional, *Bracelets & Radio*).** When controllers do overlap, each one listens for a
  clear channel before every update (2.5 ms), backs off a few ms if it hears another transmitter, and sends anyway
  after 250 ms. It needs no setup between controllers and also avoids key fobs and other 433 MHz traffic. It only
  helps when the controllers can hear each other, which is usually true since they're closer together than the
  bracelets are.
  - **Threshold:** default -75 dBm. Set it about 10 dB above the quiet reading shown on the page: a quiet
    channel read -84 to -87 dBm on the tested CC1101, so a threshold at the quiet level makes it wait on noise.
  - **Counters:** `/api/stats` shows how often it waited (`lbt_waits`) and how often it gave up and sent anyway
    (`lbt_forced`). Waiting on almost every update means the threshold is too low.
  - **Status:** off by default until it has been tested with two radios in range of each other.
- **Airtime per controller.** The dashboard's controller list shows each controller's airtime (`air`). Two
  neighbours that are both busy are competing for the channel.
- **TX jitter** adds a random delay before each update. It stops the same two controllers colliding over and over,
  but doesn't prevent collisions; listen before transmit supersedes it.

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
