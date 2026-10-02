#include <unity.h>

#include <cstring>
#include <string>

#include "heartbeat.h"

using namespace net2rf;

void setUp() {}
void tearDown() {}

static Heartbeat sample() {
    Heartbeat h{};
    strcpy(h.id, "24DB");
    strcpy(h.name, "Front Yard");
    strcpy(h.host, "net2rf-24db");
    strcpy(h.fw, "1.2.0");
    strcpy(h.radio, "ready");
    strcpy(h.input, "live");
    h.out = true;
    h.test = false;
    h.zones = 4;
    h.air = 37;
    h.up = 86400;
    h.hello = true;
    return h;
}

static bool decode(const std::string &s, Heartbeat &h) {
    return decode_heartbeat((const uint8_t *) s.data(), s.size(), h);
}

void test_round_trip() {
    char buf[HEARTBEAT_MAX];
    Heartbeat in = sample(), out{};
    size_t n = encode_heartbeat(in, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < HEARTBEAT_MAX);
    TEST_ASSERT_TRUE(decode_heartbeat((const uint8_t *) buf, n, out));
    TEST_ASSERT_EQUAL_STRING("24DB", out.id);
    TEST_ASSERT_EQUAL_STRING("Front Yard", out.name);
    TEST_ASSERT_EQUAL_STRING("net2rf-24db", out.host);
    TEST_ASSERT_EQUAL_STRING("1.2.0", out.fw);
    TEST_ASSERT_EQUAL_STRING("ready", out.radio);
    TEST_ASSERT_EQUAL_STRING("live", out.input);
    TEST_ASSERT_TRUE(out.out);
    TEST_ASSERT_FALSE(out.test);
    TEST_ASSERT_EQUAL_UINT8(4, out.zones);
    TEST_ASSERT_EQUAL_UINT8(37, out.air);
    TEST_ASSERT_EQUAL_UINT32(86400, out.up);
    TEST_ASSERT_TRUE(out.hello);
}

void test_hello_omitted_when_false() {
    char buf[HEARTBEAT_MAX];
    Heartbeat in = sample(), out{};
    in.hello = false;
    size_t n = encode_heartbeat(in, buf, sizeof(buf));
    TEST_ASSERT_NULL(strstr(buf, "hello"));
    TEST_ASSERT_TRUE(decode_heartbeat((const uint8_t *) buf, n, out));
    TEST_ASSERT_FALSE(out.hello);
}

void test_rejects_foreign_packets() {
    Heartbeat h{};
    TEST_ASSERT_FALSE(decode("", h));
    TEST_ASSERT_FALSE(decode("not json", h));
    TEST_ASSERT_FALSE(decode("[1,2,3]", h));
    TEST_ASSERT_FALSE(decode("{\"p\":\"wled\",\"v\":1,\"id\":\"24DB\"}", h));        // other software
    TEST_ASSERT_FALSE(decode("{\"p\":\"net2rf\",\"v\":0,\"id\":\"24DB\"}", h));      // bad version
    TEST_ASSERT_FALSE(decode("{\"p\":\"net2rf\",\"v\":1}", h));                      // no id
    TEST_ASSERT_FALSE(decode("{\"p\":\"net2rf\",\"v\":1,\"id\":\"<img>\"}", h));     // not a hex id
    TEST_ASSERT_FALSE(decode("{\"p\":\"net2rf\",\"v\":1,\"id\":\"1234567\"}", h));   // id too long
    TEST_ASSERT_FALSE(decode("{\"p\":\"net2rf\",\"v\":1,\"id\":\"24DB\"", h));       // truncated
}

void test_newer_version_and_minimal_packet_accepted() {
    Heartbeat h{};
    TEST_ASSERT_TRUE(decode("{\"p\":\"net2rf\",\"v\":2,\"id\":\"abcd\",\"extra\":[1,2]}", h));
    TEST_ASSERT_EQUAL_STRING("abcd", h.id);
    TEST_ASSERT_EQUAL_STRING("", h.name);
    TEST_ASSERT_FALSE(h.out);
}

void test_long_fields_truncated_and_control_chars_replaced() {
    Heartbeat h{};
    std::string name(200, 'x');
    std::string pkt = "{\"p\":\"net2rf\",\"v\":1,\"id\":\"24DB\",\"name\":\"" + name + "\",\"host\":\"a\\nb\"}";
    TEST_ASSERT_TRUE(decode(pkt, h));
    TEST_ASSERT_EQUAL_size_t(sizeof(h.name) - 1, strlen(h.name));
    TEST_ASSERT_EQUAL_STRING("a_b", h.host);
}

void test_oversized_datagram_rejected() {
    Heartbeat h{};
    std::string pkt = "{\"p\":\"net2rf\",\"v\":1,\"id\":\"24DB\",\"pad\":\"" + std::string(500, 'x') + "\"}";
    TEST_ASSERT_FALSE(decode(pkt, h));
}

void test_encode_reports_small_buffer() {
    char buf[16];
    Heartbeat in = sample();
    TEST_ASSERT_EQUAL_size_t(0, encode_heartbeat(in, buf, sizeof(buf)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip);
    RUN_TEST(test_hello_omitted_when_false);
    RUN_TEST(test_rejects_foreign_packets);
    RUN_TEST(test_newer_version_and_minimal_packet_accepted);
    RUN_TEST(test_long_fields_truncated_and_control_chars_replaced);
    RUN_TEST(test_oversized_datagram_rejected);
    RUN_TEST(test_encode_reports_small_buffer);
    return UNITY_END();
}
