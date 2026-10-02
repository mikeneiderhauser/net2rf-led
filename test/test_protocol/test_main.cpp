// Host tests for bracelet_protocol.h against packets decoded from the Flipper Zero bracelet_led.fap app.
// Run: pio test -e native
#include <unity.h>

#include <vector>

#include "bracelet_protocol.h"

using namespace bracelet;

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
    expect_packet(1, ACTION_COLOR, 255, 0, 0, {0x55, 0x00, 0x0F, 0xFF, 0xFF, 0x55, 0xFF});
    expect_packet(1, ACTION_COLOR, 255, 255, 255, {0x55, 0x00, 0x0F, 0x0F, 0x0F, 0x55, 0xFF});
    expect_packet(1, ACTION_COLOR, 255, 0, 128, {0x55, 0x00, 0x0F, 0xFF, 0x7F, 0xD5, 0xFF});
    expect_packet(1, ACTION_COLOR, 75, 0, 130, {0x55, 0x00, 0xBF, 0xFF, 0x7F, 0x65, 0xFF});
    expect_packet(1, ACTION_COLOR, 148, 0, 211, {0x55, 0x00, 0x6F, 0xFF, 0x3F, 0xF5, 0xFF});
    expect_packet(1, ACTION_COLOR, 0, 0, 0, {0x55, 0x00, 0xFF, 0xFF, 0xFF, 0xA5, 0xFF});
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

void test_fix_checksum(void) {
    uint8_t p0[7] = {0x00, 0x01, 0x00, 0x0F, 0x01, 0x02, 0x00};
    fix_checksum(0, p0);
    TEST_ASSERT_EQUAL_HEX8((0x00 + 0x01 + 0x00 + 0x0F + 0x01 + 0x02) & 0xFF, p0[6]);
    uint8_t p1[7] = {0x55, 0x03, 0x0F, 0xFF, 0xFF, 0x00, 0xFF};
    fix_checksum(1, p1);
    TEST_ASSERT_EQUAL_HEX8(0x55, p1[5]);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_p0_palette);
    RUN_TEST(test_p0_dim_and_off);
    RUN_TEST(test_p0_effects);
    RUN_TEST(test_p1_rgb);
    RUN_TEST(test_frame_timing);
    RUN_TEST(test_color_order);
    RUN_TEST(test_fx_channel);
    RUN_TEST(test_fix_checksum);
    return UNITY_END();
}
