#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "fpp_ping.h"

using namespace net2rf;

void setUp() {}
void tearDown() {}

void test_discover_detection() {
    uint8_t discover[207] = {'F', 'P', 'P', 'D', 0x04, 200, 0, 0x02, 0x01};  // what xLights sends
    TEST_ASSERT_TRUE(fpp_is_discover(discover, sizeof(discover)));
    discover[8] = 0x00;  // a plain ping (someone else's answer)
    TEST_ASSERT_FALSE(fpp_is_discover(discover, sizeof(discover)));
    uint8_t sync[] = {'F', 'P', 'P', 'D', 0x01, 0, 0, 0, 1};
    TEST_ASSERT_FALSE(fpp_is_discover(sync, sizeof(sync)));
    TEST_ASSERT_FALSE(fpp_is_discover((const uint8_t *) "FPPD", 4));
    TEST_ASSERT_FALSE(fpp_is_discover((const uint8_t *) "junkjunkjunk", 12));
}

void test_ping_layout() {
    uint8_t buf[FPP_PING_LEN + 8];
    memset(buf, 0xEE, sizeof(buf));
    const uint8_t ip[4] = {192, 168, 1, 60};
    size_t n = fpp_build_ping(buf, ip, "net2rf-24db", "0.0.4-3-gabc1234", 48);
    TEST_ASSERT_EQUAL_UINT(301, n);
    TEST_ASSERT_EQUAL_UINT8(0xEE, buf[n]);  // nothing written past the packet
    TEST_ASSERT_EQUAL_MEMORY("FPPD", buf, 4);
    TEST_ASSERT_EQUAL_UINT8(0x04, buf[4]);
    TEST_ASSERT_EQUAL_INT(294, buf[5] | (buf[6] << 8));  // how FPP and xLights read the length
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[8]);                // an answer, so nobody answers it in turn
    TEST_ASSERT_EQUAL_UINT8(0xC0, buf[9]);  // "other system": never another product's type code
    TEST_ASSERT_EQUAL_INT(0, (buf[10] << 8) | buf[11]);   // major
    TEST_ASSERT_EQUAL_INT(0, (buf[12] << 8) | buf[13]);   // minor
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[14]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(ip, buf + 15, 4);
    TEST_ASSERT_EQUAL_STRING("net2rf-24db", (const char *) buf + 19);
    TEST_ASSERT_EQUAL_STRING("0.0.4-3-gabc1234", (const char *) buf + 84);
    TEST_ASSERT_EQUAL_STRING("Net2RF-LED", (const char *) buf + 125);  // = the xLights definition ID
    TEST_ASSERT_EQUAL_STRING("0-47", (const char *) buf + 166);
}

void test_ping_versions_and_long_strings() {
    uint8_t buf[FPP_PING_LEN];
    const uint8_t ip[4] = {10, 0, 0, 2};
    fpp_build_ping(buf, ip, "h", "v1.12.3", 15);
    TEST_ASSERT_EQUAL_INT(1, (buf[10] << 8) | buf[11]);
    TEST_ASSERT_EQUAL_INT(12, (buf[12] << 8) | buf[13]);
    TEST_ASSERT_EQUAL_STRING("0-14", (const char *) buf + 166);

    char long_name[200];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    fpp_build_ping(buf, ip, long_name, long_name, 0);
    TEST_ASSERT_EQUAL_UINT(64, strlen((const char *) buf + 19));   // each string stays inside its field,
    TEST_ASSERT_EQUAL_UINT(40, strlen((const char *) buf + 84));   // NUL terminated: older FPP copies them with strcpy()
    TEST_ASSERT_EQUAL_STRING("", (const char *) buf + 166);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_discover_detection);
    RUN_TEST(test_ping_layout);
    RUN_TEST(test_ping_versions_and_long_strings);
    return UNITY_END();
}
