#pragma once

// Coverage group (no Arduino deps, host-testable): several controllers around one show put the same RF packets on
// the air, taking turns, so devices anywhere in the area hear at least one copy.
//
// One controller is the MASTER: it takes the show input and decides every packet. The others are REMOTES: they only
// transmit what the master schedules. Both find each other through the controller heartbeat (sync and master flags,
// full MAC); after that everything is unicast UDP on GROUP_PORT:
//
//   SYNC      remote -> master, every second: clock-sync request (t1), plus the remote's state and counters
//   SYNC_ACK  master -> remote: t1 echoed, t2 (received) and t3 (sent), in the master's clock
//   TX        master -> each remote, per transmission: a packet, and when (master clock) and how often to send it
//
// Each transmission is a PLAN: the live members (master included), sorted by MAC and rotated one place per plan,
// each get a slot of `copies` frames plus a guard gap, back to back, starting a little ahead of now so the TX
// messages arrive in time. Devices that hear several controllers simply get the change more than once.
//
// Times are microseconds of each board's own clock (esp_timer); a remote converts master time to its own with the
// offset ClockFilter keeps, NTP-style.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace n2rgroup {

static const uint16_t GROUP_PORT = 4050;
static const uint8_t VERSION = 1;
static const uint8_t MAX_REMOTES = 8;
static const uint8_t MAX_MEMBERS = MAX_REMOTES + 1;
static const uint8_t MAX_COPIES = 3;
static const uint8_t PACKET_LEN = 7;

// Timing (microseconds unless named _MS)
static const uint32_t SYNC_INTERVAL_MS = 1000;       // a remote's clock-sync request rate
static const uint32_t SYNC_FAST_INTERVAL_MS = 250;   // ... while it has fewer than FAST_SAMPLES samples
static const uint8_t FAST_SAMPLES = 4;
static const uint32_t MEMBER_TIMEOUT_MS = 3000;      // no SYNC this long: left out of plans
static const uint32_t MASTER_TIMEOUT_MS = 5000;      // no SYNC_ACK this long: a remote stops transmitting
static const uint32_t MAX_SYNC_ERROR_US = 10000;     // worse than this: "sync poor", no slot
static const uint32_t GUARD_BASE_US = 3000;          // radio turn-around and scheduling slack
static const uint32_t GUARD_MAX_US = 30000;
static const uint32_t LEAD_MIN_US = 15000;           // plan start this far ahead, at least ...
static const uint32_t LEAD_MAX_US = 100000;          // ... and at most
static const uint32_t DRIFT_US_PER_S = 50;           // clock error growth between samples (ESP32 crystals: <= 40 ppm)

enum MsgType : uint8_t { MSG_SYNC = 1, MSG_SYNC_ACK = 2, MSG_TX = 3 };

struct SyncMsg {
    uint8_t mac[6];
    uint32_t seq;
    int64_t t1;         // remote clock, when sent
    uint32_t error_us;  // the remote's current sync error estimate (0 = not synced yet)
    uint32_t on_time, late, missed;  // TX messages: sent in their slot / arrived too late / never arrived (seq gaps)
    char name[24];
};

struct SyncAck {
    uint8_t mac[6];  // the master's
    uint32_t seq;    // the SYNC's
    int64_t t1;      // echoed
    int64_t t2, t3;  // master clock: SYNC received, ACK sent
};

struct TxMsg {
    uint8_t mac[6];     // the master's
    uint32_t seq;       // plan number (gaps = plans this remote missed)
    int64_t start_us;   // this remote's slot start, master clock
    uint8_t copies;     // frames to send back to back
    uint8_t protocol;
    uint32_t freq_hz;
    uint8_t packet[PACKET_LEN];
};

// ---- wire format: "N2RG", version, type, then fields little-endian --------------------------------------------

namespace wire {
struct Writer {
    uint8_t *buf;
    size_t size, n;
    bool ok;
    void bytes(const void *p, size_t len) {
        if (n + len > size) {
            ok = false;
            return;
        }
        memcpy(buf + n, p, len);
        n += len;
    }
    void u8(uint8_t v) { bytes(&v, 1); }
    void u32(uint32_t v) {
        uint8_t b[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24)};
        bytes(b, 4);
    }
    void i64(int64_t v) {
        u32((uint32_t) ((uint64_t) v & 0xFFFFFFFFu));
        u32((uint32_t) ((uint64_t) v >> 32));
    }
};
struct Reader {
    const uint8_t *buf;
    size_t size, n;
    bool ok;
    void bytes(void *p, size_t len) {
        if (n + len > size) {
            ok = false;
            memset(p, 0, len);
            return;
        }
        memcpy(p, buf + n, len);
        n += len;
    }
    uint8_t u8() {
        uint8_t v = 0;
        bytes(&v, 1);
        return v;
    }
    uint32_t u32() {
        uint8_t b[4];
        bytes(b, 4);
        return (uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24;
    }
    int64_t i64() {
        uint64_t lo = u32(), hi = u32();
        return (int64_t) (lo | hi << 32);
    }
};
static const uint8_t MAGIC[4] = {'N', '2', 'R', 'G'};
inline void header(Writer &w, MsgType t) {
    w.bytes(MAGIC, 4);
    w.u8(VERSION);
    w.u8(t);
}
}  // namespace wire

// The message type of a datagram, or 0 if it isn't ours (wrong magic or version).
inline uint8_t message_type(const uint8_t *buf, size_t len) {
    if (!buf || len < 6 || memcmp(buf, wire::MAGIC, 4) != 0 || buf[4] != VERSION)
        return 0;
    return buf[5];
}

inline size_t encode(const SyncMsg &m, uint8_t *buf, size_t size) {
    wire::Writer w{buf, size, 0, true};
    wire::header(w, MSG_SYNC);
    w.bytes(m.mac, 6);
    w.u32(m.seq);
    w.i64(m.t1);
    w.u32(m.error_us);
    w.u32(m.on_time);
    w.u32(m.late);
    w.u32(m.missed);
    w.bytes(m.name, sizeof(m.name));
    return w.ok ? w.n : 0;
}
inline size_t encode(const SyncAck &m, uint8_t *buf, size_t size) {
    wire::Writer w{buf, size, 0, true};
    wire::header(w, MSG_SYNC_ACK);
    w.bytes(m.mac, 6);
    w.u32(m.seq);
    w.i64(m.t1);
    w.i64(m.t2);
    w.i64(m.t3);
    return w.ok ? w.n : 0;
}
inline size_t encode(const TxMsg &m, uint8_t *buf, size_t size) {
    wire::Writer w{buf, size, 0, true};
    wire::header(w, MSG_TX);
    w.bytes(m.mac, 6);
    w.u32(m.seq);
    w.i64(m.start_us);
    w.u8(m.copies);
    w.u8(m.protocol);
    w.u32(m.freq_hz);
    w.bytes(m.packet, PACKET_LEN);
    return w.ok ? w.n : 0;
}

inline bool decode(const uint8_t *buf, size_t len, SyncMsg &m) {
    if (message_type(buf, len) != MSG_SYNC)
        return false;
    wire::Reader r{buf, len, 6, true};
    r.bytes(m.mac, 6);
    m.seq = r.u32();
    m.t1 = r.i64();
    m.error_us = r.u32();
    m.on_time = r.u32();
    m.late = r.u32();
    m.missed = r.u32();
    r.bytes(m.name, sizeof(m.name));
    m.name[sizeof(m.name) - 1] = 0;
    return r.ok;
}
inline bool decode(const uint8_t *buf, size_t len, SyncAck &m) {
    if (message_type(buf, len) != MSG_SYNC_ACK)
        return false;
    wire::Reader r{buf, len, 6, true};
    r.bytes(m.mac, 6);
    m.seq = r.u32();
    m.t1 = r.i64();
    m.t2 = r.i64();
    m.t3 = r.i64();
    return r.ok;
}
inline bool decode(const uint8_t *buf, size_t len, TxMsg &m) {
    if (message_type(buf, len) != MSG_TX)
        return false;
    wire::Reader r{buf, len, 6, true};
    r.bytes(m.mac, 6);
    m.seq = r.u32();
    m.start_us = r.i64();
    m.copies = r.u8();
    m.protocol = r.u8();
    m.freq_hz = r.u32();
    r.bytes(m.packet, PACKET_LEN);
    return r.ok && m.copies >= 1 && m.copies <= MAX_COPIES + 1 && m.protocol < 2;
}

// ---- remote: clock offset to the master ------------------------------------------------------------------------

// NTP-style: for each exchange t1 (sent, ours), t2 (received, master), t3 (answered, master), t4 (back, ours),
// offset = ((t2 - t1) + (t3 - t4)) / 2 and round trip = (t4 - t1) - (t3 - t2). Of the last SAMPLES exchanges the one
// with the shortest round trip wins (the least queueing delay, so the most symmetric). Its error is half its round
// trip, plus clock drift since it was taken.
class ClockFilter {
 public:
    static const uint8_t SAMPLES = 8;

    void add(int64_t t1, int64_t t2, int64_t t3, int64_t t4) {
        int64_t rtt = (t4 - t1) - (t3 - t2);
        if (rtt < 0 || rtt > 2000000)
            return;  // nonsense (reordered or clock jump): ignore
        Sample &s = this->s_[this->head_];
        s.offset = ((t2 - t1) + (t3 - t4)) / 2;
        s.rtt = (uint32_t) rtt;
        s.at = t4;
        this->head_ = (uint8_t) ((this->head_ + 1) % SAMPLES);
        if (this->count_ < SAMPLES)
            this->count_++;
    }
    bool valid() const { return this->count_ > 0; }
    uint8_t samples() const { return this->count_; }
    // Master time minus local time.
    int64_t offset() const { return this->best_() ? this->best_()->offset : 0; }
    uint32_t rtt_us() const { return this->best_() ? this->best_()->rtt : 0; }
    // Error bound at local time `now`.
    uint32_t error_us(int64_t now) const {
        const Sample *b = this->best_();
        if (!b)
            return UINT32_MAX;
        int64_t age = now - b->at;
        uint64_t drift = age > 0 ? (uint64_t) age * DRIFT_US_PER_S / 1000000 : 0;
        uint64_t e = b->rtt / 2 + drift;
        return e > UINT32_MAX ? UINT32_MAX : (uint32_t) e;
    }
    int64_t to_local(int64_t master_us) const { return master_us - this->offset(); }
    void reset() { this->count_ = this->head_ = 0; }

 private:
    struct Sample {
        int64_t offset;
        uint32_t rtt;
        int64_t at;
    };
    const Sample *best_() const {
        const Sample *b = nullptr;
        for (uint8_t i = 0; i < this->count_; i++)
            if (!b || this->s_[i].rtt < b->rtt)
                b = &this->s_[i];
        return b;
    }
    Sample s_[SAMPLES]{};
    uint8_t head_{0}, count_{0};
};

// ---- master: who is in the group -------------------------------------------------------------------------------

inline int mac_cmp(const uint8_t *a, const uint8_t *b) { return memcmp(a, b, 6); }

struct Member {
    uint8_t mac[6];
    uint32_t ip;
    char name[24];
    uint32_t last_sync_ms;     // 0 = only seen in a heartbeat so far
    uint32_t last_beat_ms;
    uint32_t error_us;         // as the remote reports it
    uint32_t on_time, late, missed;
};

enum class MemberState : uint8_t { JOINING, SYNCED, POOR, LOST };

class Roster {
 public:
    // A heartbeat from a controller with sync on and master off: listed (as joining) until its SYNCs arrive.
    void on_heartbeat(const uint8_t *mac, uint32_t ip, const char *name, uint32_t now_ms) {
        Member *m = this->upsert_(mac, now_ms);
        if (!m)
            return;
        m->ip = ip;
        m->last_beat_ms = now_ms;
        if (name)
            copy_name_(m->name, name);
    }
    void on_sync(const SyncMsg &s, uint32_t ip, uint32_t now_ms) {
        Member *m = this->upsert_(s.mac, now_ms);
        if (!m)
            return;
        m->ip = ip;
        m->last_sync_ms = now_ms;
        m->error_us = s.error_us;
        m->on_time = s.on_time;
        m->late = s.late;
        m->missed = s.missed;
        copy_name_(m->name, s.name);
    }
    // A controller that turned sync off (or became a master): out at once.
    void remove(const uint8_t *mac) {
        for (uint8_t i = 0; i < this->n_; i++) {
            if (mac_cmp(this->m_[i].mac, mac) == 0) {
                this->m_[i] = this->m_[--this->n_];
                return;
            }
        }
    }
    // Drop members not heard from (heartbeat or sync) for a while.
    void expire(uint32_t now_ms, uint32_t forget_ms = 30000) {
        for (uint8_t i = 0; i < this->n_;) {
            uint32_t seen = this->m_[i].last_sync_ms > this->m_[i].last_beat_ms ? this->m_[i].last_sync_ms
                                                                                : this->m_[i].last_beat_ms;
            if (now_ms - seen > forget_ms)
                this->m_[i] = this->m_[--this->n_];
            else
                i++;
        }
    }
    MemberState state(const Member &m, uint32_t now_ms) const {
        if (!m.last_sync_ms)
            return MemberState::JOINING;
        if (now_ms - m.last_sync_ms > MEMBER_TIMEOUT_MS)
            return MemberState::LOST;
        if (m.error_us == 0 || m.error_us > MAX_SYNC_ERROR_US)
            return m.error_us == 0 ? MemberState::JOINING : MemberState::POOR;
        return MemberState::SYNCED;
    }
    uint8_t size() const { return this->n_; }
    const Member &at(uint8_t i) const { return this->m_[i]; }
    // Indices of the synced members, sorted by MAC.
    uint8_t live(uint32_t now_ms, uint8_t *out) const {
        uint8_t k = 0;
        for (uint8_t i = 0; i < this->n_; i++)
            if (this->state(this->m_[i], now_ms) == MemberState::SYNCED)
                out[k++] = i;
        for (uint8_t a = 1; a < k; a++)  // insertion sort by MAC
            for (uint8_t b = a; b > 0 && mac_cmp(this->m_[out[b - 1]].mac, this->m_[out[b]].mac) > 0; b--) {
                uint8_t t = out[b];
                out[b] = out[b - 1];
                out[b - 1] = t;
            }
        return k;
    }
    void clear() { this->n_ = 0; }

 private:
    static void copy_name_(char *dst, const char *src) {
        strncpy(dst, src, 23);
        dst[23] = 0;
    }
    Member *upsert_(const uint8_t *mac, uint32_t now_ms) {
        for (uint8_t i = 0; i < this->n_; i++)
            if (mac_cmp(this->m_[i].mac, mac) == 0)
                return &this->m_[i];
        if (this->n_ >= MAX_REMOTES) {
            this->expire(now_ms, MEMBER_TIMEOUT_MS);  // make room from the lost ones
            if (this->n_ >= MAX_REMOTES)
                return nullptr;
        }
        Member &m = this->m_[this->n_++];
        memset(&m, 0, sizeof(m));
        memcpy(m.mac, mac, 6);
        return &m;
    }
    Member m_[MAX_REMOTES]{};
    uint8_t n_{0};
};

// ---- master: the plan for one transmission ---------------------------------------------------------------------

struct Slot {
    int8_t member;  // index into the roster, or -1 for the master itself
    int64_t start_us;
    uint8_t copies;
};

struct Plan {
    uint8_t count;
    Slot slots[MAX_MEMBERS];
    int64_t end_us;  // the next plan may start here
    uint32_t guard_us, lead_us;
};

// Guard gap between slots: radio turn-around plus both neighbours' sync error.
inline uint32_t guard_for(uint32_t worst_error_us) {
    uint64_t g = (uint64_t) GUARD_BASE_US + 2ull * worst_error_us;
    return g > GUARD_MAX_US ? GUARD_MAX_US : (uint32_t) g;
}

// How far ahead to start, so the TX messages reach every remote first: its round trip, with margin.
inline uint32_t lead_for(uint32_t worst_rtt_us) {
    uint64_t l = (uint64_t) LEAD_MIN_US + 2ull * worst_rtt_us;
    return l > LEAD_MAX_US ? LEAD_MAX_US : (uint32_t) l;
}

// Members: the master (its MAC) and the live remotes (`live`, roster indices sorted by MAC). The master takes its
// place by MAC too; `rotation` (one more per plan) moves the starting member round, so no controller is always first.
inline Plan make_plan(int64_t now_us, const uint8_t *master_mac, const Roster &roster, const uint8_t *live,
                      uint8_t live_count, uint32_t rotation, uint32_t frame_us, uint8_t copies,
                      uint32_t worst_error_us, uint32_t worst_rtt_us) {
    Plan p{};
    int8_t order[MAX_MEMBERS];
    uint8_t n = 0;
    bool master_placed = false;
    for (uint8_t i = 0; i < live_count; i++) {
        if (!master_placed && mac_cmp(master_mac, roster.at(live[i]).mac) < 0) {
            order[n++] = -1;
            master_placed = true;
        }
        order[n++] = (int8_t) live[i];
    }
    if (!master_placed)
        order[n++] = -1;
    p.count = n;
    p.guard_us = guard_for(worst_error_us);
    p.lead_us = lead_for(worst_rtt_us);
    uint32_t slot_us = copies * frame_us + p.guard_us;
    int64_t t = now_us + p.lead_us;
    for (uint8_t k = 0; k < n; k++) {
        p.slots[k].member = order[(rotation + k) % n];
        p.slots[k].start_us = t;
        p.slots[k].copies = copies;
        t += slot_us;
    }
    p.end_us = t;
    return p;
}

// ---- remote: which master to follow ----------------------------------------------------------------------------

// Masters heard in heartbeats. A remote keeps the master it follows while that one is alive; otherwise it picks the
// lowest MAC, so every remote agrees. More than one alive = a configuration mistake, reported everywhere.
class MasterTracker {
 public:
    static const uint8_t MAX = 4;
    static const uint32_t ALIVE_MS = 15000;  // 3 missed heartbeats
    struct Seen {
        uint8_t mac[6];
        uint32_t ip;
        uint32_t last_ms;
        char name[24];
    };

    void on_master_beat(const uint8_t *mac, uint32_t ip, const char *name, uint32_t now_ms) {
        Seen *s = nullptr;
        for (uint8_t i = 0; i < this->n_; i++)
            if (mac_cmp(this->m_[i].mac, mac) == 0)
                s = &this->m_[i];
        if (!s) {
            if (this->n_ < MAX) {
                s = &this->m_[this->n_++];
            } else {  // replace the stalest
                s = &this->m_[0];
                for (uint8_t i = 1; i < MAX; i++)
                    if ((int32_t) (this->m_[i].last_ms - s->last_ms) < 0)
                        s = &this->m_[i];
            }
            memset(s, 0, sizeof(*s));
            memcpy(s->mac, mac, 6);
        }
        s->ip = ip;
        s->last_ms = now_ms;
        strncpy(s->name, name ? name : "", 23);
        s->name[23] = 0;
    }
    // Forget a controller that stopped being a master (its heartbeat says so).
    void not_master(const uint8_t *mac) {
        for (uint8_t i = 0; i < this->n_; i++) {
            if (mac_cmp(this->m_[i].mac, mac) == 0) {
                this->m_[i] = this->m_[--this->n_];
                return;
            }
        }
    }
    uint8_t alive(uint32_t now_ms) const {
        uint8_t k = 0;
        for (uint8_t i = 0; i < this->n_; i++)
            k += now_ms - this->m_[i].last_ms <= ALIVE_MS;
        return k;
    }
    // The master to follow now (nullptr if none), sticking with `current` (MAC, or nullptr) while it is alive.
    const Seen *choose(const uint8_t *current, uint32_t now_ms) const {
        const Seen *best = nullptr;
        for (uint8_t i = 0; i < this->n_; i++) {
            const Seen &s = this->m_[i];
            if (now_ms - s.last_ms > ALIVE_MS)
                continue;
            if (current && mac_cmp(s.mac, current) == 0)
                return &s;
            if (!best || mac_cmp(s.mac, best->mac) < 0)
                best = &s;
        }
        return best;
    }
    uint8_t size() const { return this->n_; }
    const Seen &at(uint8_t i) const { return this->m_[i]; }

 private:
    Seen m_[MAX]{};
    uint8_t n_{0};
};

// ---- remote: transmissions waiting for their slot --------------------------------------------------------------

struct Pending {
    int64_t local_us;  // slot start, own clock
    uint32_t seq;
    uint8_t copies, protocol;
    uint32_t freq_hz;
    uint8_t packet[PACKET_LEN];
};

// A short queue of scheduled transmissions, with the counters the remote reports to its master.
class RemoteQueue {
 public:
    static const uint8_t SIZE = 4;
    static const uint32_t LATE_US = 2000;  // a slot that started this long ago is skipped

    // Returns false (counted as late) if the slot has already passed.
    bool add(const TxMsg &m, int64_t local_start, int64_t now) {
        if (this->have_seq_ && (int32_t) (m.seq - this->last_seq_) > 1)
            this->missed += (uint32_t) (m.seq - this->last_seq_ - 1);
        if (!this->have_seq_ || (int32_t) (m.seq - this->last_seq_) > 0) {
            this->last_seq_ = m.seq;
            this->have_seq_ = true;
        }
        if (local_start < now - (int64_t) LATE_US || this->n_ >= SIZE) {
            this->late++;
            return false;
        }
        Pending &p = this->q_[this->n_++];
        p.local_us = local_start;
        p.seq = m.seq;
        p.copies = m.copies;
        p.protocol = m.protocol;
        p.freq_hz = m.freq_hz;
        memcpy(p.packet, m.packet, PACKET_LEN);
        return true;
    }
    // The earliest pending transmission, if any.
    const Pending *next() const {
        const Pending *b = nullptr;
        for (uint8_t i = 0; i < this->n_; i++)
            if (!b || this->q_[i].local_us < b->local_us)
                b = &this->q_[i];
        return b;
    }
    // Remove the earliest; `sent` = it went out in its slot (else it is counted as late).
    void pop(bool sent) {
        const Pending *b = this->next();
        if (!b)
            return;
        if (sent)
            this->on_time++;
        else
            this->late++;
        size_t i = (size_t) (b - this->q_);
        this->q_[i] = this->q_[--this->n_];
    }
    uint8_t size() const { return this->n_; }
    void clear() { this->n_ = 0; }
    void reset_counts() {
        this->on_time = this->late = this->missed = 0;
        this->have_seq_ = false;
    }
    uint32_t on_time{0}, late{0}, missed{0};

 private:
    Pending q_[SIZE]{};
    uint8_t n_{0};
    uint32_t last_seq_{0};
    bool have_seq_{false};
};

// "A8032A113F2A" <-> 6 bytes.
inline void mac_to_hex(const uint8_t *mac, char *out) {
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < 6; i++) {
        out[i * 2] = H[mac[i] >> 4];
        out[i * 2 + 1] = H[mac[i] & 15];
    }
    out[12] = 0;
}
inline bool mac_from_hex(const char *s, uint8_t *mac) {
    if (!s || strlen(s) != 12)
        return false;
    for (int i = 0; i < 12; i++) {
        char c = s[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0)
            return false;
        mac[i / 2] = (uint8_t) (i % 2 ? (mac[i / 2] | v) : v << 4);
    }
    return true;
}

}  // namespace n2rgroup
