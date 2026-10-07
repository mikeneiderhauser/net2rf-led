// Host tests for rf_rx.h: frames encoded by rf_protocol.h decode back to the same packets, with
// realistic timing errors and noise, and the zone tracker keeps the right colour per group.
// Run: pio test -e native
#include <unity.h>

#include <cstdlib>
#include <vector>

#include "rf_protocol.h"
#include "rf_rx.h"
#include "pulse_capture.h"

#include <string>

using namespace rfproto;

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

// Collects bursts from a pulse stream the way the engine does.
struct CaptureSink {
    BurstSegmenter seg;
    BurstStore<3> store;
    std::vector<uint16_t> counts;
    uint32_t now{0};
    void push(uint8_t level, uint32_t us) {
        if (this->seg.push(level, us)) {
            this->counts.push_back(this->seg.count());
            this->store.add(this->seg, ++this->now, false, -60);
        }
    }
    void mark(uint32_t us) { this->push(1, us); }
    void space(uint32_t us) { this->push(0, us); }
};

void test_capture_bursts(void) {
    srand(7);
    CaptureSink c;
    for (int i = 0; i < 3000; i++)  // empty channel: glitches, far shorter than any remote's pulse
        c.push(i & 1, 5 + rand() % 50);
    TEST_ASSERT_EQUAL(0, c.counts.size());

    uint8_t pkt[PACKET_LEN];
    const uint8_t g1[2] = {1, 0xFF};
    build(1, g1, 255, 0, 0, pkt);
    for (int i = 0; i < 3; i++)
        encode_frame(1, pkt, c);
    c.push(0, 30000);  // the gap after the transmission ends it
    TEST_ASSERT_EQUAL(1, c.counts.size());
    TEST_ASSERT_EQUAL_UINT16(3 * 114, c.counts[0]);  // every mark and space of the three frames
    const RawBurst &b = c.store.at(0);
    TEST_ASSERT_EQUAL_INT16(200, b.pulses[0]);    // sync mark
    TEST_ASSERT_EQUAL_INT16(-1600, b.pulses[1]);  // sync space
    TEST_ASSERT_FALSE(b.truncated);

    // a burst that just stops (no closing edge): idle() ends it
    BurstSegmenter s;
    for (int i = 0; i < 40; i++)
        s.push(i & 1 ? 0 : 1, 400);
    TEST_ASSERT_TRUE(s.open());
    TEST_ASSERT_FALSE(s.idle(2000));
    TEST_ASSERT_TRUE(s.idle(9000));
    TEST_ASSERT_EQUAL_UINT16(40, s.count());
    TEST_ASSERT_EQUAL_UINT32(16000, s.total_us());

    // too short to be a signal: dropped
    BurstSegmenter t;
    for (int i = 0; i < 10; i++)
        TEST_ASSERT_FALSE(t.push(i & 1 ? 0 : 1, 400));
    TEST_ASSERT_FALSE(t.push(0, 20000));
}

void test_capture_truncation_and_store(void) {
    BurstSegmenter s;
    for (int i = 0; i < 1500; i++)
        s.push(i & 1 ? 0 : 1, 300);
    TEST_ASSERT_TRUE(s.push(0, 20000));
    TEST_ASSERT_EQUAL_UINT16(BurstSegmenter::MAX_PULSES, s.count());
    TEST_ASSERT_TRUE(s.truncated());

    BurstStore<3> st;
    st.add(s, 1, true, -50);    // id 1, decoded
    st.add(s, 2, false, -50);   // id 2
    st.add(s, 3, true, -50);    // id 3, decoded
    st.add(s, 4, false, -50);   // full: replaces the oldest decoded (id 1)
    TEST_ASSERT_NULL(st.find(1));
    TEST_ASSERT_NOT_NULL(st.find(2));
    st.add(s, 5, false, -50);   // replaces id 3, the remaining decoded one
    TEST_ASSERT_NULL(st.find(3));
    st.add(s, 6, false, -50);   // none decoded: the oldest (id 2)
    TEST_ASSERT_NULL(st.find(2));
    TEST_ASSERT_EQUAL_UINT8(3, st.size());
}

void test_capture_export(void) {
    BurstSegmenter s;
    const int32_t d[] = {200, -1600, 600, -200, 200, -600};
    for (int r = 0; r < 6; r++)
        for (int32_t v : d)
            s.push(v > 0, (uint32_t) (v > 0 ? v : -v));
    s.push(1, 600);  // ends on a mark
    TEST_ASSERT_TRUE(s.idle(50000));
    BurstStore<1> st;
    const RawBurst &b = st.add(s, 1, false, -70);
    std::string out;
    uint16_t n = export_ook(b, 433904500, [&](const char *t) { out += t; });
    TEST_ASSERT_EQUAL_UINT16(19, n);
    TEST_ASSERT_TRUE(out.rfind(";pulse data\n;version 1\n;timescale 1us\n;freq1 433904500\n;ook 19 pulses\n200 1600\n600 200\n", 0) == 0);
    TEST_ASSERT_TRUE(out.find("\n600 10000\n;end\n") != std::string::npos);  // the last mark gets a closing gap
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
    RUN_TEST(test_capture_bursts);
    RUN_TEST(test_capture_truncation_and_store);
    RUN_TEST(test_capture_export);
    return UNITY_END();
}
