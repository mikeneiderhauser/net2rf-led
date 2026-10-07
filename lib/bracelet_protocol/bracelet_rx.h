#pragma once

// Receive side of the bracelet protocols (no Arduino deps, host-testable): turns the demodulated OOK
// signal (a stream of mark/space durations) back into packets, and keeps track of what every group was
// last told to show. Used by the controller's receiver mode.
//
// Both protocols are decoded at the same time: their frequencies are 31 kHz apart, well inside one
// receiver bandwidth, so a receiver tuned between them hears both.

#include <cstdint>
#include <cstring>

#include "bracelet_protocol.h"

namespace bracelet {

// Protocol 1 checksum as the vendor's DMX transmitter sends it (see p1_checksum), or as the Flipper app and
// net2rf firmware before the fix sent it (RR^GG^BB^0x5A, no group). Both kinds of packet are accepted.
enum P1Checksum : uint8_t { P1_CK_BAD = 0, P1_CK_VENDOR, P1_CK_LEGACY };
inline P1Checksum p1_checksum_kind(const uint8_t *pkt) {
    if (pkt[5] == p1_checksum(pkt))
        return P1_CK_VENDOR;
    if (pkt[5] == (uint8_t) (pkt[2] ^ pkt[3] ^ pkt[4] ^ 0x5A))
        return P1_CK_LEGACY;
    return P1_CK_BAD;
}

inline bool packet_valid(uint8_t protocol, const uint8_t *pkt) {
    if (protocol == 0)
        return pkt[6] == p0_checksum(pkt);
    return pkt[0] == 0x55 && p1_checksum_kind(pkt) != P1_CK_BAD;
}

struct RxFrame {
    uint8_t protocol;
    uint8_t pkt[PACKET_LEN];
};

// Decodes frames from mark/space durations. Feed every level change: push(level, us) with the level that has
// just ended (1 = carrier on) and how long it lasted. Bits are told apart by the mark length alone, so the
// space after the last bit (which runs into the gap before the next frame) doesn't matter.
class FrameDecoder {
 public:
    // Returns true when a complete frame (valid checksum) has been decoded into `out`.
    bool push(uint8_t level, uint32_t us, RxFrame &out) {
        bool got = false;
        if (this->p1_.push(level, us, this->scratch_)) {
            if (packet_valid(1, this->scratch_)) {
                out.protocol = 1;
                memcpy(out.pkt, this->scratch_, PACKET_LEN);
                got = true;
            } else if (this->scratch_[0] == 0x55) {
                // Only count frames that start like one: a protocol 0 sync also passes as a protocol 1 sync.
                this->bad_++;
            }
        }
        if (this->p0_.push(level, us, this->scratch_)) {
            if (packet_valid(0, this->scratch_)) {
                if (!got) {
                    out.protocol = 0;
                    memcpy(out.pkt, this->scratch_, PACKET_LEN);
                    got = true;
                }
            } else {
                this->bad_++;
            }
        }
        if (got)
            this->good_++;
        return got;
    }
    void reset() {
        this->p0_ = P0{};
        this->p1_ = P1{};
    }
    void reset_counts() { this->good_ = this->bad_ = 0; }
    uint32_t good() const { return this->good_; }
    // Frames that had the right shape (sync + 56 bits) but failed their checksum: weak signal or interference.
    uint32_t bad() const { return this->bad_; }

    // Timing windows (microseconds). Generous: the receiver's slicer stretches and shrinks marks a little.
    // Protocol 1: sync = a short mark then a long gap (the vendor's transmitter was also measured with a
    // ~1000 us gap after a long mark, so only the gap is checked), bits = 600/200 or 200/600.
    static const uint32_t P1_SYNC_MIN = 850, P1_SYNC_MAX = 2600;
    static const uint32_t P1_MARK_MIN = 90, P1_MARK_MAX = 1000, P1_ONE_MIN = 400;
    static const uint32_t P1_SPACE_MIN = 90, P1_SPACE_MAX = 840;
    // Protocol 0: sync = mark 2000, space 1000, mark 1000, space 550; bits = 500/250 or 250/500.
    static const uint32_t P0_BIT_MIN = 90, P0_BIT_MAX = 720, P0_ONE_MIN = 375;

 private:
    struct Machine {
        uint8_t step{0};  // protocol 0: sync steps 1-3; both: 4 = bits
        uint8_t bits{0};
        uint8_t pkt[PACKET_LEN]{};

        void start_bits() {
            this->step = 4;
            this->bits = 0;
            memset(this->pkt, 0, PACKET_LEN);
        }
        // Mark of one bit; true when it was the last of 56.
        bool add_bit(bool one, uint8_t *out) {
            if (one)
                this->pkt[this->bits / 8] |= (uint8_t) (0x80 >> (this->bits % 8));
            if (++this->bits < PACKET_LEN * 8)
                return false;
            memcpy(out, this->pkt, PACKET_LEN);
            this->step = 0;
            return true;
        }
    };

    struct P1 : Machine {
        bool push(uint8_t level, uint32_t us, uint8_t *out) {
            if (this->step == 4) {
                if (level) {
                    if (us >= P1_MARK_MIN && us <= P1_MARK_MAX)
                        return this->add_bit(us >= P1_ONE_MIN, out);
                } else if (us >= P1_SPACE_MIN && us <= P1_SPACE_MAX) {
                    return false;
                }
                this->step = 0;  // out of shape: look for the next sync (this pulse may be it)
            }
            if (!level && us >= P1_SYNC_MIN && us <= P1_SYNC_MAX)
                this->start_bits();
            return false;
        }
    };

    struct P0 : Machine {
        bool push(uint8_t level, uint32_t us, uint8_t *out) {
            if (this->step == 4) {
                if (us >= P0_BIT_MIN && us <= P0_BIT_MAX) {
                    if (level)
                        return this->add_bit(us >= P0_ONE_MIN, out);
                    return false;
                }
                this->step = 0;
            }
            // Sync: mark ~2000, space ~1000, mark ~1000, space ~550.
            switch (this->step) {
                case 1:
                    this->step = (!level && us >= 650 && us <= 1450) ? 2 : 0;
                    break;
                case 2:
                    this->step = (level && us >= 650 && us <= 1450) ? 3 : 0;
                    break;
                case 3:
                    if (!level && us >= 330 && us <= 850) {
                        this->start_bits();
                        return false;
                    }
                    this->step = 0;
                    break;
            }
            if (this->step == 0 && level && us >= 1400 && us <= 2800)
                this->step = 1;
            return false;
        }
    };

    P1 p1_;
    P0 p0_;
    uint8_t scratch_[PACKET_LEN]{};
    uint32_t good_{0}, bad_{0};
};

// What a received packet tells its groups to show.
struct RxCommand {
    uint8_t r{0}, g{0}, b{0};
    const char *label{nullptr};  // protocol 0 colour or effect name; nullptr = plain RGB (protocol 1)
    bool keep_colour{false};     // fade in: the bracelet goes back to the colour it last showed
};

inline const char *p0_colour_name(uint8_t code, uint8_t *r, uint8_t *g, uint8_t *b) {
    for (size_t i = 0; i < P0_PALETTE_SIZE; i++) {
        if (P0_PALETTE[i].code == code) {
            *r = P0_PALETTE[i].r, *g = P0_PALETTE[i].g, *b = P0_PALETTE[i].b;
            return P0_PALETTE[i].name;
        }
    }
    *r = *g = *b = 0;
    return "unknown colour";
}

inline RxCommand describe_packet(uint8_t protocol, const uint8_t *pkt) {
    RxCommand c;
    if (protocol != 0) {
        c.r = p1_level(pkt[2]), c.g = p1_level(pkt[3]), c.b = p1_level(pkt[4]);
        return c;
    }
    if (pkt[0] == 0xD0) {
        c.label = "effect C";
        c.keep_colour = true;
        return c;
    }
    switch (pkt[4]) {
        case 0x01:
            c.label = p0_colour_name(pkt[5], &c.r, &c.g, &c.b);
            break;
        case 0x00:
            c.label = "off";
            break;
        case 0x05:
            c.label = "fade in";
            c.keep_colour = true;
            break;
        case 0x06:
            c.label = "fade out";
            break;
        default:
            c.label = "unknown command";
            c.keep_colour = true;
    }
    return c;
}

// The groups a packet reaches, as a bit set (protocol 0: groups 0-15; protocol 1: one group code 1-255).
// Returns true for a packet to every group (protocol 0: mask FFFF or the effect C packet; protocol 1: group 0).
inline bool packet_groups(uint8_t protocol, const uint8_t *pkt, uint16_t *mask, uint8_t *group) {
    *mask = 0;
    *group = 0;
    if (protocol != 0) {
        *group = pkt[1];
        return pkt[1] == 0;
    }
    if (pkt[0] == 0xD0) {
        *mask = 0xFFFF;
        return true;
    }
    // groups 0-7 are the bits of byte 1, groups 8-15 those of byte 2
    *mask = (uint16_t) (pkt[1] | (pkt[2] << 8));
    return *mask == 0xFFFF;
}

// The last thing every group heard. A packet to every group updates the "all groups" row and every group row
// already shown; a packet to particular groups creates or updates their rows.
struct RxZone {
    uint8_t protocol;
    uint8_t group;  // ALL_GROUPS for the every-group row
    uint8_t r, g, b;
    const char *label;
    uint8_t pkt[PACKET_LEN];  // the packet that set it
    uint32_t updates;         // distinct commands (repeats of one transmission count once)
    uint32_t last_ms;
    int16_t rssi_dbm;
};

class RxTracker {
 public:
    static const uint8_t MAX_ZONES = 32;
    static const uint8_t ALL_GROUPS = 0xFF;
    static const uint32_t REPEAT_MS = 400;  // the same packet again within this = a repeat of one transmission

    // Returns true when the frame is a new command (not a repeat of the last one).
    bool apply(const RxFrame &f, uint32_t now_ms, int16_t rssi_dbm = -127) {
        this->frames_++;
        bool repeat = this->have_last_ && this->last_.protocol == f.protocol &&
                      memcmp(this->last_.pkt, f.pkt, PACKET_LEN) == 0 && now_ms - this->last_ms_ < REPEAT_MS;
        this->last_ = f;
        this->last_ms_ = now_ms;
        this->have_last_ = true;
        if (!repeat)
            this->updates_++;

        RxCommand cmd = describe_packet(f.protocol, f.pkt);
        uint16_t mask;
        uint8_t group;
        if (packet_groups(f.protocol, f.pkt, &mask, &group)) {
            this->set_(this->upsert_(f.protocol, ALL_GROUPS), cmd, f, now_ms, rssi_dbm, repeat);
            for (uint8_t i = 0; i < this->count_; i++)
                if (this->zones_[i].protocol == f.protocol && this->zones_[i].group != ALL_GROUPS)
                    this->set_(&this->zones_[i], cmd, f, now_ms, rssi_dbm, repeat);
        } else if (f.protocol != 0) {
            this->set_(this->upsert_(1, group), cmd, f, now_ms, rssi_dbm, repeat);
        } else {
            for (uint8_t g = 0; g < 16; g++)
                if (mask & (1u << g))
                    this->set_(this->upsert_(0, g), cmd, f, now_ms, rssi_dbm, repeat);
        }
        return !repeat;
    }

    uint8_t count() const { return this->count_; }
    const RxZone &zone(uint8_t i) const { return this->zones_[i]; }
    uint32_t frames() const { return this->frames_; }
    uint32_t updates() const { return this->updates_; }
    bool have_last() const { return this->have_last_; }
    const RxFrame &last() const { return this->last_; }
    uint32_t last_ms() const { return this->last_ms_; }
    void clear() { *this = RxTracker(); }

 private:
    // Rows stay sorted: protocol, then the every-group row, then group number. A full table drops new groups.
    RxZone *upsert_(uint8_t protocol, uint8_t group) {
        auto key = [](uint8_t p, uint8_t g) { return (uint16_t) ((p << 9) | (g == ALL_GROUPS ? 0 : 0x100 | g)); };
        uint16_t k = key(protocol, group);
        uint8_t at = 0;
        while (at < this->count_ && key(this->zones_[at].protocol, this->zones_[at].group) < k)
            at++;
        if (at < this->count_ && key(this->zones_[at].protocol, this->zones_[at].group) == k)
            return &this->zones_[at];
        if (this->count_ >= MAX_ZONES)
            return nullptr;
        memmove(&this->zones_[at + 1], &this->zones_[at], (this->count_ - at) * sizeof(RxZone));
        this->count_++;
        RxZone &z = this->zones_[at];
        memset(&z, 0, sizeof(z));
        z.protocol = protocol;
        z.group = group;
        z.rssi_dbm = -127;
        return &z;
    }
    static void set_(RxZone *z, const RxCommand &cmd, const RxFrame &f, uint32_t now_ms, int16_t rssi, bool repeat) {
        if (!z)
            return;
        if (!cmd.keep_colour)
            z->r = cmd.r, z->g = cmd.g, z->b = cmd.b;
        z->label = cmd.label;
        memcpy(z->pkt, f.pkt, PACKET_LEN);
        if (!repeat)
            z->updates++;
        z->last_ms = now_ms;
        if (rssi > -127)
            z->rssi_dbm = rssi;
    }

    RxZone zones_[MAX_ZONES]{};
    uint8_t count_{0};
    uint32_t frames_{0}, updates_{0};
    RxFrame last_{};
    uint32_t last_ms_{0};
    bool have_last_{false};
};

}  // namespace bracelet
