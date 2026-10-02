// Host tests for the register math in the cc1101_ook and sx1278_ook libraries.
// Reference values: TI SmartRF Studio (CC1101 433.92 MHz = FREQ 10 B0 71) and the Semtech SX127x
// datasheet formula (434 MHz = RegFrf 0x6C8000). Run: pio test -e native
#include <unity.h>

#include "cc1101_ook.h"
#include "sx1278_ook.h"

void test_cc1101_freq_word(void) {
    TEST_ASSERT_EQUAL_HEX32(0x10B071, cc1101_ook::freq_word(433920000));  // protocol 1
    TEST_ASSERT_EQUAL_HEX32(0x10B023, cc1101_ook::freq_word(433889000));  // protocol 0
}

void test_cc1101_pa_table(void) {
    TEST_ASSERT_EQUAL_HEX8(0xC0, cc1101_ook::pa_value_433(10));
    TEST_ASSERT_EQUAL_HEX8(0xC0, cc1101_ook::pa_value_433(15));  // above max -> highest step
    TEST_ASSERT_EQUAL_HEX8(0x60, cc1101_ook::pa_value_433(0));
    TEST_ASSERT_EQUAL_HEX8(0x34, cc1101_ook::pa_value_433(-5));  // between steps -> next lower
    TEST_ASSERT_EQUAL_HEX8(0x12, cc1101_ook::pa_value_433(-30));
    TEST_ASSERT_EQUAL_HEX8(0x12, cc1101_ook::pa_value_433(-60));  // below min -> lowest step
}

void test_sx1278_frf(void) {
    TEST_ASSERT_EQUAL_HEX32(0x6C8000, sx1278_ook::frf(434000000));
    TEST_ASSERT_EQUAL_HEX32(0x6C7AE1, sx1278_ook::frf(433920000));  // protocol 1
    TEST_ASSERT_EQUAL_HEX32(0x6C78E5, sx1278_ook::frf(433889000));  // protocol 0
}

void test_sx1278_pa_config(void) {
    TEST_ASSERT_EQUAL_HEX8(0xFF, sx1278_ook::pa_config(17));  // PA_BOOST, MaxPower 7, OutputPower 15
    TEST_ASSERT_EQUAL_HEX8(0xF0, sx1278_ook::pa_config(2));
    TEST_ASSERT_EQUAL_HEX8(0xF8, sx1278_ook::pa_config(10));
    TEST_ASSERT_EQUAL_HEX8(0xF0, sx1278_ook::pa_config(-5));  // clamped
    TEST_ASSERT_EQUAL_HEX8(0xFF, sx1278_ook::pa_config(20));  // clamped
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_cc1101_freq_word);
    RUN_TEST(test_cc1101_pa_table);
    RUN_TEST(test_sx1278_frf);
    RUN_TEST(test_sx1278_pa_config);
    return UNITY_END();
}
