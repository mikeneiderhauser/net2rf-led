// Host tests for src/config.cpp (built against the stubs in test/stubs): per-zone protocols, the migration of
// settings saved by single-protocol firmware, and the /api/config JSON.
// Run: pio test -e native
#include <unity.h>

#include "../../src/config.cpp"

namespace updater {
bool valid_repo(const char *) { return true; }
}  // namespace updater

using namespace rfproto;

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

void test_settings_survive_a_save_and_load(void) {
    AppConfig c;
    config_defaults_app(c);
    c.zones[1].protocols = ALL_PROTOCOLS;
    strlcpy(c.name, "Front Yard", sizeof(c.name));
    Preferences::store().clear();
    config_save_app(c);
    config_load();
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, g_app.zones[1].protocols);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], g_app.zones[0].protocols);
    TEST_ASSERT_EQUAL_STRING("Front Yard", g_app.name);
}

// A record written with another layout (older firmware) is not converted: that part starts from defaults.
void test_other_layouts_are_not_loaded(void) {
    AppConfig c;
    config_defaults_app(c);
    strlcpy(c.name, "Old Layout", sizeof(c.name));
    Preferences::store().clear();
    config_save_app(c);
    Preferences::store()["rfb/app"].pop_back();  // not the size this firmware saves
    config_load();
    TEST_ASSERT_EQUAL_STRING("Net2RF LED", g_app.name);

    Preferences::store().clear();
    c.magic ^= 1;  // the right size, but marked as another layout
    config_save_app(c);
    config_load();
    TEST_ASSERT_EQUAL_STRING("Net2RF LED", g_app.name);
}

// The two parts are stored separately: clearing one leaves the other exactly as it was.
void test_reset_one_part_keeps_the_other(void) {
    Preferences::store().clear();
    config_defaults_app(g_app);
    config_defaults_net(g_net);
    strlcpy(g_app.name, "Front Yard", sizeof(g_app.name));
    g_app.num_zones = 9;
    strlcpy(g_net.wifi_ssid, "ShowNet", sizeof(g_net.wifi_ssid));
    g_net.dhcp = 0;
    config_save_app(g_app);
    config_save_net(g_net);

    config_settings_reset();
    config_load();
    TEST_ASSERT_EQUAL_STRING("Net2RF LED", g_app.name);    // settings back to defaults
    TEST_ASSERT_EQUAL_UINT8(5, g_app.num_zones);
    TEST_ASSERT_EQUAL_STRING("ShowNet", g_net.wifi_ssid);  // network untouched
    TEST_ASSERT_EQUAL_UINT8(0, g_net.dhcp);

    strlcpy(g_app.name, "Back Yard", sizeof(g_app.name));
    config_save_app(g_app);
    config_network_reset();
    config_load();
    TEST_ASSERT_EQUAL_STRING("", g_net.wifi_ssid);         // network back to defaults
    TEST_ASSERT_EQUAL_UINT8(1, g_net.dhcp);
    TEST_ASSERT_EQUAL_STRING("Back Yard", g_app.name);     // settings untouched

    config_factory_reset();
    config_load();
    TEST_ASSERT_EQUAL_STRING("Net2RF LED", g_app.name);
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
    RUN_TEST(test_settings_survive_a_save_and_load);
    RUN_TEST(test_other_layouts_are_not_loaded);
    RUN_TEST(test_reset_one_part_keeps_the_other);
    RUN_TEST(test_json_controller_wide);
    RUN_TEST(test_json_per_zone);
    RUN_TEST(test_json_older_clients);
    RUN_TEST(test_export_import_roundtrip);
    RUN_TEST(test_json_errors);
    return UNITY_END();
}
