// Host tests for rf_protocol.h: protocol 0 against packets decoded from the Flipper Zero bracelet_led.fap app,
// protocol 1 against off-air captures of the vendor's DMX transmitter.
// Run: pio test -e native
#include <unity.h>

#include <vector>
#include <cstring>

#include "rf_protocol.h"

using namespace rfproto;

static const uint8_t ADDR_P0[4] = {0x00, 0xFF, 0xFF, 0x0F};
static const uint8_t ADDR_P1[2] = {0x00, 0xFF};

static void expect_packet(uint8_t protocol, Action action, uint8_t r, uint8_t g, uint8_t b,
                          std::vector<uint8_t> expected) {
    uint8_t pkt[PACKET_LEN];
    build_packet(protocol, protocol == 0 ? ADDR_P0 : ADDR_P1, action, r, g, b, 16, pkt);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected.data(), pkt, PACKET_LEN);
}

void test_p0_palette(void) {
    expect_packet(0, ACTION_COLOR, 255, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x00, 0x0E});
    expect_packet(0, ACTION_COLOR, 0, 255, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x01, 0x0F});
    expect_packet(0, ACTION_COLOR, 0, 0, 255, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x02, 0x10});
    expect_packet(0, ACTION_COLOR, 255, 255, 255, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x04, 0x12});
    expect_packet(0, ACTION_COLOR, 255, 0, 128, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x03, 0x11});
    expect_packet(0, ACTION_COLOR, 255, 128, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x09, 0x17});
    expect_packet(0, ACTION_COLOR, 255, 255, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x05, 0x13});
    expect_packet(0, ACTION_COLOR, 0, 255, 255, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x0B, 0x19});
    expect_packet(0, ACTION_COLOR, 75, 0, 130, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x0A, 0x18});
    expect_packet(0, ACTION_COLOR, 148, 0, 211, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x08, 0x16});
}

void test_p0_dim_and_off(void) {
    expect_packet(0, ACTION_COLOR, 60, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x01, 0x00, 0x0E});  // dim red is red
    expect_packet(0, ACTION_COLOR, 0, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x00, 0xAA, 0xB7});
    expect_packet(0, ACTION_OFF, 255, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x00, 0xAA, 0xB7});
}

void test_p0_effects(void) {
    expect_packet(0, ACTION_FX_A, 0, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x05, 0xAA, 0xBC});
    expect_packet(0, ACTION_FX_B, 0, 0, 0, {0x00, 0xFF, 0xFF, 0x0F, 0x06, 0xAA, 0xBD});
    expect_packet(0, ACTION_FX_C, 0, 0, 0, {0xD0, 0xFF, 0xFF, 0xFF, 0x55, 0x00, 0x22});
}

void test_p1_rgb(void) {
    expect_packet(1, ACTION_COLOR, 255, 0, 0, {0x55, 0x00, 0x00, 0xFF, 0xFF, 0x55, 0xFF});
    expect_packet(1, ACTION_COLOR, 255, 255, 255, {0x55, 0x00, 0x00, 0x00, 0x00, 0x55, 0xFF});
    expect_packet(1, ACTION_COLOR, 255, 0, 128, {0x55, 0x00, 0x00, 0xFF, 0x7F, 0xD5, 0xFF});
    expect_packet(1, ACTION_COLOR, 75, 0, 130, {0x55, 0x00, 0xB4, 0xFF, 0x7D, 0x63, 0xFF});
    expect_packet(1, ACTION_COLOR, 148, 0, 211, {0x55, 0x00, 0x6B, 0xFF, 0x2C, 0xED, 0xFF});
    expect_packet(1, ACTION_COLOR, 0, 0, 0, {0x55, 0x00, 0xFF, 0xFF, 0xFF, 0xAA, 0xFF});
    expect_packet(1, ACTION_OFF, 255, 0, 0, {0x55, 0x00, 0xFF, 0xFF, 0xFF, 0xAA, 0xFF});
}

// Payloads captured off the air from the vendor's DMX-to-RF transmitter with an RTL-SDR (CrispyPyro/
// Wireless_DMX_Receiver, docs/gflai-protocol.md, appendix A), written as packet bytes 1-5. The DMX input for each
// row is the colour bytes inverted. One table row (Red, group 8) contradicts its neighbours and is left out.
struct VendorCapture {
    const char *name;
    uint8_t group, rr, gg, bb, ck;
};
static const VendorCapture VENDOR_CAPTURES[] = {
    {"Red", 0, 0x00, 0xFF, 0xFF, 0x55},    {"Green", 0, 0xFF, 0x7F, 0xFF, 0x2A},   {"Blue", 0, 0xFF, 0xFF, 0x00, 0x55},
    {"White", 0, 0x00, 0x00, 0x00, 0x55},  {"Black", 0, 0xFF, 0xFF, 0xFF, 0xAA},   {"Yellow", 0, 0x40, 0xBF, 0xFF, 0x55},
    {"Purple", 0, 0x82, 0xFF, 0x7C, 0x54}, {"Aqua", 0, 0xFF, 0x54, 0xAA, 0x54},    {"Orange", 0, 0x20, 0xDF, 0xFF, 0x55},
    {"Pink", 0, 0x53, 0xFF, 0xAC, 0x55},   {"Red", 1, 0x00, 0xFF, 0xFF, 0x54},     {"Green", 1, 0xFF, 0x7F, 0xFF, 0x2B},
    {"Blue", 1, 0xFF, 0xFF, 0x00, 0x54},   {"White", 1, 0x00, 0x00, 0x00, 0x54},   {"Black", 1, 0xFF, 0xFF, 0xFF, 0xAB},
    {"Red", 2, 0x00, 0xFF, 0xFF, 0x57},    {"Green", 2, 0xFF, 0x7F, 0xFF, 0x28},   {"Blue", 2, 0xFF, 0xFF, 0x00, 0x57},
    {"White", 2, 0x00, 0x00, 0x00, 0x57},  {"Black", 2, 0xFF, 0xFF, 0xFF, 0xA8},   {"Red", 3, 0x00, 0xFF, 0xFF, 0x56},
    {"Green", 3, 0xFF, 0x7F, 0xFF, 0x29},  {"Blue", 3, 0xFF, 0xFF, 0x00, 0x56},    {"White", 3, 0x00, 0x00, 0x00, 0x56},
    {"Black", 3, 0xFF, 0xFF, 0xFF, 0xA9},  {"Red", 4, 0x00, 0xFF, 0xFF, 0x51},     {"Green", 4, 0xFF, 0x7F, 0xFF, 0x2E},
    {"Blue", 4, 0xFF, 0xFF, 0x00, 0x51},   {"White", 4, 0x00, 0x00, 0x00, 0x51},   {"Black", 4, 0xFF, 0xFF, 0xFF, 0xAE},
    {"Red", 5, 0x00, 0xFF, 0xFF, 0x50},    {"Red", 6, 0x00, 0xFF, 0xFF, 0x53},     {"Red", 7, 0x00, 0xFF, 0xFF, 0x52},
    {"Lime", 8, 0xFF, 0x00, 0xFF, 0x5D},   {"Blue", 8, 0xFF, 0xFF, 0x00, 0x5D},    {"White", 8, 0x00, 0x00, 0x00, 0x5D},
    {"Black", 8, 0xFF, 0xFF, 0xFF, 0xA2},  {"Purple", 8, 0x7F, 0xFF, 0x7F, 0xA2},  {"Red", 9, 0x00, 0xFF, 0xFF, 0x5C},
    {"Red", 10, 0x00, 0xFF, 0xFF, 0x5F},   {"Red", 11, 0x00, 0xFF, 0xFF, 0x5E},    {"Red", 14, 0x00, 0xFF, 0xFF, 0x5B},
    {"Green", 14, 0xFF, 0x7F, 0xFF, 0x24}, {"Blue", 14, 0xFF, 0xFF, 0x00, 0x5B},   {"White", 14, 0x00, 0x00, 0x00, 0x5B},
    {"Black", 14, 0xFF, 0xFF, 0xFF, 0xA4}, {"Red", 79, 0x00, 0xFF, 0xFF, 0x1A},    {"Red", 82, 0x00, 0xFF, 0xFF, 0x07},
    {"Red", 83, 0x00, 0xFF, 0xFF, 0x06},   {"Gray", 0, 0x7F, 0x7F, 0x7F, 0x2A},    {"Red", 69, 0x00, 0xFF, 0xFF, 0x10},
    {"Red", 72, 0x00, 0xFF, 0xFF, 0x1D},   {"Lime", 69, 0xFF, 0x00, 0xFF, 0x10},   {"White", 69, 0x00, 0x00, 0x00, 0x10},
    {"Black", 69, 0xFF, 0xFF, 0xFF, 0xEF}, {"Purple", 69, 0x7F, 0xFF, 0x7F, 0xEF}, {"Black", 5, 0xFF, 0xFF, 0xFF, 0xAF},
    {"Black", 6, 0xFF, 0xFF, 0xFF, 0xAC},  {"Black", 7, 0xFF, 0xFF, 0xFF, 0xAD},   {"Black", 9, 0xFF, 0xFF, 0xFF, 0xA3},
    {"Black", 10, 0xFF, 0xFF, 0xFF, 0xA0},
};

void test_p1_vendor_captures(void) {
    for (const VendorCapture &c : VENDOR_CAPTURES) {
        const uint8_t addr[2] = {c.group, 0xFF};
        const uint8_t expected[PACKET_LEN] = {0x55, c.group, c.rr, c.gg, c.bb, c.ck, 0xFF};
        uint8_t pkt[PACKET_LEN];
        build_packet(1, addr, ACTION_COLOR, p1_level(c.rr), p1_level(c.gg), p1_level(c.bb), 16, pkt);
        TEST_ASSERT_EQUAL_HEX8_ARRAY_MESSAGE(expected, pkt, PACKET_LEN, c.name);
    }
}

void test_p1_full_range(void) {
    for (int v = 0; v < 256; v++) {
        TEST_ASSERT_EQUAL_HEX8((uint8_t) (255 - v), p1_channel((uint8_t) v));
        TEST_ASSERT_EQUAL_UINT8((uint8_t) v, p1_level(p1_channel((uint8_t) v)));
    }
}

struct DurationSink {
    std::vector<int32_t> t;
    void mark(uint32_t us) { t.push_back((int32_t) us); }
    void space(uint32_t us) { t.push_back(-(int32_t) us); }
};

void test_frame_timing(void) {
    for (uint8_t p = 0; p < 2; p++) {
        uint8_t pkt[PACKET_LEN];
        build_packet(p, p == 0 ? ADDR_P0 : ADDR_P1, ACTION_COLOR, 255, 0, 0, 16, pkt);
        DurationSink sink;
        encode_frame(p, pkt, sink);
        uint32_t total = 0;
        for (int32_t v : sink.t)
            total += v < 0 ? -v : v;
        TEST_ASSERT_EQUAL_UINT32(frame_us(p), total);
        TEST_ASSERT_EQUAL(p == 0 ? 116 : 114, sink.t.size());
        TEST_ASSERT_TRUE(sink.t.front() > 0);  // starts with a mark
    }
}

void test_color_order(void) {
    struct Case {
        uint8_t order;
        uint8_t px[3];
    } cases[] = {{ORDER_RGB, {0x11, 0x22, 0x33}}, {ORDER_RBG, {0x11, 0x33, 0x22}}, {ORDER_GRB, {0x22, 0x11, 0x33}},
                 {ORDER_GBR, {0x22, 0x33, 0x11}}, {ORDER_BRG, {0x33, 0x11, 0x22}}, {ORDER_BGR, {0x33, 0x22, 0x11}}};
    for (auto &c : cases) {
        uint8_t r, g, b;
        extract_rgb(c.order, c.px, &r, &g, &b);
        TEST_ASSERT_EQUAL_HEX8(0x11, r);
        TEST_ASSERT_EQUAL_HEX8(0x22, g);
        TEST_ASSERT_EQUAL_HEX8(0x33, b);
    }
}

void test_fx_channel(void) {
    TEST_ASSERT_EQUAL(ACTION_COLOR, fx_action(0));
    TEST_ASSERT_EQUAL(ACTION_OFF, fx_action(25));
    TEST_ASSERT_EQUAL(ACTION_FX_A, fx_action(50));
    TEST_ASSERT_EQUAL(ACTION_FX_B, fx_action(70));
    TEST_ASSERT_EQUAL(ACTION_FX_C, fx_action(90));
    TEST_ASSERT_EQUAL(ACTION_COLOR, fx_action(200));
}

// All Zones (index 0) + zones for groups 1, 2, 3, as in the default layout.
static void layer_setup(uint8_t wants[4][7], uint8_t addrs[4][4], const uint8_t colours[4][3]) {
    const uint8_t a[4][4] = {{0, 0xFF, 0xFF, 0x0F}, {0, 0x02, 0, 0x0F}, {0, 0x04, 0, 0x0F}, {0, 0x08, 0, 0x0F}};
    memcpy(addrs, a, sizeof(a));
    for (int i = 0; i < 4; i++)
        build_packet(0, addrs[i], ACTION_COLOR, colours[i][0], colours[i][1], colours[i][2], 16, wants[i]);
}

void test_p0_base_layer() {
    uint8_t wants[4][7], addrs[4][4], expect[7];
    const bool active[4] = {true, true, true, true};
    uint16_t follows, mask;

    // All Zones red, zones black: one broadcast to everyone, and every zone follows it
    const uint8_t c1[4][3] = {{255, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    layer_setup(wants, addrs, c1);
    TEST_ASSERT_EQUAL_INT(0, p0_apply_base_layer(wants, addrs, active, 4, &follows, &mask));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, mask);
    TEST_ASSERT_EQUAL_HEX16(0x000E, follows);
    build_packet(0, addrs[0], ACTION_COLOR, 255, 0, 0, 16, expect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[0], 7);
    build_packet(0, addrs[2], ACTION_COLOR, 255, 0, 0, 16, expect);  // zone 2 "shows" red at its own address
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[2], 7);

    // Zone 2 (group 2) blue over All Zones green: the broadcast skips group 2 (00 FB FF 0F), zone 2 keeps blue
    const uint8_t c2[4][3] = {{0, 255, 0}, {0, 0, 0}, {0, 0, 255}, {0, 0, 0}};
    layer_setup(wants, addrs, c2);
    p0_apply_base_layer(wants, addrs, active, 4, &follows, &mask);
    TEST_ASSERT_EQUAL_HEX16(0xFBFF, mask);
    TEST_ASSERT_EQUAL_HEX16(0x000A, follows);
    const uint8_t all_but_2[4] = {0, 0xFB, 0xFF, 0x0F};
    build_packet(0, all_but_2, ACTION_COLOR, 0, 255, 0, 16, expect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[0], 7);
    build_packet(0, addrs[2], ACTION_COLOR, 0, 0, 255, 16, expect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[2], 7);

    // All Zones black while zone 2 holds blue: "off" goes to everyone except group 2
    const uint8_t c3[4][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 255}, {0, 0, 0}};
    layer_setup(wants, addrs, c3);
    p0_apply_base_layer(wants, addrs, active, 4, &follows, &mask);
    build_packet(0, all_but_2, ACTION_OFF, 0, 0, 0, 16, expect);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[0], 7);
    build_packet(0, addrs[1], ACTION_OFF, 0, 0, 0, 16, expect);  // zone 1 follows: off
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, wants[1], 7);

    // a zone that wants the base's own colour follows it: no cut-out, no packet of its own
    const uint8_t c4[4][3] = {{255, 0, 0}, {255, 0, 0}, {0, 0, 255}, {255, 0, 0}};
    layer_setup(wants, addrs, c4);
    p0_apply_base_layer(wants, addrs, active, 4, &follows, &mask);
    TEST_ASSERT_EQUAL_HEX16(0xFBFF, mask);    // only zone 2 (blue) is cut out
    TEST_ASSERT_EQUAL_HEX16(0x000A, follows);  // zones 1 and 3 ride on the broadcast

    // a disabled zone neither follows nor is cut out; without an all-groups zone nothing changes
    const bool no_z2[4] = {true, true, false, true};
    layer_setup(wants, addrs, c2);
    p0_apply_base_layer(wants, addrs, no_z2, 4, &follows, &mask);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, mask);
    TEST_ASSERT_EQUAL_HEX16(0x000A, follows);
    const bool no_base[4] = {false, true, true, true};
    layer_setup(wants, addrs, c2);
    uint8_t before[4][7];
    memcpy(before, wants, sizeof(before));
    TEST_ASSERT_EQUAL_INT(-1, p0_apply_base_layer(wants, addrs, no_base, 4, &follows, &mask));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(before, wants, sizeof(before));
}

void test_p0_merge() {
    const uint8_t g1[4] = {0, 0x02, 0, 0x0F}, g2[4] = {0, 0x04, 0, 0x0F}, g9[4] = {0, 0, 0x02, 0x0F};
    uint8_t a[7], b[7], c[7], blue[7], off[7], expect[7];
    build_packet(0, g1, ACTION_COLOR, 255, 0, 0, 16, a);
    build_packet(0, g2, ACTION_COLOR, 255, 0, 0, 16, b);
    build_packet(0, g9, ACTION_COLOR, 255, 0, 0, 16, c);
    build_packet(0, g2, ACTION_COLOR, 0, 0, 255, 16, blue);
    build_packet(0, g2, ACTION_OFF, 0, 0, 0, 16, off);
    TEST_ASSERT_TRUE(p0_can_merge(a, b));
    TEST_ASSERT_TRUE(p0_can_merge(a, c));
    TEST_ASSERT_FALSE(p0_can_merge(a, blue));
    TEST_ASSERT_FALSE(p0_can_merge(a, off));
    p0_merge(a, b);
    p0_merge(a, c);
    const uint8_t groups_1_2_9[4] = {0, 0x06, 0x02, 0x0F};
    build_packet(0, groups_1_2_9, ACTION_COLOR, 255, 0, 0, 16, expect);  // same as building it for the combined mask
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, a, 7);

    uint8_t fx_c[7], fx_c2[7];
    build_packet(0, g1, ACTION_FX_C, 0, 0, 0, 16, fx_c);
    build_packet(0, g2, ACTION_FX_C, 0, 0, 0, 16, fx_c2);
    TEST_ASSERT_FALSE(p0_can_merge(fx_c, fx_c2));  // the fixed effect packet carries no group mask
}

void test_address_breadth() {
    const uint8_t all0[7] = {0x00, 0xFF, 0xFF, 0x0F, 1, 0, 0}, g3[7] = {0x00, 0x08, 0x00, 0x0F, 1, 0, 0};
    const uint8_t g23[7] = {0x00, 0x0C, 0x00, 0x0F, 1, 0, 0}, g12[7] = {0x00, 0x00, 0x10, 0x0F, 1, 0, 0};
    const uint8_t none[7] = {0x00, 0x00, 0x00, 0x0F, 1, 0, 0};
    TEST_ASSERT_EQUAL_UINT8(16, address_breadth(0, all0));
    TEST_ASSERT_EQUAL_UINT8(1, address_breadth(0, g3));
    TEST_ASSERT_EQUAL_UINT8(2, address_breadth(0, g23));
    TEST_ASSERT_EQUAL_UINT8(1, address_breadth(0, g12));
    TEST_ASSERT_EQUAL_UINT8(0, address_breadth(0, none));
    const uint8_t p1_all[7] = {0x55, 0x00, 0x0F, 0x0F, 0x0F, 0, 0xFF}, p1_g2[7] = {0x55, 0x02, 0x0F, 0x0F, 0x0F, 0, 0xFF};
    TEST_ASSERT_TRUE(address_breadth(1, p1_all) > address_breadth(1, p1_g2));  // broadcast beats any single group
    TEST_ASSERT_TRUE(address_breadth(1, p1_all) > address_breadth(0, all0));

    TEST_ASSERT_TRUE(addresses_overlap(0, all0, g3));
    TEST_ASSERT_TRUE(addresses_overlap(0, g23, g3));
    TEST_ASSERT_TRUE(addresses_overlap(0, all0, g12));
    TEST_ASSERT_FALSE(addresses_overlap(0, g3, g12));
    TEST_ASSERT_FALSE(addresses_overlap(0, none, all0));
    const uint8_t p1_g3[7] = {0x55, 0x03, 0x0F, 0x0F, 0x0F, 0, 0xFF};
    TEST_ASSERT_TRUE(addresses_overlap(1, p1_all, p1_g2));
    TEST_ASSERT_TRUE(addresses_overlap(1, p1_g2, p1_g2));
    TEST_ASSERT_FALSE(addresses_overlap(1, p1_g2, p1_g3));
}

void test_fix_checksum(void) {
    uint8_t p0[7] = {0x00, 0x01, 0x00, 0x0F, 0x01, 0x02, 0x00};
    fix_checksum(0, p0);
    TEST_ASSERT_EQUAL_HEX8((0x00 + 0x01 + 0x00 + 0x0F + 0x01 + 0x02) & 0xFF, p0[6]);
    uint8_t p1[7] = {0x55, 0x03, 0x0F, 0xFF, 0xFF, 0x00, 0xFF};
    fix_checksum(1, p1);
    TEST_ASSERT_EQUAL_HEX8(0x03 ^ 0x0F ^ 0x55, p1[5]);  // the group code is part of the checksum
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_p0_palette);
    RUN_TEST(test_p0_dim_and_off);
    RUN_TEST(test_p0_effects);
    RUN_TEST(test_p1_rgb);
    RUN_TEST(test_p1_vendor_captures);
    RUN_TEST(test_p1_full_range);
    RUN_TEST(test_frame_timing);
    RUN_TEST(test_color_order);
    RUN_TEST(test_fx_channel);
    RUN_TEST(test_fix_checksum);
    RUN_TEST(test_address_breadth);
    RUN_TEST(test_p0_base_layer);
    RUN_TEST(test_p0_merge);
    return UNITY_END();
}
