#include "group.h"

#include <AsyncUDP.h>
#include <esp_timer.h>
#include <freertos/semphr.h>

#include "config.h"
#include "engine.h"
#include "net.h"

using namespace n2rgroup;

namespace group {

namespace {

AsyncUDP s_udp;
bool s_listening = false;
SemaphoreHandle_t s_mutex = nullptr;
uint8_t s_self_mac[6];

struct Lock {
    Lock() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(s_mutex); }
};

enum class Role : uint8_t { OFF, MASTER, REMOTE };
Role s_role = Role::OFF;

// Both roles: masters seen in heartbeats (a remote follows one; more than one alive is reported everywhere).
MasterTracker s_masters;

// Master
Roster s_roster;
uint32_t s_plan_seq = 0, s_rotation = 0, s_plans = 0;
struct LastPlan {
    bool valid;
    uint32_t at_ms;
    uint8_t count, copies;
    uint32_t guard_us, lead_us, length_us;
    int8_t order[MAX_MEMBERS];
} s_last{};

// Remote
bool s_have_master = false;
uint8_t s_master_mac[6];
uint32_t s_master_ip = 0;
char s_master_name[24] = "";
ClockFilter s_clock;
RemoteQueue s_queue;
uint32_t s_sync_seq = 0, s_last_sync_ms = 0, s_last_ack_ms = 0, s_last_tx_ms = 0;

int64_t now_us() { return esp_timer_get_time(); }

void send_to(const uint8_t *buf, size_t n, uint32_t ip, uint16_t port) {
    if (n && ip)
        s_udp.writeTo(buf, n, IPAddress(ip), port);
}

void reset_role_state() {
    s_roster.clear();
    s_have_master = false;
    s_clock.reset();
    s_queue.clear();
    s_last.valid = false;
}

void on_packet(AsyncUDPPacket &packet) {
    int64_t t_rx = now_us();
    const uint8_t *data = packet.data();
    size_t len = packet.length();
    uint32_t ip = (uint32_t) packet.remoteIP();
    switch (message_type(data, len)) {
        case MSG_SYNC: {
            SyncMsg m;
            if (!decode(data, len, m))
                return;
            SyncAck a{};
            {
                Lock lock;
                if (s_role != Role::MASTER)
                    return;
                s_roster.on_sync(m, ip, millis());
            }
            memcpy(a.mac, s_self_mac, 6);
            a.seq = m.seq;
            a.t1 = m.t1;
            a.t2 = t_rx;
            uint8_t buf[64];
            a.t3 = now_us();
            send_to(buf, encode(a, buf, sizeof(buf)), ip, packet.remotePort());
            break;
        }
        case MSG_SYNC_ACK: {
            SyncAck a;
            if (!decode(data, len, a))
                return;
            Lock lock;
            if (s_role != Role::REMOTE || !s_have_master || mac_cmp(a.mac, s_master_mac) != 0)
                return;
            s_clock.add(a.t1, a.t2, a.t3, t_rx);
            s_last_ack_ms = millis();
            break;
        }
        case MSG_TX: {
            TxMsg m;
            if (!decode(data, len, m))
                return;
            Lock lock;
            if (s_role != Role::REMOTE || !s_have_master || mac_cmp(m.mac, s_master_mac) != 0)
                return;
            if (!s_clock.valid()) {
                s_queue.late++;
                return;
            }
            int64_t local = s_clock.to_local(m.start_us);
            if (local - t_rx > 2000000)  // more than 2 s ahead: a bad clock sample, not a real slot
                return;
            s_queue.add(m, local, t_rx);
            s_last_tx_ms = millis();
            break;
        }
        default:
            break;
    }
}

Role wanted_role() {
    StateLock lock;
    return sync_is_master(g_app) ? Role::MASTER : sync_is_remote(g_app) ? Role::REMOTE : Role::OFF;
}

void remote_tick(uint32_t now) {
    SyncMsg m{};
    uint32_t ip;
    {
        Lock lock;
        // Follow the master we already follow while it is alive, else the lowest MAC.
        const MasterTracker::Seen *best = s_masters.choose(s_have_master ? s_master_mac : nullptr, now);
        if (!best) {
            if (s_have_master)
                log_i("Coverage group: master lost");
            s_have_master = false;
            s_queue.clear();
            return;
        }
        if (!s_have_master || mac_cmp(best->mac, s_master_mac) != 0) {
            memcpy(s_master_mac, best->mac, 6);
            s_have_master = true;
            s_clock.reset();
            s_queue.clear();
            log_i("Coverage group: following master %s", best->name);
        }
        s_master_ip = best->ip;
        strlcpy(s_master_name, best->name, sizeof(s_master_name));
        uint32_t interval = s_clock.samples() < FAST_SAMPLES ? SYNC_FAST_INTERVAL_MS : SYNC_INTERVAL_MS;
        if (now - s_last_sync_ms < interval)
            return;
        s_last_sync_ms = now;
        memcpy(m.mac, s_self_mac, 6);
        m.seq = ++s_sync_seq;
        m.error_us = s_clock.valid() ? s_clock.error_us(now_us()) : 0;
        m.on_time = s_queue.on_time;
        m.late = s_queue.late;
        m.missed = s_queue.missed;
        ip = s_master_ip;
    }
    {
        StateLock lock;
        strlcpy(m.name, g_app.name, sizeof(m.name));
    }
    uint8_t buf[96];
    m.t1 = now_us();
    send_to(buf, encode(m, buf, sizeof(buf)), ip, GROUP_PORT);
}

}  // namespace

void begin() {
    s_mutex = xSemaphoreCreateMutex();
    device_mac(s_self_mac);
}

void loop() {
    uint32_t now = millis();
    Role want = wanted_role();
    if (want != s_role) {
        Lock lock;
        log_i("Coverage group: %s", want == Role::MASTER ? "master" : want == Role::REMOTE ? "remote" : "off");
        s_role = want;
        reset_role_state();
    }
    bool listen = s_role != Role::OFF && net::connected();
    if (listen && !s_listening) {
        if (s_udp.listen(GROUP_PORT)) {
            s_udp.onPacket(on_packet);
            s_listening = true;
            log_i("Coverage group on UDP %u", GROUP_PORT);
        }
    } else if (!listen && s_listening) {
        s_udp.close();
        s_listening = false;
    }
    if (!s_listening)
        return;
    if (s_role == Role::REMOTE) {
        remote_tick(now);
    } else if (s_role == Role::MASTER) {
        Lock lock;
        s_roster.expire(now);
    }
}

void on_heartbeat(const net2rf::Heartbeat &h, uint32_t src_ip) {
    uint8_t mac[6];
    if (!mac_from_hex(h.mac, mac) || mac_cmp(mac, s_self_mac) == 0)
        return;
    uint32_t now = millis();
    Lock lock;
    if (h.sync && h.master)
        s_masters.on_master_beat(mac, src_ip, h.name, now);
    else
        s_masters.not_master(mac);
    if (s_role == Role::MASTER) {
        if (h.sync && !h.master)
            s_roster.on_heartbeat(mac, src_ip, h.name, now);
        else
            s_roster.remove(mac);
    }
}

bool plan(uint8_t protocol, uint32_t freq_hz, const uint8_t *packet, uint8_t copies, uint32_t frame_us, int64_t now,
          int64_t &own_start_us, uint8_t &own_copies, int64_t &plan_end_us) {
    struct Out {
        uint32_t ip;
        TxMsg msg;
    } out[MAX_REMOTES];
    uint8_t n_out = 0;
    {
        Lock lock;
        if (s_role != Role::MASTER || !s_listening)
            return false;
        uint8_t live[MAX_REMOTES];
        uint8_t n = s_roster.live(millis(), live);
        if (n == 0)
            return false;
        uint32_t worst = 0;
        for (uint8_t i = 0; i < n; i++)
            worst = max(worst, s_roster.at(live[i]).error_us);
        // A remote's error is half its best round trip (plus drift), so 2 x error bounds its round trip.
        Plan p = make_plan(now, s_self_mac, s_roster, live, n, s_rotation++, frame_us, copies, worst, 2 * worst);
        uint32_t seq = ++s_plan_seq;
        for (uint8_t k = 0; k < p.count; k++) {
            const Slot &s = p.slots[k];
            if (s.member < 0) {
                own_start_us = s.start_us;
                own_copies = s.copies;
                continue;
            }
            const Member &m = s_roster.at((uint8_t) s.member);
            Out &o = out[n_out++];
            o.ip = m.ip;
            memset(&o.msg, 0, sizeof(o.msg));
            memcpy(o.msg.mac, s_self_mac, 6);
            o.msg.seq = seq;
            o.msg.start_us = s.start_us;
            o.msg.copies = s.copies;
            o.msg.protocol = protocol;
            o.msg.freq_hz = freq_hz;
            memcpy(o.msg.packet, packet, PACKET_LEN);
        }
        plan_end_us = p.end_us;
        s_plans++;
        s_last.valid = true;
        s_last.at_ms = millis();
        s_last.count = p.count;
        s_last.copies = copies;
        s_last.guard_us = p.guard_us;
        s_last.lead_us = p.lead_us;
        s_last.length_us = (uint32_t) (p.end_us - p.slots[0].start_us);
        for (uint8_t k = 0; k < p.count; k++)
            s_last.order[k] = p.slots[k].member;
    }
    for (uint8_t i = 0; i < n_out; i++) {  // outside the lock: one small unicast per remote
        uint8_t buf[64];
        send_to(buf, encode(out[i].msg, buf, sizeof(buf)), out[i].ip, GROUP_PORT);
    }
    return true;
}

bool following() {
    Lock lock;
    return s_role == Role::REMOTE && s_have_master && s_clock.valid() && millis() - s_last_ack_ms < MASTER_TIMEOUT_MS;
}

bool next_due(Pending &out) {
    Lock lock;
    if (s_role != Role::REMOTE)
        return false;
    const Pending *p = s_queue.next();
    if (!p)
        return false;
    out = *p;
    return true;
}

void done(bool sent) {
    Lock lock;
    s_queue.pop(sent);
}

void reset_stats() {
    Lock lock;
    s_queue.reset_counts();
    s_plans = 0;
}

String summary() {
    uint32_t now = millis();
    Lock lock;
    if (s_role == Role::OFF)
        return String();
    if (s_masters.alive(now) + (s_role == Role::MASTER ? 1 : 0) > 1)
        return "Group: 2+ MASTERS!";
    if (s_role == Role::MASTER) {
        uint8_t live[MAX_REMOTES];
        uint8_t n = s_roster.live(now, live);
        return "Master: " + String(n) + "/" + String(s_roster.size()) + " remotes";
    }
    if (!s_have_master)
        return "Remote: no master";
    if (!s_clock.valid() || now - s_last_ack_ms >= MASTER_TIMEOUT_MS)
        return "Remote: syncing";
    return "Remote: " + String(s_master_name);
}

static String mac_string(const uint8_t *mac) {
    char s[13];
    mac_to_hex(mac, s);
    return String(s);
}

void status_json(JsonObject o) {
    uint32_t now = millis();
    Lock lock;
    o["role"] = s_role == Role::MASTER ? "master" : s_role == Role::REMOTE ? "remote" : "off";
    o["mac"] = mac_string(s_self_mac);
    o["port"] = GROUP_PORT;
    o["listening"] = s_listening;
    // More than one master alive is a configuration mistake: both would schedule the air.
    JsonArray masters = o["masters"].to<JsonArray>();
    for (uint8_t i = 0; i < s_masters.size(); i++) {
        const MasterTracker::Seen &m = s_masters.at(i);
        if (now - m.last_ms > MasterTracker::ALIVE_MS)
            continue;
        JsonObject j = masters.add<JsonObject>();
        j["name"] = m.name;
        j["mac"] = mac_string(m.mac);
        j["ip"] = IPAddress(m.ip).toString();
    }
    o["conflict"] = s_masters.alive(now) + (s_role == Role::MASTER ? 1 : 0) > 1;
    if (s_role == Role::MASTER) {
        static const char *const STATES[] = {"joining", "synced", "sync_poor", "lost"};
        JsonArray rs = o["remotes"].to<JsonArray>();
        for (uint8_t i = 0; i < s_roster.size(); i++) {
            const Member &m = s_roster.at(i);
            JsonObject j = rs.add<JsonObject>();
            j["name"] = m.name;
            j["mac"] = mac_string(m.mac);
            j["ip"] = IPAddress(m.ip).toString();
            j["state"] = STATES[(int) s_roster.state(m, now)];
            j["error_us"] = m.error_us;
            j["sync_age_ms"] = m.last_sync_ms ? (int32_t) (now - m.last_sync_ms) : -1;
            j["on_time"] = m.on_time;
            j["late"] = m.late;
            j["missed"] = m.missed;
        }
        o["plans"] = s_plans;
        if (s_last.valid) {
            JsonObject p = o["last_plan"].to<JsonObject>();
            p["age_ms"] = now - s_last.at_ms;
            p["copies"] = s_last.copies;
            p["guard_us"] = s_last.guard_us;
            p["lead_us"] = s_last.lead_us;
            p["length_us"] = s_last.length_us;
            JsonArray order = p["order"].to<JsonArray>();
            for (uint8_t k = 0; k < s_last.count; k++) {
                int8_t m = s_last.order[k];
                order.add(m < 0 ? String("(master)") : String(m < s_roster.size() ? s_roster.at(m).name : "?"));
            }
        }
    } else if (s_role == Role::REMOTE) {
        o["following"] = s_have_master && s_clock.valid() && now - s_last_ack_ms < MASTER_TIMEOUT_MS;
        if (s_have_master) {
            JsonObject m = o["master"].to<JsonObject>();
            m["name"] = s_master_name;
            m["mac"] = mac_string(s_master_mac);
            m["ip"] = IPAddress(s_master_ip).toString();
            m["ack_age_ms"] = s_last_ack_ms ? (int32_t) (now - s_last_ack_ms) : -1;
        }
        JsonObject c = o["clock"].to<JsonObject>();
        c["synced"] = s_clock.valid();
        c["samples"] = s_clock.samples();
        c["offset_us"] = (double) s_clock.offset();
        c["error_us"] = s_clock.valid() ? s_clock.error_us(now_us()) : 0;
        c["rtt_us"] = s_clock.rtt_us();
        o["on_time"] = s_queue.on_time;
        o["late"] = s_queue.late;
        o["missed"] = s_queue.missed;
        o["last_tx_age_ms"] = s_last_tx_ms ? (int32_t) (now - s_last_tx_ms) : -1;
    }
}

}  // namespace group
