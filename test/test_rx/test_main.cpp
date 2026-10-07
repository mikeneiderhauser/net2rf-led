// Host tests for bracelet_rx.h: frames encoded by bracelet_protocol.h decode back to the same packets, with
// realistic timing errors and noise, and the zone tracker keeps the right colour per group.
// Run: pio test -e native
#include <unity.h>

#include <cstdlib>
#include <vector>

#include "bracelet_protocol.h"
#include "bracelet_rx.h"

using namespace bracelet;

// Feeds encode_frame() output straight into a decoder, optionally stretching marks (the receiver's slicer
// does that) and adding random timing jitter.
struct DecodeSink {
    explicit DecodeSink(FrameDecoder &d) : dec(d) {}
    FrameDecoder &dec;
    std::vector<RxFrame> frames;
    int mark_skew_us{0};
    int jitter_pct{0};
    uint32_t vary(uint32_t us) {
        if (!this->jitter_pct)
            return us;
        int span = (int) us * this->jitter_pct / 100;
        return (uint32_t) ((int) us + (rand() % (2 * span + 1)) - span);
    }
    void push(uint8_t level, uint32_t us) {
        RxFrame f;
        if (this->dec.push(level, us, f))
            this->frames.push_back(f);
    }
    void mark(uint32_t us) { this->push(1, this->vary((uint32_t) ((int) us + this->mark_skew_us))); }
    void space(uint32_t us) { this->push(0, this->vary((uint32_t) ((int) us - this->mark_skew_us))); }
    void idle() { this->push(0, 50000); }
};

static void build(uint8_t protocol, const uint8_t *addr, uint8_t r, uint8_t g, uint8_t b, uint8_t *pkt) {
    build_packet(protocol, addr, ACTION_COLOR, r, g, b, 16, pkt);
}

static const uint8_t P0_ALL[4] = {0x00, 0xFF, 0xFF, 0x0F};

void test_roundtrip_both_protocols(void) {
    FrameDecoder dec;
    DecodeSink sink(dec);
    uint8_t p0[PACKET_LEN], p1[PACKET_LEN];
    const uint8_t g3[2] = {3, 0xFF};
    build(0, P0_ALL, 0, 0, 255, p0);
    build(1, g3, 12, 200, 99, p1);
    for (int i = 0; i < 3; i++)
        encode_frame(0, p0, sink);
    sink.idle();
    for (int i = 0; i < 3; i++)
        encode_frame(1, p1, sink);
    sink.idle();
    TEST_ASSERT_EQUAL(6, sink.frames.size());
    for (int i = 0; i < 6; i++) {
        TEST_ASSERT_EQUAL_UINT8(i < 3 ? 0 : 1, sink.frames[i].protocol);
        TEST_ASSERT_EQUAL_HEX8_ARRAY(i < 3 ? p0 : p1, sink.frames[i].pkt, PACKET_LEN);
    }
    TEST_ASSERT_EQUAL_UINT32(6, dec.good());
    TEST_ASSERT_EQUAL_UINT32(0, dec.bad());  // protocol 0 frames don't count as broken protocol 1 frames
}

void test_roundtrip_with_timing_error(void) {
    srand(1234);
    for (int skew : {-60, 0, 80}) {
        FrameDecoder dec;
        DecodeSink sink(dec);
        sink.mark_skew_us = skew;
        sink.jitter_pct = 12;
        uint8_t pkt[PACKET_LEN];
        for (int k = 0; k < 40; k++) {
            uint8_t proto = k & 1;
            const uint8_t a1[2] = {(uint8_t) (k * 7), 0xFF};
            build(proto, proto ? a1 : P0_ALL, (uint8_t) (k * 37), (uint8_t) (k * 11), (uint8_t) (255 - k), pkt);
            sink.frames.clear();
            encode_frame(proto, pkt, sink);
            sink.idle();
            TEST_ASSERT_EQUAL_MESSAGE(1, sink.frames.size(), "frame lost");
            TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, sink.frames[0].pkt, PACKET_LEN);
        }
    }
}

void test_noise_and_bad_checksum(void) {
    srand(42);
    FrameDecoder dec;
    DecodeSink sink(dec);
    for (int i = 0; i < 5000; i++)  // receiver noise between transmissions: random short pulses
        sink.push(i & 1, 20 + rand() % 900);
    sink.idle();
    TEST_ASSERT_EQUAL(0, sink.frames.size());

    uint8_t pkt[PACKET_LEN];
    const uint8_t g2[2] = {2, 0xFF};
    build(1, g2, 255, 0, 0, pkt);
    pkt[3] ^= 0x10;  // corrupted on the way
    encode_frame(1, pkt, sink);
    sink.idle();
    TEST_ASSERT_EQUAL(0, sink.frames.size());
    TEST_ASSERT_EQUAL_UINT32(1, dec.bad());

    build(1, g2, 255, 0, 0, pkt);  // and a good one right after noise
    for (int i = 0; i < 200; i++)
        sink.push(i & 1, 30 + rand() % 400);
    encode_frame(1, pkt, sink);
    sink.idle();
    TEST_ASSERT_EQUAL(1, sink.frames.size());
}

void test_vendor_sync_variant(void) {
    // The vendor's transmitter was also measured with a long sync mark and a ~1000 us gap.
    FrameDecoder dec;
    uint8_t pkt[PACKET_LEN];
    const uint8_t g1[2] = {1, 0xFF};
    build(1, g1, 0, 128, 0, pkt);
    RxFrame f;
    int got = 0;
    dec.push(1, 600, f);
    dec.push(0, 1000, f);
    for (int bit = 0; bit < 56; bit++) {
        bool one = pkt[bit / 8] & (0x80 >> (bit % 8));
        got += dec.push(1, one ? 600 : 200, f);
        got += dec.push(0, bit == 55 ? 20000 : one ? 200 : 600, f);
    }
    TEST_ASSERT_EQUAL(1, got);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, f.pkt, PACKET_LEN);
}

void test_legacy_checksum_accepted(void) {
    // A packet as the Flipper app (and net2rf before the vendor fix) built it: 16 levels, CK = RR^GG^BB^0x5A.
    const uint8_t legacy[PACKET_LEN] = {0x55, 0x00, 0x0F, 0xFF, 0xFF, 0x55, 0xFF};
    TEST_ASSERT_EQUAL(P1_CK_LEGACY, p1_checksum_kind(legacy));
    TEST_ASSERT_TRUE(packet_valid(1, legacy));
    uint8_t vendor[PACKET_LEN];
    const uint8_t g9[2] = {9, 0xFF};
    build(1, g9, 255, 0, 0, vendor);
    TEST_ASSERT_EQUAL(P1_CK_VENDOR, p1_checksum_kind(vendor));
    vendor[5] ^= 1;
    TEST_ASSERT_EQUAL(P1_CK_BAD, p1_checksum_kind(vendor));
}

void test_tracker_p1(void) {
    RxTracker t;
    RxFrame f{1, {}};
    const uint8_t g2[2] = {2, 0xFF}, g5[2] = {5, 0xFF}, all[2] = {0, 0xFF};
    build(1, g5, 0, 0, 255, f.pkt);
    TEST_ASSERT_TRUE(t.apply(f, 1000));
    TEST_ASSERT_FALSE(t.apply(f, 1047));  // the second copy of the same transmission
    TEST_ASSERT_FALSE(t.apply(f, 1094));
    build(1, g2, 255, 0, 0, f.pkt);
    t.apply(f, 2000);
    TEST_ASSERT_EQUAL_UINT8(2, t.count());
    TEST_ASSERT_EQUAL_UINT8(2, t.zone(0).group);  // sorted by group
    TEST_ASSERT_EQUAL_UINT8(5, t.zone(1).group);
    TEST_ASSERT_EQUAL_UINT8(255, t.zone(0).r);
    TEST_ASSERT_EQUAL_UINT32(1, t.zone(1).updates);
    TEST_ASSERT_EQUAL_UINT32(4, t.frames());
    TEST_ASSERT_EQUAL_UINT32(2, t.updates());

    build(1, all, 0, 255, 0, f.pkt);  // group 0 reaches every group
    t.apply(f, 3000, -61);
    TEST_ASSERT_EQUAL_UINT8(3, t.count());
    TEST_ASSERT_EQUAL_UINT8(RxTracker::ALL_GROUPS, t.zone(0).group);  // the every-group row comes first
    for (uint8_t i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL_UINT8(0, t.zone(i).r);
        TEST_ASSERT_EQUAL_UINT8(255, t.zone(i).g);
        TEST_ASSERT_EQUAL_INT16(-61, t.zone(i).rssi_dbm);
    }
    build(1, g5, 0, 255, 0, f.pkt);  // same colour, new packet: a new command
    TEST_ASSERT_TRUE(t.apply(f, 3100));
}

void test_tracker_p0(void) {
    RxTracker t;
    RxFrame f{0, {}};
    const uint8_t g23[4] = {0x00, 0x0C, 0x00, 0x0F}, g9[4] = {0x00, 0x00, 0x02, 0x0F};
    build(0, g23, 0, 0, 255, f.pkt);
    t.apply(f, 0);
    TEST_ASSERT_EQUAL_UINT8(2, t.count());
    TEST_ASSERT_EQUAL_UINT8(2, t.zone(0).group);
    TEST_ASSERT_EQUAL_UINT8(3, t.zone(1).group);
    TEST_ASSERT_EQUAL_STRING("blue", t.zone(1).label);
    build(0, g9, 255, 0, 0, f.pkt);
    t.apply(f, 1000);
    TEST_ASSERT_EQUAL_UINT8(9, t.zone(2).group);

    build_packet(0, g23, ACTION_FX_B, 0, 0, 0, 16, f.pkt);  // fade out: dark
    t.apply(f, 2000);
    TEST_ASSERT_EQUAL_STRING("fade out", t.zone(0).label);
    TEST_ASSERT_EQUAL_UINT8(0, t.zone(0).b);
    build_packet(0, P0_ALL, ACTION_COLOR, 255, 255, 0, 16, f.pkt);  // every group: yellow
    t.apply(f, 3000);
    TEST_ASSERT_EQUAL_UINT8(4, t.count());
    TEST_ASSERT_EQUAL_UINT8(RxTracker::ALL_GROUPS, t.zone(0).group);
    for (uint8_t i = 0; i < 4; i++)
        TEST_ASSERT_EQUAL_STRING("yellow", t.zone(i).label);
    build_packet(0, g23, ACTION_FX_A, 0, 0, 0, 16, f.pkt);  // fade in keeps the colour
    t.apply(f, 4000);
    TEST_ASSERT_EQUAL_STRING("fade in", t.zone(1).label);
    TEST_ASSERT_EQUAL_UINT8(255, t.zone(1).r);
    TEST_ASSERT_EQUAL_UINT8(255, t.zone(1).g);

    const uint8_t none[4] = {0x00, 0x00, 0x00, 0x0F};  // an empty mask reaches nobody
    build(0, none, 255, 0, 0, f.pkt);
    t.apply(f, 5000);
    TEST_ASSERT_EQUAL_UINT8(4, t.count());
}

void test_tracker_full(void) {
    RxTracker t;
    RxFrame f{1, {}};
    for (int g = 1; g <= 40; g++) {
        const uint8_t a[2] = {(uint8_t) g, 0xFF};
        build(1, a, 255, 255, 255, f.pkt);
        t.apply(f, g * 1000);
    }
    TEST_ASSERT_EQUAL_UINT8(RxTracker::MAX_ZONES, t.count());
    TEST_ASSERT_EQUAL_UINT8(32, t.zone(31).group);
    TEST_ASSERT_EQUAL_UINT32(40, t.updates());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_both_protocols);
    RUN_TEST(test_roundtrip_with_timing_error);
    RUN_TEST(test_noise_and_bad_checksum);
    RUN_TEST(test_vendor_sync_variant);
    RUN_TEST(test_legacy_checksum_accepted);
    RUN_TEST(test_tracker_p1);
    RUN_TEST(test_tracker_p0);
    RUN_TEST(test_tracker_full);
    return UNITY_END();
}
