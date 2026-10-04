# HTTP API

All endpoints are JSON. POST bodies are JSON objects. Errors return `{"ok": false, "error": "..."}` with a 4xx/5xx status.
Successful writes return `{"ok": true, "reboot": <bool>}`; when `reboot` is true the controller restarts about a second later.

## Request rules

Every state-changing `POST` must:

- send a JSON body with `Content-Type: application/json` (otherwise `415`);
- address the controller by IP or by its own name (`net2rf-xxxx`, `net2rf-xxxx.local`, `net2rf.local`);
  any other host name gets `403`, including the same name under another domain (`net2rf-xxxx.lan`), since a
  DNS-rebinding page can pick its own domain. Use the IP when your router publishes its own DNS names;
- if sent from a browser, come from the controller's own page (the `Origin` header must match).

These stop other websites from making your browser change settings or flash firmware behind your back. Scripts,
curl and Home Assistant are unaffected as long as they set the JSON content type. `/update` (multipart) is exempt from
the content-type rule but not from the other two.

## Authentication

Off by default. Once an admin password is set (`POST /api/auth`), every endpoint marked 🔒 needs HTTP Basic auth
with user `admin`:

```bash
curl -u admin:yourpassword http://net2rf-3f2a.local/api/config
```

Missing or wrong credentials return `401` with a `WWW-Authenticate: Basic` challenge (browsers show a login prompt).
After 5 wrong passwords, all attempts are refused with `429` for 30 s. The password is stored as a salted SHA-256
hash. Basic auth is not encrypted on the wire, so treat it as protection on a trusted LAN, not across the internet.
Forgotten password: hold the front-panel button 5 s (network reset), which clears it along with the network settings.

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | Web UI |
| GET | `/api/status` | Device, network and engine status (polled by the UI every second). Always open. |
| GET 🔒 | `/api/config` | Current settings (`app` + `network`, without passwords) |
| POST 🔒 | `/api/config` | Update app settings. Any subset of the fields below. Changing `radio.type` reboots. |
| POST 🔒 | `/api/network` | Update network settings, then reboot |
| GET | `/api/stats` | Compact counters for monitoring (see below). Always open. |
| GET | `/api/discover` | This controller plus every other controller it knows of, with live state (see *Controller discovery*). Always open. |
| POST | `/api/discover` | Run an mDNS search now (otherwise every 60 s; heartbeats arrive continuously) |
| POST 🔒 | `/api/auth` | `{"password": "..."}`: set or change the admin password (8-64 characters); `""` removes it |
| POST 🔒 | `/api/output` | `{"enabled": true\|false}`: global RF switch (persisted). Disabled = DDP is consumed and counted, nothing is transmitted. |
| POST 🔒 | `/api/radio` | `{"power": true\|false}`: radio chip power (persisted). `false` puts the chip to sleep: `radio.state` reads `off`, show data is still counted, and sends return 409 (`radio is shut down`). `true` re-initialises it and re-sends the current colours. |
| POST 🔒 | `/api/test` | `{"mode": "off"\|"solid"\|"cycle", "rgb": "FF0000"}`: override DDP input |
| POST 🔒 | `/api/send` | `{"zone": 0, "action": "color"\|"off"\|"fxa"\|"fxb"\|"fxc", "rgb": "FF0000"}`; `"zone": "all"` (or -1) = every bracelet: one broadcast packet on protocol 1, one per enabled zone on protocol 0. `"all"` + `"off"` behaves like `/api/all-off`. 400 for a zone that isn't saved; 409 when output is disabled. |
| POST 🔒 | `/api/all-off` | Leave test mode and switch every bracelet off. Bracelets then stay off until the input changes a colour. 409 when output is disabled. |
| POST 🔒 | `/api/raw` | `{"protocol": 1, "hex": "55000FFFFF55FF", "repeats": 3, "fix": true}`. 409 when output is disabled. |
| POST 🔒 | `/api/stats/reset` | Zero the packet counters |
| POST / GET 🔒 | `/api/wifi/scan` | Start a scan / read results |
| GET 🔒 | `/api/export` | Download settings (never includes the Wi-Fi password) |
| POST 🔒 | `/api/import` | Body = an export file; add `"include_network": true` to also import network settings. Reboots. |
| POST 🔒 | `/api/reboot` | Reboot |
| POST 🔒 | `/api/factory-reset` | Erase all settings and reboot |
| POST 🔒 | `/api/update/github` | `{"tag": "v1.2.3", "asset": "net2rf-led-1.2.3.bin"}`: the controller downloads that file from the release of the configured repository (`update.repo`) over HTTPS and flashes it, then reboots. Returns at once; progress is `device.update_job` in `/api/status` (`state`: `idle` / `downloading` / `done` / `failed`, `progress` in %, `error`). 409 while an update is running. Needs internet access. |
| POST 🔒 | `/update` | `multipart/form-data` firmware upload (`firmware.bin`). Origin and credentials are checked before anything is written to flash. Reboots when done; see *Update rollback*. |

## Settings (`/api/config` → `app`)

```json
{
  "name": "Front Yard",
  "output_enabled": true,
  "bracelets": {"protocol": 1, "mode": "pixel", "color_order": "RGB"},
  "input":     {"ddp_enabled": true, "ddp_port": 4048, "e131_enabled": false, "e131_universe": 1,
                "e131_multicast": true, "start_channel": 1, "timeout_s": 300},
  "radio":     {"type": "cc1101", "tx_power": 10, "freq_p0": 433889000, "freq_p1": 433920000,
                "repeats": 3, "off_threshold": 16, "refresh_ms": 0, "tx_jitter_ms": 0,
                "lbt_enabled": false, "lbt_threshold_dbm": -75, "power": true},
  "zones": [
    {"enabled": true, "name": "Left side", "addr": "00FF", "start": 1},
    {"enabled": true, "name": "Right side", "addr": "01FF", "start": 4}
  ]
}
```

- `bracelets.protocol`: 0 = Shenzen New Dody (433.889 MHz, 10-colour palette), 1 = LedGiftSupplier.com
  (433.920 MHz, RGB, group codes). Changing it without sending `zones` resets every zone address to the default.
- `bracelets.mode`: `pixel` (3 channels per zone: R G B in `color_order`), `dmx` (4: R, G, B, FX) or `vendor`
  (5: boot code, group, R, G, B, the LedGiftSupplier DMX transmitter's layout; protocol 1 only). In `vendor` mode a
  zone only transmits while its first channel is 85, and the group comes from its second channel.
- `zones[].addr`: protocol 0 = 4 bytes (packet bytes 0-3; `00FFFF0F` = all groups, `0002000F` = group 1,
  `0004000F` = group 2, ...); protocol 1 = group code byte + byte 6 (`00FF` = group 0, all groups; `01FF` =
  group 1). Factory default: protocol 0 with four zones, *All Zones* and *Zone 1* to *3* (groups 1-3). Zone
  *k* defaults to group *k*, and zone 0 to all groups. `zones[].start` is read-only: zone *k* starts at
  `start_channel + k × (3, 4 or 5)`.
- Transmit order: zones are sent when their colour changes, taking turns. When zones that reach the same
  bracelets change together, the broader address goes first (all groups before a single group), so the more
  specific colour lands last.
- `radio.type`: `cc1101` or `sx1278`. `tx_power` is clamped to the module's range. `refresh_ms` defaults to 0
  (send on change only): the bracelets latch, and extra airtime only adds interference.
- `radio.power`: `false` = radio chip shut down (same as `POST /api/radio`).
- `update.repo`: GitHub repository (`owner/name`) whose releases the firmware update checks and installs.
  Default `mikeneiderhauser/net2rf-led`; `""` restores the default.
- `radio.lbt_enabled` (listen before transmit, off by default): before each update the radio listens for 2.5 ms
  and only transmits if the strongest signal stayed below `lbt_threshold_dbm` (-120..-30, default -75).
  Otherwise it backs off a random 3-15 ms and listens again, for at most 250 ms, then sends anyway. Set the
  threshold about 10 dB above the quiet reading shown on *Bracelets & Radio* (`radio.lbt.last_rssi_dbm` in
  `/api/status`). See [RF.md](RF.md#multiple-controllers).
- `input.timeout_s`: blank the bracelets after this long without input. 0 = hold the last colour. On protocol 1 the
  blank is one broadcast "off" to every group (so it also reaches bracelets in groups no zone uses).
- Test mode (`/api/test`) on protocol 1 also uses one broadcast per colour change rather than one packet per zone.
- `input.e131_*`: E1.31 listens on UDP 5568 for one universe (slots 1-512 = channels 1-512); with multicast it also
  joins 239.255.*hi*.*lo* for that universe. Off by default so a shared universe can't drive the bracelets by accident.

## Stats (`/api/stats`)

```json
{
  "uptime_s": 86400, "free_heap": 182344, "name": "Front Yard", "output_enabled": true,
  "radio": "ready", "test_mode": "off",
  "input":  {"packets": 1204233, "frames": 1204233, "malformed": 0, "ignored": 2, "bytes": 19267728, "fps": 40.0,
             "seen": true, "timed_out": false, "age_ms": 25, "source": "192.168.1.20", "timeout_s": 300},
  "output": {"enabled": true, "suppressed": 0, "updates": 81234, "frames": 243702, "errors": 0, "manual": 4,
             "airtime_pct": 38.5, "lbt_checks": 81234, "lbt_waits": 120, "lbt_forced": 2, "lbt_wait_ms": 940},
  "zones":  [{"name": "Left side", "enabled": true, "rgb": "FF0000", "updates": 40617, "last_tx_age_ms": 120}]
}
```

`input.packets`/`frames` count every valid DDP or E1.31 packet and frame (`ddp_packets` / `e131_packets` split
them; `last_source` is the protocol of the last one, `source` its sender's IP); `output.suppressed` counts zone updates that
were consumed while output was disabled. With listen before transmit on, `lbt_checks` counts updates that
listened first, `lbt_waits` those that found the channel busy, `lbt_forced` those sent anyway after 250 ms, and
`lbt_wait_ms` the total delay. A steadily rising `lbt_forced` means the channel is saturated: lower the load
(fewer updates, fewer repeats) or the number of transmitters. `POST /api/stats/reset` zeroes the counters.

## Controller discovery

Controllers find each other two ways:

- **Heartbeat (live state):** every 5 s each controller broadcasts a small JSON packet on **UDP 4049** on every
  active interface (Ethernet, Wi-Fi, setup AP):
  ```json
  {"p":"net2rf","v":1,"id":"24DB","name":"Front Yard","host":"net2rf-24db","fw":"1.2.0",
   "radio":"ready","input":"live","out":true,"test":false,"zones":4,"air":12,"up":86400}
  ```
  `input` is `live` (data in the last 3 s), `idle`, `timed_out` or `none`; `air` is the controller's RF
  airtime over the last 10 s (%). A controller that has just started
  adds `"hello":true`; the others answer at once (broadcast, plus directly to the sender, which also works
  across subnets). A beat also goes out immediately when the radio, input, output or test state changes. The
  sender's IP is taken from the packet, not the payload.
- **mDNS** (`_net2rf._tcp`, every 60 s): finds controllers whose heartbeats can't reach this one, such as older
  firmware or Wi-Fi with client isolation. They're listed without live state.

Broadcasts and mDNS stay on the local subnet; controllers on different VLANs don't see each other.

`GET /api/discover`:

```json
{"running": false, "alias": "net2rf.local", "alias_claimed": true,
 "controllers": [
   {"name": "Front Yard", "hostname": "net2rf-24db", "ip": "192.168.1.60", "firmware": "1.2.0", "id": "24DB",
    "self": true, "online": true, "last_seen_ms": 0, "via": ["self"],
    "state": {"radio": "ready", "input": "live", "output_enabled": true, "test": false, "zones": 4,
              "airtime_pct": 12, "uptime_s": 86400}},
   {"name": "Back Yard", "hostname": "net2rf-81c4", "ip": "192.168.1.61", "firmware": "1.2.0", "id": "81C4",
    "self": false, "online": false, "last_seen_ms": 42000, "via": ["udp", "mdns"], "state": {"...": "last known"}}]}
```

- `online`: a heartbeat arrived in the last 15 s (three missed beats = offline). Offline controllers stay
  listed for 10 minutes, then drop off.
- `via`: `udp` (heartbeat), `mdns`, or `self`. Entries seen only via mDNS have no `state`.
- `alias_claimed`: this controller currently answers `net2rf.local`. Exactly one controller does: the one with
  the lowest ID among those it can see, re-elected as controllers come and go.

`tools/peer_sim.py` simulates other controllers for testing.

## Update rollback

A newly uploaded firmware boots as *pending*. It is confirmed once the controller has been reachable (web UI up,
with a network connection or the setup AP) for 30 s. If it crashes or reboots before that, the bootloader
returns to the previous firmware on the next boot; if it never becomes reachable within 3 minutes, it rolls
itself back. `/api/status` reports `device.update_pending` and `device.update_rolled_back`. This applies to
uploads and to installs from GitHub alike.

## Network (`/api/network`)

```json
{"hostname": "net2rf-3f2a", "eth_enabled": true, "wifi_ssid": "HomeNet", "wifi_pass": "…",
 "dhcp": false, "ip": "192.168.1.60", "subnet": "255.255.255.0", "gateway": "192.168.1.1", "dns": "",
 "ap_mode": "no_connection", "ap_pass": "net2rf1234"}
```

Omit `wifi_pass` / `ap_pass` to keep the current ones. `ap_mode`: `no_connection` (default), `always`, `never`.
