#include <unity.h>

#include <cstring>

#include "wled_compat.h"

using namespace net2rf;

void setUp() {}
void tearDown() {}

static WledConfig base() { return WledConfig{5, true, false, true, 1}; }

void test_info_passes_xlights_checks() {
    JsonDocument doc;
    wled_info_json(doc.to<JsonObject>(), "Front Yard", "0.0.4", 5);
    // xLights' WLED driver needs these four keys, and a build id it accepts
    TEST_ASSERT_TRUE(doc["ver"].is<const char *>());
    TEST_ASSERT_TRUE(doc["arch"].is<const char *>());
    TEST_ASSERT_EQUAL_STRING("Front Yard", doc["name"]);
    int vid = doc["vid"];
    TEST_ASSERT_TRUE(vid >= 2105110);
    TEST_ASSERT_FALSE(vid > 2112080 && vid < 2203190);
    TEST_ASSERT_TRUE(strcmp(doc["brand"], "WLED") != 0);  // stay out of WLED discovery
}

void test_cfg_round_trip() {
    WledConfig c = base();
    JsonDocument doc;
    wled_cfg_json(doc.to<JsonObject>(), c);
    TEST_ASSERT_EQUAL_INT(5, doc["hw"]["led"]["ins"][0]["len"]);
    TEST_ASSERT_EQUAL_INT(1, doc["hw"]["led"]["ins"][0]["order"]);  // WLED's code for RGB
    TEST_ASSERT_EQUAL_INT(4048, doc["if"]["live"]["port"]);
    WledConfig back = WledConfig{0, false, false, false, 0};
    TEST_ASSERT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), back, 16));
    TEST_ASSERT_EQUAL_UINT8(5, back.pixels);
    TEST_ASSERT_TRUE(back.ddp);
    TEST_ASSERT_FALSE(back.e131);
}

void test_xlights_upload() {
    // what xLights posts for a 16-node model (it sends colour order GRB by default: ignored), E1.31 universe 7
    JsonDocument doc;
    deserializeJson(doc, R"({"hw":{"led":{"total":16,"ins":[{"len":16,"start":0,"pin":[33],"type":22,"order":0,
        "rev":false,"skip":0,"ref":false}]}},"if":{"live":{"en":true,"port":5568,"mc":true,"dmx":{"uni":7,"addr":1}}},
        "rb":true})");
    WledConfig c = base();
    TEST_ASSERT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), c, 16));
    TEST_ASSERT_EQUAL_UINT8(16, c.pixels);
    TEST_ASSERT_TRUE(c.e131);
    TEST_ASSERT_EQUAL_UINT16(7, c.universe);
    TEST_ASSERT_TRUE(c.multicast);
    TEST_ASSERT_TRUE(c.ddp);  // an upload never switches an input off
}

void test_rejects_what_cannot_work() {
    WledConfig c = base();
    JsonDocument doc;
    deserializeJson(doc, R"({"hw":{"led":{"ins":[{"len":17,"order":1}]}}})");
    TEST_ASSERT_NOT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), c, 16));
    deserializeJson(doc, R"({"hw":{"led":{"ins":[{"len":2,"order":1},{"len":2,"order":1}]}}})");
    TEST_ASSERT_NOT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), c, 16));
    deserializeJson(doc, R"({"if":{"live":{"port":6454}}})");  // Art-Net
    TEST_ASSERT_NOT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), c, 16));
    TEST_ASSERT_EQUAL_UINT8(5, c.pixels);  // nothing was changed by the failed uploads

    // no models on the port (xLights sends null) and no input section: nothing to do, not an error
    deserializeJson(doc, R"({"hw":{"led":{"total":0,"ins":null}},"rb":true})");
    TEST_ASSERT_NULL(wled_cfg_apply(doc.as<JsonObjectConst>(), c, 16));
    TEST_ASSERT_EQUAL_UINT8(5, c.pixels);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_info_passes_xlights_checks);
    RUN_TEST(test_cfg_round_trip);
    RUN_TEST(test_xlights_upload);
    RUN_TEST(test_rejects_what_cannot_work);
    return UNITY_END();
}
