#pragma once

// Pure protocol encoding for 433 MHz OOK LED wristbands (no Arduino deps, host-testable).
//
// Reverse engineered from the Flipper Zero "bracelet_led.fap" app.
//
// Protocol 0 (Shenzen New Dody Tech Co.) 433.889 MHz  packet: A0 A1 A2 A3 CMD ARG SUM  (SUM = byte sum of first 6)
//                                                       default address: 00 FF FF 0F
// Protocol 1 (LedGiftSupplier.com)       433.920 MHz  packet: 55 GP RR GG BB CK A1     (CK = RR^GG^BB^0x5A)
//   55 = "boot code" (DMX ch1 = 85 on the vendor's DMX transmitter), GP = group code (ch2; 0 = all groups),
//   RR GG BB = colour (ch3-5). A1 is 0xFF in every capture/app packet seen so far.
//
// Both protocols send a sync pulse followed by 56 bits (7 bytes) MSB first, pulse-width encoded.

#include <cstdint>
#include <cstddef>

namespace bracelet {

static const uint8_t PACKET_LEN = 7;
static const uint8_t NUM_PROTOCOLS = 2;

static const uint32_t DEFAULT_FREQ_P0 = 433889000;
static const uint32_t DEFAULT_FREQ_P1 = 433920000;

// Frame durations in microseconds (sync + 56 bits), used for airtime scheduling.
static const uint32_t FRAME_US_P0 = 2000 + 1000 + 1000 + 550 + 56 * 750;  // 46550
static const uint32_t FRAME_US_P1 = 200 + 1600 + 56 * 800;                // 46600

enum Action : uint8_t {
    ACTION_COLOR = 0,  // follow RGB
    ACTION_OFF,
    ACTION_FX_A,       // protocol 0: fade in to the last colour (CMD 05 AA)
    ACTION_FX_B,       // protocol 0: fade out to black (CMD 06 AA)
    ACTION_FX_C,       // protocol 0: from the Flipper app (D0 FF FF FF 55 00 packet); no reaction on tested bracelets
};

struct PaletteEntry {
    uint8_t code;  // protocol 0 ARG value for CMD 01
    uint8_t r, g, b;
    const char *name;
};

// Protocol 0 fixed colour table (ARG codes 06 and 07 are unused by the app; unknown colours).
static const PaletteEntry P0_PALETTE[] = {
    {0x00, 0xFF, 0x00, 0x00, "red"},     {0x01, 0x00, 0xFF, 0x00, "green"},
    {0x02, 0x00, 0x00, 0xFF, "blue"},    {0x03, 0xFF, 0x00, 0x80, "pink"},
    {0x04, 0xFF, 0xFF, 0xFF, "white"},   {0x05, 0xFF, 0xFF, 0x00, "yellow"},
    {0x08, 0x94, 0x00, 0xD3, "violet"},  {0x09, 0xFF, 0x80, 0x00, "orange"},
    {0x0A, 0x4B, 0x00, 0x82, "indigo"},  {0x0B, 0x00, 0xFF, 0xFF, "cyan"},
};
static const size_t P0_PALETTE_SIZE = sizeof(P0_PALETTE) / sizeof(P0_PALETTE[0]);

inline uint8_t p0_checksum(const uint8_t *pkt) {
    uint8_t sum = 0;
    for (int i = 0; i < 6; i++)
        sum += pkt[i];
    return sum;
}

inline uint8_t p1_checksum(const uint8_t *pkt) { return pkt[2] ^ pkt[3] ^ pkt[4] ^ 0x5A; }

// Recompute the checksum byte of a packet in place.
inline void fix_checksum(uint8_t protocol, uint8_t *pkt) {
    if (protocol == 0)
        pkt[6] = p0_checksum(pkt);
    else
        pkt[5] = p1_checksum(pkt);
}

// How many groups a packet's address reaches (larger = broader). Protocol 0: the set bits of the group mask in
// bytes 1-2 (FFFF = all 16). Protocol 1: group 0 is the broadcast to every group, any other code one group.
inline uint8_t address_breadth(uint8_t protocol, const uint8_t *pkt) {
    if (protocol != 0)
        return pkt[1] == 0 ? 255 : 1;
    uint8_t n = 0;
    for (uint16_t m = (uint16_t) (pkt[1] << 8 | pkt[2]); m; m >>= 1)
        n += m & 1;
    return n;
}

// True when two packets' addresses reach at least one common group.
inline bool addresses_overlap(uint8_t protocol, const uint8_t *a, const uint8_t *b) {
    if (protocol != 0)
        return a[1] == 0 || b[1] == 0 || a[1] == b[1];
    return ((a[1] & b[1]) | (a[2] & b[2])) != 0;
}

// Protocol 0 "base layer": the zone addressed to every group (mask FFFF) is the base, and the other zones sit on
// top of it. A zone showing its own colour is cut out of the base zone's address, so base changes never reach
// its bracelets; a zone that is off (black), or wants the very colour the base shows, follows the base
// instead and needs no packet of its own while the base's broadcast covers it. This works because the address is a group mask: "everyone except groups 2 and 5"
// is one packet.
//
// wants[i] holds zone i's packet built from its own colour; addrs[i] its configured address; active[i] whether
// the zone is in use. Rewrites wants in place. Returns the base zone's index (-1 if there is none: nothing is
// changed), sets `follows` (bit per zone following the base) and `base_mask` (the groups the base still
// addresses; 0 = every group is covered by a zone with its own colour).
inline int p0_apply_base_layer(uint8_t wants[][PACKET_LEN], const uint8_t (*addrs)[4], const bool *active, uint8_t n,
                               uint16_t *follows, uint16_t *base_mask) {
    *follows = 0;
    *base_mask = 0xFFFF;
    int base = -1;
    for (uint8_t i = 0; i < n && base < 0; i++)
        if (active[i] && addrs[i][1] == 0xFF && addrs[i][2] == 0xFF)
            base = i;
    if (base < 0)
        return -1;
    const bool base_whole_packet = wants[base][0] == 0xD0;  // the fixed built-in effect packet: its address is not ours
    uint16_t mask = 0xFFFF;
    for (uint8_t i = 0; i < n; i++) {
        if (i == base || !active[i])
            continue;
        const bool same_as_base = !base_whole_packet && wants[i][0] != 0xD0 && wants[i][4] == wants[base][4] &&
                                  wants[i][5] == wants[base][5];
        if (wants[i][4] != 0x00 && !same_as_base) {  // not off, not the base's colour: the zone shows its own
            if (wants[i][0] != 0xD0)
                mask &= (uint16_t) ~(addrs[i][1] << 8 | addrs[i][2]);
            continue;
        }
        *follows |= (uint16_t) (1u << i);
        if (base_whole_packet) {
            memcpy(wants[i], wants[base], PACKET_LEN);
        } else {
            wants[i][4] = wants[base][4];
            wants[i][5] = wants[base][5];
            fix_checksum(0, wants[i]);
        }
    }
    if (!base_whole_packet) {
        wants[base][1] = mask >> 8;
        wants[base][2] = mask & 0xFF;
        fix_checksum(0, wants[base]);
    }
    *base_mask = mask;
    return base;
}

// Protocol 0: two packets can share one transmission when they carry the same command and differ only in
// the group mask, because masks combine ("red to groups 1, 2 and 5" is one packet).
inline bool p0_can_merge(const uint8_t *a, const uint8_t *b) {
    return a[0] != 0xD0 && b[0] != 0xD0 && a[0] == b[0] && a[3] == b[3] && a[4] == b[4] && a[5] == b[5];
}

// Adds `other`'s groups to `pkt` (both protocol 0, p0_can_merge) and fixes the checksum.
inline void p0_merge(uint8_t *pkt, const uint8_t *other) {
    pkt[1] |= other[1];
    pkt[2] |= other[2];
    fix_checksum(0, pkt);
}

// 8-bit channel value -> protocol 1 byte: inverted 4-bit level in the high nibble, low nibble 0xF.
inline uint8_t p1_channel(uint8_t v) {
    uint8_t level = (15 * v + 127) / 255;
    return (uint8_t) (((15 - level) << 4) | 0x0F);
}

// Nearest protocol 0 palette colour. Returns false when the colour is dark enough to mean "off".
inline bool p0_nearest(uint8_t r, uint8_t g, uint8_t b, uint8_t off_threshold, uint8_t *code) {
    uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx < off_threshold)
        return false;
    // Score each entry on both the raw colour (so dark palette entries like indigo match exactly)
    // and the brightness-normalised colour (so a dim red still maps to red).
    uint32_t nr = (uint32_t) r * 255 / mx, ng = (uint32_t) g * 255 / mx, nb = (uint32_t) b * 255 / mx;
    auto dist = [](uint32_t r1, uint32_t g1, uint32_t b1, const PaletteEntry &e) {
        int32_t dr = (int32_t) r1 - e.r, dg = (int32_t) g1 - e.g, db = (int32_t) b1 - e.b;
        return (uint32_t) (dr * dr + dg * dg + db * db);
    };
    uint32_t best = UINT32_MAX;
    for (size_t i = 0; i < P0_PALETTE_SIZE; i++) {
        uint32_t d_raw = dist(r, g, b, P0_PALETTE[i]);
        uint32_t d_norm = dist(nr, ng, nb, P0_PALETTE[i]);
        uint32_t d = d_raw < d_norm ? d_raw : d_norm;
        if (d < best) {
            best = d;
            *code = P0_PALETTE[i].code;
        }
    }
    return true;
}

// Build a 7-byte packet. `addr` holds the group address bytes:
//   protocol 0: addr[0..3] = packet bytes 0..3
//   protocol 1: addr[0] = packet byte 1, addr[1] = packet byte 6
inline void build_packet(uint8_t protocol, const uint8_t *addr, Action action, uint8_t r, uint8_t g, uint8_t b,
                         uint8_t off_threshold, uint8_t *out) {
    if (protocol == 0) {
        if (action == ACTION_FX_C) {
            static const uint8_t FX_C[PACKET_LEN] = {0xD0, 0xFF, 0xFF, 0xFF, 0x55, 0x00, 0x22};
            for (int i = 0; i < PACKET_LEN; i++)
                out[i] = FX_C[i];
            return;
        }
        for (int i = 0; i < 4; i++)
            out[i] = addr[i];
        uint8_t code = 0;
        if (action == ACTION_FX_A) {
            out[4] = 0x05;
            out[5] = 0xAA;
        } else if (action == ACTION_FX_B) {
            out[4] = 0x06;
            out[5] = 0xAA;
        } else if (action == ACTION_COLOR && p0_nearest(r, g, b, off_threshold, &code)) {
            out[4] = 0x01;
            out[5] = code;
        } else {
            out[4] = 0x00;  // off
            out[5] = 0xAA;
        }
        out[6] = p0_checksum(out);
    } else {
        if (action != ACTION_COLOR)
            r = g = b = 0;  // protocol 1 has no built-in effects; treat as off
        out[0] = 0x55;
        out[1] = addr[0];
        out[2] = p1_channel(r);
        out[3] = p1_channel(g);
        out[4] = p1_channel(b);
        out[5] = p1_checksum(out);
        out[6] = addr[1];
    }
}

// Map the optional per-group FX channel (DMX value 0-255) to an action.
inline Action fx_action(uint8_t v) {
    if (v < 20)
        return ACTION_COLOR;
    if (v < 40)
        return ACTION_OFF;
    if (v < 60)
        return ACTION_FX_A;
    if (v < 80)
        return ACTION_FX_B;
    if (v < 100)
        return ACTION_FX_C;
    return ACTION_COLOR;
}

// Pixel colour orders: index -> position of R, G, B within the 3-byte pixel.
enum ColorOrder : uint8_t { ORDER_RGB = 0, ORDER_RBG, ORDER_GRB, ORDER_GBR, ORDER_BRG, ORDER_BGR, NUM_ORDERS };
static const uint8_t COLOR_ORDER_OFFSETS[NUM_ORDERS][3] = {
    {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {2, 0, 1}, {1, 2, 0}, {2, 1, 0},
};
static const char *const COLOR_ORDER_NAMES[NUM_ORDERS] = {"RGB", "RBG", "GRB", "GBR", "BRG", "BGR"};

inline void extract_rgb(uint8_t order, const uint8_t *px, uint8_t *r, uint8_t *g, uint8_t *b) {
    const uint8_t *o = COLOR_ORDER_OFFSETS[order < NUM_ORDERS ? order : (uint8_t) ORDER_RGB];
    *r = px[o[0]];
    *g = px[o[1]];
    *b = px[o[2]];
}

inline uint32_t frame_us(uint8_t protocol) { return protocol == 0 ? FRAME_US_P0 : FRAME_US_P1; }

// Emit the OOK timings of one frame. Sink must provide mark(us) and space(us).
template<typename Sink> void encode_frame(uint8_t protocol, const uint8_t *pkt, Sink &sink) {
    uint32_t one_mark, one_space, zero_mark, zero_space;
    if (protocol == 0) {
        sink.mark(2000);
        sink.space(1000);
        sink.mark(1000);
        sink.space(550);
        one_mark = 500, one_space = 250, zero_mark = 250, zero_space = 500;
    } else {
        sink.mark(200);
        sink.space(1600);
        one_mark = 600, one_space = 200, zero_mark = 200, zero_space = 600;
    }
    for (int byte = 0; byte < PACKET_LEN; byte++) {
        for (int bit = 7; bit >= 0; bit--) {
            if (pkt[byte] & (1 << bit)) {
                sink.mark(one_mark);
                sink.space(one_space);
            } else {
                sink.mark(zero_mark);
                sink.space(zero_space);
            }
        }
    }
}

}  // namespace bracelet
