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
        TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], c.zones[i].protocols);  // protocol 1 on a new controller
        TEST_ASSERT_EQUAL_HEX8(i, c.zones[i].p1_addr[0]);                // and a protocol 1 address ready
    }
    TEST_ASSERT_EQUAL_HEX8(0x04, c.zones[2].addr[1]);  // group 2 in the protocol 0 mask
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], zones_common_protocols(c));
    TEST_ASSERT_EQUAL_UINT8(1, c.protocol);
    TEST_ASSERT_EQUAL_UINT8(1, c.lbt_enabled);
}

void test_saved_settings_keep_their_protocol(void) {
    Preferences::store().clear();
    AppConfig c;
    config_defaults_app(c);
    c.protocol = 0;
    c.lbt_enabled = 0;
    for (uint8_t i = 0; i < MAX_ZONES; i++)
        c.zones[i].protocols = PROTOCOL_BIT[0];
    config_save_app(c);
    config_load();
    TEST_ASSERT_EQUAL_UINT8(0, g_app.protocol);
    TEST_ASSERT_EQUAL_UINT8(0, g_app.lbt_enabled);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], zones_common_protocols(g_app));
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
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], g_app.zones[0].protocols);
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
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], c.zones[3].protocols);
    TEST_ASSERT_EQUAL_UINT8(ALL_PROTOCOLS, c.zones[2].protocols);  // untouched without "protocols"
}

void test_retired_api_fields_do_nothing(void) {
    AppConfig c;
    config_defaults_app(c);
    String err;
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocols":[0]}})", err));
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocol":1}})", err));  // read-only
    TEST_ASSERT_EQUAL_UINT8(0, c.protocol);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], zones_common_protocols(c));
    uint8_t group = c.zones[1].p1_addr[0];
    TEST_ASSERT_TRUE(apply(c, R"({"zones":[{"addr":"00FF"},{"addr":"07FF"}]})", err));
    TEST_ASSERT_EQUAL_HEX8(group, c.zones[1].p1_addr[0]);
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[0], c.zones[1].protocols);
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
    AppConfig other;
    config_defaults_app(other);
    other.protocol = 0;
    String err;
    TEST_ASSERT_TRUE(app_from_json(exported.as<JsonObjectConst>(), other, err));
    TEST_ASSERT_EQUAL_UINT8(PROTOCOL_BIT[1], other.zones[0].protocols);
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
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"protocols":[0]}})", err));
    TEST_ASSERT_FALSE(apply(c, R"({"devices":{"mode":"vendor"}})", err));
    TEST_ASSERT_TRUE(err.find("vendor") != std::string::npos);
    TEST_ASSERT_TRUE(apply(c, R"({"devices":{"mode":"vendor","protocols":[0,1]}})", err));
}

// The update settings are just the release source: the controller no longer checks for releases itself, so the
// old check settings are gone from the JSON, and a file that still carries them imports without complaint.
void test_sync_settings(void) {
    // A record saved by the firmware before the coverage group fields still loads, zones and all.
    AppConfig c;
    config_defaults_app(c);
    c.num_zones = 7;
    strcpy(c.name, "Before");
    Preferences::store().clear();
    config_save_app(c);
    Preferences::store()["rfb/app"].resize(APP_PREV_SIZE);
    TEST_ASSERT_TRUE(APP_PREV_SIZE < sizeof(AppConfig));
    config_load();
    TEST_ASSERT_EQUAL_STRING("Before", g_app.name);
    TEST_ASSERT_EQUAL_UINT8(7, g_app.num_zones);
    TEST_ASSERT_EQUAL_UINT8(0, g_app.sync_enabled);
    TEST_ASSERT_EQUAL_UINT8(1, g_app.sync_copies);

    String err;
    config_defaults_app(c);
    TEST_ASSERT_TRUE(apply(c, R"({"sync":{"enabled":true,"master":true,"copies":2}})", err));
    TEST_ASSERT_TRUE(sync_is_master(c));
    TEST_ASSERT_FALSE(sync_is_remote(c));
    TEST_ASSERT_EQUAL_UINT8(2, c.sync_copies);
    JsonDocument out;
    app_to_json(c, out.to<JsonObject>());
    TEST_ASSERT_TRUE(out["sync"]["master"].as<bool>());
    TEST_ASSERT_EQUAL(2, out["sync"]["copies"].as<int>());
    TEST_ASSERT_TRUE(apply(c, R"({"sync":{"master":false}})", err));
    TEST_ASSERT_TRUE(sync_is_remote(c));
    TEST_ASSERT_TRUE(apply(c, R"({"role":"receiver"})", err));  // a receiver never transmits: no group role
    TEST_ASSERT_FALSE(sync_is_remote(c));
    TEST_ASSERT_FALSE(apply(c, R"({"sync":{"copies":4}})", err));
}

void test_update_settings(void) {
    AppConfig c;
    config_defaults_app(c);
    JsonDocument out;
    app_to_json(c, out.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("mikeneiderhauser/net2rf-led", out["update"]["repo"].as<const char *>());
    TEST_ASSERT_TRUE(out["update"]["auto_check"].isNull());
    TEST_ASSERT_TRUE(out["update"]["check_hours"].isNull());

    JsonDocument in;
    in["update"]["repo"] = "someone/fork";
    String err;
    TEST_ASSERT_TRUE(app_from_json(in.as<JsonObjectConst>(), c, err));
    TEST_ASSERT_EQUAL_STRING("someone/fork", c.update_repo);
}

void setUp(void) {}
void tearDown(void) {}

void test_amp_record(void) {
    Preferences::store().clear();
    AmpConfig a;
    config_load_amp(a);  // nothing saved: not fitted, active low, on
    TEST_ASSERT_EQUAL_UINT8(0, a.fitted);
    TEST_ASSERT_EQUAL_UINT8(1, a.active_low);
    TEST_ASSERT_EQUAL_UINT8(1, a.on);
    TEST_ASSERT_EQUAL_UINT16(0, a.idle_min);
    config_save_amp(AmpConfig{1, 0, 0, 30});
    config_load_amp(a);
    TEST_ASSERT_EQUAL_UINT8(1, a.fitted);
    TEST_ASSERT_EQUAL_UINT8(0, a.active_low);
    TEST_ASSERT_EQUAL_UINT8(0, a.on);
    TEST_ASSERT_EQUAL_UINT16(30, a.idle_min);
}

void test_amp_record_from_older_firmware(void) {
    Preferences::store().clear();
    struct {
        uint32_t magic;
        uint8_t fitted, active_low, on;
    } old = {0x4E324131, 1, 0, 1};
    Preferences p;
    p.begin(NVS_NS, false);
    p.putBytes("amp", &old, sizeof(old));
    p.end();
    AmpConfig a;
    config_load_amp(a);
    TEST_ASSERT_EQUAL_UINT8(1, a.fitted);
    TEST_ASSERT_EQUAL_UINT8(0, a.active_low);
    TEST_ASSERT_EQUAL_UINT8(1, a.on);
    TEST_ASSERT_EQUAL_UINT16(0, a.idle_min);
}

static bool preset(FadePresets &p, const char *json, String &err) {
    JsonDocument doc;
    deserializeJson(doc, json);
    return fade_presets_apply(p, doc.as<JsonObjectConst>(), err);
}

void test_fade_presets(void) {
    Preferences::store().clear();
    FadePresets p;
    config_load_fade_presets(p);
    TEST_ASSERT_EQUAL_UINT8(0, p.n);
    String err;
    TEST_ASSERT_TRUE(preset(p, R"({"name":"Xmas","palette":["FF0000","00FF00"],"fade_s":2.5,"hold_s":1})", err));
    TEST_ASSERT_EQUAL_UINT8(2, p.p[0].fade.n);
    TEST_ASSERT_EQUAL_UINT32(2500, p.p[0].fade.fade_ms);
    TEST_ASSERT_TRUE(preset(p, R"({"name":"Xmas","palette":["0000FF"]})", err));  // same name: replaced
    TEST_ASSERT_EQUAL_UINT8(1, p.n);
    TEST_ASSERT_EQUAL_UINT8(1, p.p[0].fade.n);
    TEST_ASSERT_FALSE(preset(p, R"({"name":"","palette":["0000FF"]})", err));
    TEST_ASSERT_FALSE(preset(p, R"({"name":"123456789012345678901","palette":["0000FF"]})", err));
    TEST_ASSERT_FALSE(preset(p, R"({"name":"No colours"})", err));
    TEST_ASSERT_FALSE(preset(p, R"({"name":"Bad","palette":["nope"]})", err));
    TEST_ASSERT_FALSE(preset(p, R"({"name":"Long","palette":["0000FF"],"fade_s":601})", err));
    for (int i = 1; i < MAX_FADE_PRESETS; i++) {
        String one = String("{\"name\":\"P") + i + "\",\"palette\":[\"00FF00\"]}";
        TEST_ASSERT_TRUE(preset(p, one.c_str(), err));
    }
    TEST_ASSERT_FALSE(preset(p, R"({"name":"One too many","palette":["0000FF"]})", err));
    TEST_ASSERT_TRUE(preset(p, R"({"delete":"Xmas"})", err));
    TEST_ASSERT_EQUAL_UINT8(MAX_FADE_PRESETS - 1, p.n);
    TEST_ASSERT_EQUAL_STRING("P1", p.p[0].name);
    TEST_ASSERT_FALSE(preset(p, R"({"delete":"Xmas"})", err));
    config_save_fade_presets(p);
    FadePresets back;
    config_load_fade_presets(back);
    TEST_ASSERT_EQUAL_UINT8(p.n, back.n);
    TEST_ASSERT_EQUAL_STRING("P1", back.p[0].name);
}

void test_extras_roundtrip(void) {
    Preferences::store().clear();
    BootTest t{};
    t.mode = 3;  // fade
    t.rgb[0] = 0xFF;
    t.fade.n = 2, t.fade.rgb[1][2] = 0x80, t.fade.fade_ms = 2500, t.fade.hold_ms = 4000;
    config_save_boot_test(&t);
    config_save_amp(AmpConfig{1, 0, 1, 15});
    FadePresets p{};
    String err;
    TEST_ASSERT_TRUE(preset(p, R"({"name":"Mine","palette":["112233"]})", err));
    config_save_fade_presets(p);

    JsonDocument doc;
    extras_to_json(doc.to<JsonObject>());
    Preferences::store().clear();
    SettingsExtras x;
    TEST_ASSERT_TRUE(extras_from_json(doc.as<JsonObjectConst>(), x, err));
    extras_save(x);

    BootTest b{};
    TEST_ASSERT_TRUE(config_load_boot_test(b));
    TEST_ASSERT_EQUAL_UINT8(3, b.mode);
    TEST_ASSERT_EQUAL_UINT8(2, b.fade.n);
    TEST_ASSERT_EQUAL_HEX8(0x80, b.fade.rgb[1][2]);
    TEST_ASSERT_EQUAL_UINT32(2500, b.fade.fade_ms);
    AmpConfig a;
    config_load_amp(a);
    TEST_ASSERT_EQUAL_UINT8(1, a.fitted);
    TEST_ASSERT_EQUAL_UINT8(0, a.active_low);
    TEST_ASSERT_EQUAL_UINT16(15, a.idle_min);
    FadePresets back;
    config_load_fade_presets(back);
    TEST_ASSERT_EQUAL_STRING("Mine", back.p[0].name);

    JsonDocument bad;  // nothing is saved from a bad file
    deserializeJson(bad, R"({"boot_test":{"mode":"nope","rgb":"FFFFFF","fade":{}}})");
    TEST_ASSERT_FALSE(extras_from_json(bad.as<JsonObjectConst>(), x, err));
    JsonDocument none;  // a file from older firmware: nothing to restore
    deserializeJson(none, R"({"app":{}})");
    TEST_ASSERT_TRUE(extras_from_json(none.as<JsonObjectConst>(), x, err));
    TEST_ASSERT_FALSE(x.has_boot || x.has_amp || x.has_presets);
}

void test_settings_reset_clears_the_small_records(void) {
    Preferences::store().clear();
    BootTest t{};
    t.mode = 4, t.fade.n = 1;
    config_save_boot_test(&t);
    config_save_amp(AmpConfig{1, 1, 1, 5});
    config_settings_reset();
    BootTest b;
    TEST_ASSERT_FALSE(config_load_boot_test(b));
    AmpConfig a;
    config_load_amp(a);
    TEST_ASSERT_EQUAL_UINT8(0, a.fitted);
}

void test_boot_test_record(void) {
    Preferences::store().clear();
    BootTest t{};
    TEST_ASSERT_FALSE(config_load_boot_test(t));
    t.mode = 4;  // flicker
    t.rgb[0] = 0xFF, t.rgb[1] = 0x7A, t.rgb[2] = 0x10;
    t.fade.n = 1;
    config_save_boot_test(&t);
    BootTest back{};
    TEST_ASSERT_TRUE(config_load_boot_test(back));
    TEST_ASSERT_EQUAL_UINT8(4, back.mode);
    TEST_ASSERT_EQUAL_UINT8(0x7A, back.rgb[1]);
    config_save_boot_test(nullptr);
    TEST_ASSERT_FALSE(config_load_boot_test(back));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_saved_settings_keep_their_protocol);
    RUN_TEST(test_settings_survive_a_save_and_load);
    RUN_TEST(test_other_layouts_are_not_loaded);
    RUN_TEST(test_reset_one_part_keeps_the_other);
    RUN_TEST(test_json_controller_wide);
    RUN_TEST(test_json_per_zone);
    RUN_TEST(test_retired_api_fields_do_nothing);
    RUN_TEST(test_export_import_roundtrip);
    RUN_TEST(test_json_errors);
    RUN_TEST(test_sync_settings);
    RUN_TEST(test_update_settings);
    RUN_TEST(test_boot_test_record);
    RUN_TEST(test_amp_record);
    RUN_TEST(test_amp_record_from_older_firmware);
    RUN_TEST(test_fade_presets);
    RUN_TEST(test_extras_roundtrip);
    RUN_TEST(test_settings_reset_clears_the_small_records);
    return UNITY_END();
}
