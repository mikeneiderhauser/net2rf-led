// Host tests for src/config.cpp (built against the stubs in test/stubs): per-zone protocols, the migration of
// settings saved by single-protocol firmware, and the /api/config JSON.
// Run: pio test -e native
#include <unity.h>

#include "../../src/config.cpp"

namespace updater {
bool valid_repo(const char *) { return true; }
}  // namespace updater

using namespace rfproto;

// Saves `c` as the firmware would, as it was laid out before zones had their own protocols: the zone's protocols and
// p1_addr bytes were spare (zero), and `addr` held the address in the controller's one protocol.
static void save_single_protocol(AppConfig c) {
    for (ZoneConfig &z : c.zones) {
        z.protocols = 0;
        z.p1_addr[0] = z.p1_addr[1] = 0;
    }
    Preferences::store().clear();
    config_save_app(c);
}

void test_defaults(void) {
    AppConfig c;
    config_defaults_app(c);
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], c.zones[i].protocols);  // protocol 0, as before
        TEST_ASSERT_EQUAL_HEX8(i, c.zones[i].p1_addr[0]);                // and a protocol 1 address ready
    }
    TEST_ASSERT_EQUAL_HEX8(0x04, c.zones[2].addr[1]);  // group 2 in the protocol 0 mask
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], zones_common_protocols(c));
}

void test_migrate_protocol0_controller(void) {
    AppConfig old;
    config_defaults_app(old);
    old.protocol = 0;
    const uint8_t custom[4] = {0x00, 0x0C, 0x00, 0x0F};  // groups 2 and 3
    memcpy(old.zones[1].addr, custom, 4);
    save_single_protocol(old);
    config_load();
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], g_app.zones[1].protocols);   // still protocol 0 only: no new airtime
    TEST_ASSERT_EQUAL_HEX8_ARRAY(custom, g_app.zones[1].addr, 4);         // its address kept
    TEST_ASSERT_EQUAL_HEX8(1, g_app.zones[1].p1_addr[0]);                 // protocol 1 address: the default
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_app.zones[1].p1_addr[1]);
}

void test_migrate_protocol1_controller(void) {
    AppConfig old;
    config_defaults_app(old);
    old.protocol = 1;
    old.mode = MODE_VENDOR;
    for (uint8_t i = 0; i < MAX_ZONES; i++) {  // protocol 1 addresses lived in addr[0..1]
        uint8_t a[4] = {(uint8_t) (i == 3 ? 42 : i), 0xFF, 0, 0};
        memcpy(old.zones[i].addr, a, 4);
    }
    save_single_protocol(old);
    config_load();
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], g_app.zones[3].protocols);
    TEST_ASSERT_EQUAL_HEX8(42, g_app.zones[3].p1_addr[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, g_app.zones[3].p1_addr[1]);
    const uint8_t p0_group3[4] = {0x00, 0x08, 0x00, 0x0F};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p0_group3, g_app.zones[3].addr, 4);  // protocol 0 address: the default
    TEST_ASSERT_EQUAL_UINT8(MODE_VENDOR, g_app.mode);
    TEST_ASSERT_FALSE(protocol_in_use(g_app, 0));
}

void test_migrated_settings_stay_migrated(void) {
    AppConfig c;
    config_defaults_app(c);
    c.zones[1].protocols = ALL_PROTOCOLS;
    Preferences::store().clear();
    config_save_app(c);
    config_load();
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, g_app.zones[1].protocols);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], g_app.zones[0].protocols);
}

static bool apply(AppConfig &c, const char *json, String &err) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return app_from_json(doc.as<JsonObjectConst>(), c, err);
}

void test_json_controller_wide(void) {
    AppConfig c;
    config_defaults_app(c);
    String err;
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocols":[0,1]}})", err));
    for (const ZoneConfig &z : c.zones)
        TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, z.protocols);
    JsonDocument out;
    app_to_json(c, out.to<JsonObject>());
    TEST_ASSERT_EQUAL(2, out["devices"]["protocols"].size());
    TEST_ASSERT_FALSE(out["devices"]["per_zone"].as<bool>());
    TEST_ASSERT_EQUAL_STRING("0004000F", out["zones"][2]["addr_p0"]);
    TEST_ASSERT_EQUAL_STRING("02FF", out["zones"][2]["addr_p1"]);

    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocols":[1]}})", err));
    TEST_ASSERT_EQUAL_UINT8(1, c.protocol);  // the default follows
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], zones_common_protocols(c));
}

void test_json_per_zone(void) {
    AppConfig c;
    config_defaults_app(c);
    c.num_zones = 3;
    String err;
    TEST_ASSERT_TRUE(apply(c, R"({"zones":[
        {"name":"All","protocols":[0]},
        {"name":"Pucks","protocols":[1],"addr_p1":"05FF"},
        {"name":"Both","protocols":[0,1],"addr_p0":"0008000F"}]})", err));
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], c.zones[0].protocols);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], c.zones[1].protocols);
    TEST_ASSERT_EQUAL_HEX8(5, c.zones[1].p1_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, c.zones[2].protocols);
    TEST_ASSERT_EQUAL_HEX8(0x08, c.zones[2].addr[1]);
    JsonDocument out;
    app_to_json(c, out.to<JsonObject>());
    TEST_ASSERT_TRUE(out["devices"]["per_zone"].as<bool>());
    TEST_ASSERT_EQUAL(2, out["devices"]["protocols"].size());  // between them: 0 and 1
    TEST_ASSERT_EQUAL(1, out["zones"][1]["protocols"][0].as<int>());

    // A zone added later starts on what the zones share; here they differ, so on the default protocol.
    TEST_ASSERT_TRUE(apply(c, R"({"zones":[{},{},{},{"name":"New"}]})", err));
    TEST_ASSERT_EQUAL_UINT8(4, c.num_zones);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], c.zones[3].protocols);
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, c.zones[2].protocols);  // untouched without "protocols"
}

void test_json_older_clients(void) {
    AppConfig c;
    config_defaults_app(c);
    String err;
    // An older client switches the controller's protocol: every zone follows, both addresses stay.
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocol":1}})", err));
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], zones_common_protocols(c));
    TEST_ASSERT_EQUAL_HEX8(0x04, c.zones[2].addr[1]);
    // ... and imports zones with one "addr" in that protocol.
    TEST_ASSERT_TRUE(apply(c, R"({"zones":[{"addr":"00FF"},{"addr":"07FF"}]})", err));
    TEST_ASSERT_EQUAL_HEX8(7, c.zones[1].p1_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], c.zones[1].protocols);
}

void test_export_import_roundtrip(void) {
    // A per-zone setup exported (as /api/config reports it) and imported again, here on a controller whose default
    // protocol differs, keeps every zone's own protocols.
    AppConfig c;
    config_defaults_app(c);
    c.num_zones = 3;
    c.zones[1].protocols = PROTOCOL_BIT[1];
    c.zones[2].protocols = ALL_PROTOCOLS;
    c.zones[1].p1_addr[0] = 9;
    JsonDocument exported;
    app_to_json(c, exported.to<JsonObject>());
    exported["devices"]["protocol"] = 1;
    AppConfig other;
    config_defaults_app(other);
    String err;
    TEST_ASSERT_TRUE(app_from_json(exported.as<JsonObjectConst>(), other, err));
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], other.zones[0].protocols);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], other.zones[1].protocols);
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, other.zones[2].protocols);
    TEST_ASSERT_EQUAL_HEX8(9, other.zones[1].p1_addr[0]);

    // A uniform setup round-trips too.
    config_defaults_app(c);
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocols":[0,1]}})", err));
    JsonDocument e2;
    app_to_json(c, e2.to<JsonObject>());
    config_defaults_app(other);
    TEST_ASSERT_TRUE(app_from_json(e2.as<JsonObjectConst>(), other, err));
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, zones_common_protocols(other));
}

void test_json_errors(void) {
    AppConfig c;
    config_defaults_app(c);
    String err;
    TEST_ASSERT_FALSE(apply(c, R"({"devices":{"protocols":[]}})", err));
    TEST_ASSERT_FALSE(apply(c, R"({"devices":{"protocols":[2]}})", err));
    TEST_ASSERT_FALSE(apply(c, R"({"zones":[{"protocols":[0],"addr_p1":"123"}]})", err));
    // Vendor mode drives protocol 1 only: every enabled zone needs it.
    TEST_ASSERT_FALSE(apply(c, R"({"devices":{"mode":"vendor"}})", err));
    TEST_ASSERT_TRUE(err.find("vendor") != std::string::npos);
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"mode":"vendor","protocols":[0,1]}})", err));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_migrate_protocol0_controller);
    RUN_TEST(test_migrate_protocol1_controller);
    RUN_TEST(test_migrated_settings_stay_migrated);
    RUN_TEST(test_json_controller_wide);
    RUN_TEST(test_json_per_zone);
    RUN_TEST(test_json_older_clients);
    RUN_TEST(test_export_import_roundtrip);
    RUN_TEST(test_json_errors);
    return UNITY_END();
}
