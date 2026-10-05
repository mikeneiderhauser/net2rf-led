#include "peers.h"

#include <AsyncUDP.h>
#include <freertos/semphr.h>
#include <vector>

#include <fpp_ping.h>

#include "config.h"
#include "engine.h"
#include "heartbeat.h"
#include "net.h"

namespace peers {

static const uint32_t BEAT_INTERVAL_MS = 5000;
static const uint32_t ONLINE_FOR_MS = 15000;          // 3 missed beats -> offline
static const uint32_t FORGET_AFTER_MS = 10 * 60000;   // offline (or mDNS-only and gone) this long -> dropped
static const uint32_t MDNS_FRESH_MS = 3 * 60000;      // mDNS sighting counts for the alias election this long
static const uint32_t ELECTION_SETTLE_MS = 7000;      // listen this long after the network is up before deciding
static const uint32_t MIN_GAP_MS = 500;               // rate limit for change-triggered beats
static const uint8_t HELLO_BEATS = 2;                 // first beats ask everyone to answer
static const size_t MAX_PEERS = 32;

struct Peer {
    String id, name, host, ip, fw, radio, input;
    bool out = false, test = false;
    uint8_t zones = 0, air = 0;
    uint32_t up = 0;
    uint32_t last_udp_ms = 0;   // 0 = never heard a beat
    uint32_t last_mdns_ms = 0;  // 0 = never seen via mDNS
};

static AsyncUDP s_udp;
static bool s_listening = false;
static SemaphoreHandle_t s_lock = nullptr;
static std::vector<Peer> s_peers;
static uint32_t s_last_beat_ms = 0;
static uint32_t s_net_up_ms = 0;  // when we first had an address (election settle timer)
static uint8_t s_hellos_left = HELLO_BEATS;
static volatile bool s_beat_now = false;
static volatile uint32_t s_reply_to = 0;  // also answer this sender's "hello" directly (e.g. from another subnet)
static String s_last_state;  // radio/input/out/test of the last beat, to send on change

struct Guard {
    Guard() { xSemaphoreTake(s_lock, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_lock); }
};

static Peer *find(const String &id) {
    for (Peer &p : s_peers)
        if (p.id.equalsIgnoreCase(id))
            return &p;
    return nullptr;
}

// Caller holds the lock. Returns nullptr when the table is full of peers we've heard from more recently.
static Peer *upsert(const String &id, uint32_t now) {
    if (Peer *p = find(id))
        return p;
    if (s_peers.size() >= MAX_PEERS) {
        // replace the stalest entry
        size_t oldest = 0;
        uint32_t oldest_age = 0;
        for (size_t i = 0; i < s_peers.size(); i++) {
            uint32_t seen = max(s_peers[i].last_udp_ms, s_peers[i].last_mdns_ms);
            if (now - seen >= oldest_age) {
                oldest_age = now - seen;
                oldest = i;
            }
        }
        s_peers.erase(s_peers.begin() + oldest);
    }
    Peer p;
    p.id = id;
    p.id.toUpperCase();
    s_peers.push_back(p);
    return &s_peers.back();
}

// ---- our own state ------------------------------------------------------------------------------

static void own_heartbeat(net2rf::Heartbeat &h) {
    memset(&h, 0, sizeof(h));
    EngineSnapshot s = g_engine.snapshot();
    {
        StateLock lock;
        strlcpy(h.name, g_app.name, sizeof(h.name));
        strlcpy(h.host, g_net.hostname, sizeof(h.host));
    }
    strlcpy(h.id, device_suffix().c_str(), sizeof(h.id));
    strlcpy(h.fw, FW_VERSION, sizeof(h.fw));
    strlcpy(h.radio, radio_state_name(s.radio_state), sizeof(h.radio));
    const char *input = !s.input_seen ? "none" : s.timed_out ? "timed_out" : s.input_age_ms < 3000 ? "live" : "idle";
    strlcpy(h.input, input, sizeof(h.input));
    h.out = s.output_enabled;
    h.test = s.test_active;
    h.zones = s.num_zones;
    h.air = (uint8_t) min(100.0f, s.airtime_pct + 0.5f);
    h.up = millis() / 1000;
}

static String state_key(const net2rf::Heartbeat &h) {
    return String(h.radio) + "|" + h.input + "|" + h.out + "|" + h.test + "|" + h.zones;
}

static void send_beat() {
    net2rf::Heartbeat h;
    own_heartbeat(h);
    h.hello = s_hellos_left > 0;
    if (s_hellos_left)
        s_hellos_left--;
    char buf[net2rf::HEARTBEAT_MAX];
    size_t n = net2rf::encode_heartbeat(h, buf, sizeof(buf));
    if (n == 0)
        return;
    // Broadcast on each interface that has an address (Ethernet and Wi-Fi are rarely both up; the setup AP may be).
    if (net::eth_up())
        s_udp.broadcastTo((uint8_t *) buf, n, net2rf::HEARTBEAT_PORT, TCPIP_ADAPTER_IF_ETH);
    if (net::wifi_up())
        s_udp.broadcastTo((uint8_t *) buf, n, net2rf::HEARTBEAT_PORT, TCPIP_ADAPTER_IF_STA);
    if (net::ap_active())
        s_udp.broadcastTo((uint8_t *) buf, n, net2rf::HEARTBEAT_PORT, TCPIP_ADAPTER_IF_AP);
    uint32_t reply = s_reply_to;
    if (reply) {
        s_reply_to = 0;
        s_udp.writeTo((const uint8_t *) buf, n, IPAddress(reply), net2rf::HEARTBEAT_PORT);
    }
    s_last_state = state_key(h);
}

// ---- FPP discovery ------------------------------------------------------------------------------
// Falcon Player and xLights look for controllers with FPP's "discover" ping (UDP 32320). Answering it makes
// this controller show up in FPP's MultiSync list and in xLights' controller discovery. We only speak when
// asked, plus once when the network comes up; nothing here takes part in MultiSync playback.

static AsyncUDP s_fpp;
static bool s_fpp_listening = false;
static volatile bool s_fpp_send = false;       // a ping is owed
static volatile uint32_t s_fpp_reply_to = 0;   // ... also directly to this address (discover from another subnet)
static uint32_t s_fpp_last_ms = 0;

static void send_fpp_ping() {
    char hostname[sizeof(g_net.hostname)];
    uint16_t channels;
    {
        StateLock lock;
        strlcpy(hostname, g_net.hostname, sizeof(hostname));
        channels = (uint16_t) g_app.num_zones * zone_width(g_app);
    }
    IPAddress me = net::ip();
    const uint8_t ip[4] = {me[0], me[1], me[2], me[3]};
    uint8_t buf[net2rf::FPP_PING_LEN];
    size_t n = net2rf::fpp_build_ping(buf, ip, hostname, FW_VERSION, channels);
    IPAddress group(net2rf::FPP_MULTICAST[0], net2rf::FPP_MULTICAST[1], net2rf::FPP_MULTICAST[2], net2rf::FPP_MULTICAST[3]);
    if (net::eth_up())
        s_fpp.writeTo(buf, n, group, net2rf::FPP_PORT, TCPIP_ADAPTER_IF_ETH);
    if (net::wifi_up())
        s_fpp.writeTo(buf, n, group, net2rf::FPP_PORT, TCPIP_ADAPTER_IF_STA);
    uint32_t reply = s_fpp_reply_to;
    if (reply) {
        s_fpp_reply_to = 0;
        s_fpp.writeTo(buf, n, IPAddress(reply), net2rf::FPP_PORT);
    }
}

static void fpp_loop(uint32_t now) {
    if (!net::connected())
        return;  // not on the setup AP: there is no player there to find us
    if (!s_fpp_listening) {
        IPAddress group(net2rf::FPP_MULTICAST[0], net2rf::FPP_MULTICAST[1], net2rf::FPP_MULTICAST[2], net2rf::FPP_MULTICAST[3]);
        if (s_fpp.listenMulticast(group, net2rf::FPP_PORT)) {
            s_fpp.onPacket([](AsyncUDPPacket &packet) {
                if (!net2rf::fpp_is_discover(packet.data(), packet.length()))
                    return;
                s_fpp_reply_to = (uint32_t) packet.remoteIP();
                s_fpp_send = true;
            });
            s_fpp_listening = true;
            s_fpp_send = true;  // announce ourselves once
            log_i("FPP discovery on UDP %u", net2rf::FPP_PORT);
        }
        return;
    }
    // At most one answer a second, however many discover packets arrive (xLights sends several at once).
    if (s_fpp_send && (s_fpp_last_ms == 0 || now - s_fpp_last_ms >= 1000)) {
        s_fpp_send = false;
        s_fpp_last_ms = now | 1;
        send_fpp_ping();
    }
}

// ---- receiving ----------------------------------------------------------------------------------

static void on_packet(AsyncUDPPacket &packet) {
    net2rf::Heartbeat h;
    if (!net2rf::decode_heartbeat(packet.data(), packet.length(), h))
        return;
    if (device_suffix().equalsIgnoreCase(h.id))
        return;  // our own broadcast looped back
    uint32_t now = millis();
    {
        Guard g;
        Peer *p = upsert(h.id, now);
        p->name = h.name;
        p->host = h.host;
        p->ip = packet.remoteIP().toString();
        p->fw = h.fw;
        p->radio = h.radio;
        p->input = h.input;
        p->out = h.out;
        p->test = h.test;
        p->zones = h.zones;
        p->air = h.air;
        p->up = h.up;
        p->last_udp_ms = now | 1;
    }
    if (h.hello) {
        s_beat_now = true;  // a controller just started: let it see us immediately
        s_reply_to = (uint32_t) packet.remoteIP();
    }
}

// ---- public ------------------------------------------------------------------------------------

void begin() {
    s_lock = xSemaphoreCreateMutex();
}

void note_mdns(const String &id, const String &name, const String &host, const String &ip, const String &fw) {
    if (id.isEmpty() || id.length() > 6 || device_suffix().equalsIgnoreCase(id))
        return;
    String h = host;
    if (h.endsWith(".local"))
        h.remove(h.length() - 6);
    uint32_t now = millis();
    Guard g;
    Peer *p = upsert(id, now);
    if (!p->last_udp_ms || now - p->last_udp_ms >= ONLINE_FOR_MS) {
        // the heartbeat is fresher when we have one; otherwise take what mDNS says
        p->name = name;
        p->host = h;
        p->ip = ip;
        p->fw = fw;
    }
    p->last_mdns_ms = now | 1;
}

void announce() { s_beat_now = true; }

void loop() {
    uint32_t now = millis();
    fpp_loop(now);
    bool have_net = net::connected() || net::ap_active();
    if (have_net && !s_listening) {
        if (s_udp.listen(net2rf::HEARTBEAT_PORT)) {
            s_udp.onPacket(on_packet);
            s_listening = true;
            log_i("Controller heartbeat on UDP %u", net2rf::HEARTBEAT_PORT);
        }
    }
    if (!have_net) {
        s_net_up_ms = 0;
        return;
    }
    if (!s_net_up_ms)
        s_net_up_ms = now | 1;

    // send: on the interval, when asked (hello / state change), or when our state changed
    bool due = now - s_last_beat_ms >= BEAT_INTERVAL_MS || s_last_beat_ms == 0;
    static uint32_t last_state_check = 0;
    if (!due && now - s_last_beat_ms >= MIN_GAP_MS) {
        if (s_beat_now) {
            due = true;
        } else if (now - last_state_check >= 250) {
            last_state_check = now;
            net2rf::Heartbeat h;
            own_heartbeat(h);
            due = state_key(h) != s_last_state;
        }
    }
    if (due && s_listening) {
        s_beat_now = false;
        s_last_beat_ms = now;
        send_beat();
    }

    // forget peers gone for a long time
    static uint32_t last_sweep = 0;
    if (now - last_sweep > 5000) {
        last_sweep = now;
        Guard g;
        for (size_t i = 0; i < s_peers.size();) {
            const Peer &p = s_peers[i];
            uint32_t seen = max(p.last_udp_ms, p.last_mdns_ms);
            if (now - seen > FORGET_AFTER_MS)
                s_peers.erase(s_peers.begin() + i);
            else
                i++;
        }
    }
}

static void state_json(JsonObject st, const String &radio, const String &input, bool out, bool test, uint8_t zones,
                       uint8_t air, uint32_t up) {
    st["radio"] = radio;
    st["input"] = input;
    st["output_enabled"] = out;
    st["test"] = test;
    st["zones"] = zones;
    st["airtime_pct"] = air;
    st["uptime_s"] = up;
}

void json(JsonArray arr) {
    uint32_t now = millis();
    // this controller first, with the same fields
    net2rf::Heartbeat me;
    own_heartbeat(me);
    JsonObject self = arr.add<JsonObject>();
    self["name"] = me.name;
    self["hostname"] = me.host;
    self["ip"] = net::ip().toString();
    self["firmware"] = me.fw;
    self["id"] = me.id;
    self["self"] = true;
    self["online"] = true;
    self["last_seen_ms"] = 0;
    self["via"].to<JsonArray>().add("self");
    state_json(self["state"].to<JsonObject>(), me.radio, me.input, me.out, me.test, me.zones, me.air, me.up);

    Guard g;
    for (const Peer &p : s_peers) {
        JsonObject j = arr.add<JsonObject>();
        j["name"] = p.name;
        j["hostname"] = p.host;
        j["ip"] = p.ip;
        j["firmware"] = p.fw;
        j["id"] = p.id;
        j["self"] = false;
        bool online = p.last_udp_ms && now - p.last_udp_ms < ONLINE_FOR_MS;
        j["online"] = online;
        uint32_t seen = max(p.last_udp_ms, p.last_mdns_ms);
        j["last_seen_ms"] = now - seen;
        JsonArray via = j["via"].to<JsonArray>();
        if (p.last_udp_ms)
            via.add("udp");
        if (p.last_mdns_ms)
            via.add("mdns");
        if (p.last_udp_ms)  // live state only comes with the heartbeat
            state_json(j["state"].to<JsonObject>(), p.radio, p.input, p.out, p.test, p.zones, p.air,
                       p.up + (now - p.last_udp_ms) / 1000);
    }
}

bool alias_should_hold(bool &decided) {
    uint32_t now = millis();
    decided = s_net_up_ms && now - s_net_up_ms >= ELECTION_SETTLE_MS;
    String me = device_suffix();
    Guard g;
    for (const Peer &p : s_peers) {
        bool live = (p.last_udp_ms && now - p.last_udp_ms < ONLINE_FOR_MS) ||
                    (!p.last_udp_ms && p.last_mdns_ms && now - p.last_mdns_ms < MDNS_FRESH_MS);
        if (!live)
            continue;
        if (p.host.equalsIgnoreCase(net::SHARED_ALIAS) || p.id < me)
            return false;
    }
    return true;
}

}  // namespace peers
