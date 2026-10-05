#include <unity.h>

#include "version.h"

using namespace net2rf;

void setUp() {}
void tearDown() {}

void test_compare_versions() {
    TEST_ASSERT_EQUAL_INT(0, compare_versions("v0.0.3", "0.0.3"));
    TEST_ASSERT_TRUE(compare_versions("v0.0.4", "0.0.3") > 0);
    TEST_ASSERT_TRUE(compare_versions("v0.0.3", "0.0.4") < 0);
    TEST_ASSERT_TRUE(compare_versions("v0.1.0", "0.0.9") > 0);
    TEST_ASSERT_TRUE(compare_versions("v1.0.0", "0.9.9") > 0);
    TEST_ASSERT_TRUE(compare_versions("v0.0.10", "0.0.9") > 0);  // numeric, not text
    // a development build counts as the release it was built on
    TEST_ASSERT_EQUAL_INT(0, compare_versions("v0.0.3", "0.0.3-4-gb697235"));
    TEST_ASSERT_EQUAL_INT(0, compare_versions("v0.0.3", "0.0.3-4-gb697235-dirty"));
    TEST_ASSERT_TRUE(compare_versions("v0.0.4", "0.0.3-4-gb697235") > 0);
    // odd input doesn't crash and sorts low
    TEST_ASSERT_TRUE(compare_versions("v0.0.1", "dev") > 0);
    TEST_ASSERT_EQUAL_INT(0, compare_versions("", ""));
    TEST_ASSERT_TRUE(compare_versions("v1.2", "1.1.9") > 0);
}

void test_tag_from_release_url() {
    char tag[41];
    TEST_ASSERT_TRUE(tag_from_release_url("https://github.com/mikeneiderhauser/net2rf-led/releases/tag/v0.0.3", tag, sizeof(tag)));
    TEST_ASSERT_EQUAL_STRING("v0.0.3", tag);
    TEST_ASSERT_TRUE(tag_from_release_url("https://github.com/a/b/releases/tag/v1.2.3?x=1", tag, sizeof(tag)));
    TEST_ASSERT_EQUAL_STRING("v1.2.3", tag);
    TEST_ASSERT_FALSE(tag_from_release_url("https://github.com/a/b/releases", tag, sizeof(tag)));       // no releases
    TEST_ASSERT_FALSE(tag_from_release_url("https://github.com/a/b/releases/tag/", tag, sizeof(tag)));
    TEST_ASSERT_FALSE(tag_from_release_url("https://github.com/a/b/releases/tag/v1/../x", tag, sizeof(tag)));
    char tiny[4];
    TEST_ASSERT_FALSE(tag_from_release_url("https://github.com/a/b/releases/tag/v0.0.3", tiny, sizeof(tiny)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_compare_versions);
    RUN_TEST(test_tag_from_release_url);
    return UNITY_END();
}
