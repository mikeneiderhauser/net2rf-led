#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Other controllers on the network, with their live state.
//
// Two sources feed one table, keyed by controller ID (last two MAC bytes):
//  * a UDP heartbeat (net2rf::HEARTBEAT_PORT) every controller broadcasts every 5 s on each active interface,
//    carrying its name, firmware and state; a peer is "online" while beats keep arriving (15 s = 3 missed);
//  * mDNS (_net2rf._tcp) discovery from net.cpp, which also finds controllers whose beats can't reach us
//    (older firmware, Wi-Fi client isolation); those are listed without live state.
// Offline peers stay listed (greyed out) for 10 minutes, then drop off.
namespace peers {

void begin();
void loop();  // call from the Arduino loop: sends beats, expires peers

// From the mDNS discovery task (thread-safe).
void note_mdns(const String &id, const String &name, const String &host, const String &ip, const String &fw);

// Send a beat right away (state changed), instead of waiting for the next interval.
void announce();

// This controller first, then the others: [{name, hostname, ip, firmware, id, self, online, last_seen_ms,
// via: ["udp","mdns"], state: {radio, input, output_enabled, test, zones, uptime_s}}]
void json(JsonArray out);

// Shared-alias election: true when no live controller with a lower ID (or one that uses the alias as its own
// hostname) is known. `decided` is false until we've listened long enough to know.
bool alias_should_hold(bool &decided);

}  // namespace peers
