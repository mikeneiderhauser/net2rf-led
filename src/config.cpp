#include "config.h"
#include "pins.h"
#include "updater.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

AppConfig g_app;
NetConfig g_net;

static const uint32_t APP_MAGIC = 0x4E325232;  // "N2R2": bump when the AppConfig layout changes (older records are dropped)
static const uint32_t NET_MAGIC = 0x52464E02;  // v2: admin password fields
static const char *const NVS_NS = "rfb";

static const char *const RADIO_NAMES[NUM_RADIO_TYPES] = {"cc1101", "sx1278"};

const char *radio_type_name(uint8_t type) { return type < NUM_RADIO_TYPES ? RADIO_NAMES[type] : "unknown"; }

String device_suffix() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_ETH);
    char buf[5];
    snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
    return String(buf);
}

void device_mac(uint8_t *mac) { esp_read_mac(mac, ESP_MAC_ETH); }

String default_hostname() {
    String s = "net2rf-" + device_suffix();
    s.toLowerCase();
    return s;
}

uint8_t address_len(uint8_t protocol) { return protocol == 0 ? 4 : 2; }

void default_address(uint8_t protocol, uint8_t *addr) {
    static const uint8_t P0[4] = {0x00, 0xFF, 0xFF, 0x0F};
    static const uint8_t P1[4] = {0x00, 0xFF, 0x00, 0x00};
    memcpy(addr, protocol == 0 ? P0 : P1, 4);
}

// ---------------------------------------------------------------------------------------------
// Defaults / persistence
// ---------------------------------------------------------------------------------------------

// Zone 0 reaches every device; zone N (1-15) reaches group N only.
static void zone_default_address(uint8_t protocol, uint8_t index, uint8_t *addr) {
    default_address(protocol, addr);
    if (index == 0 || index >= MAX_ZONES)
        return;
    if (protocol == 0) {  // one bit per group: byte 1 = groups 0-7, byte 2 = groups 8-15
        addr[1] = (1u << index) & 0xFF;
        addr[2] = (1u << index) >> 8;
    } else {
        addr[0] = index;  // group code
    }
}

// A zone always keeps an address in both protocols, so it can switch (or use both) without losing either.
static void zone_default_addresses(ZoneConfig &z, uint8_t index) {
    zone_default_address(0, index, z.addr);
    uint8_t p1[4];
    zone_default_address(1, index, p1);
    memcpy(z.p1_addr, p1, 2);
}

static void zone_defaults(ZoneConfig &z, uint8_t index, uint8_t protocols) {
    memset(&z, 0, sizeof(z));
    z.enabled = 1;
    zone_default_addresses(z, index);
    z.protocols = protocols & rfproto::ALL_PROTOCOLS ? protocols & rfproto::ALL_PROTOCOLS : rfproto::PROTOCOL_BIT[0];
    if (index == 0)
        strlcpy(z.name, "All Zones", sizeof(z.name));
    else
        snprintf(z.name, sizeof(z.name), "Zone %u", index);
}

void config_reset_zone(AppConfig &c, uint8_t index) {
    if (index < MAX_ZONES)
        zone_defaults(c.zones[index], index, c.zones[index].protocols ? c.zones[index].protocols
                                                                      : rfproto::PROTOCOL_BIT[c.protocol & 1]);
}

uint8_t zones_common_protocols(const AppConfig &c) {
    uint8_t common = 0;
    for (uint8_t i = 0; i < c.num_zones && i < MAX_ZONES; i++) {
        uint8_t p = c.zones[i].protocols & rfproto::ALL_PROTOCOLS;
        if (i == 0)
            common = p;
        else if (p != common)
            return 0;
    }
    return common;
}

// The protocols a newly added zone gets: what the other zones share, else the default protocol.
static uint8_t new_zone_protocols(const AppConfig &c) {
    uint8_t common = zones_common_protocols(c);
    return common ? common : rfproto::PROTOCOL_BIT[c.protocol & 1];
}

void config_defaults_app(AppConfig &c) {
    memset(&c, 0, sizeof(c));
    c.magic = APP_MAGIC;
    strlcpy(c.name, "Net2RF LED", sizeof(c.name));
    c.protocol = 1;  // the devices on sale (Wally's Lights, LedGiftSupplier). Saved settings keep their own.
    c.mode = MODE_PIXEL;
    c.color_order = rfproto::ORDER_RGB;
    c.num_zones = 5;  // All Zones + Zone 1-4 (the smaller xLights model, tools/xlights)
    c.start_channel = 1;
    c.ddp_port = 4048;
    c.input_timeout_s = 300;
    c.radio_type = NET2RF_DEFAULT_RADIO;
    c.tx_power = 10;
    c.freq[0] = rfproto::DEFAULT_FREQ_P0;
    c.freq[1] = rfproto::DEFAULT_FREQ_P1;
    c.repeats = 3;
    c.off_threshold = 16;
    c.refresh_ms = 0;  // devices latch the last colour; re-sending only adds airtime (and car-fob interference)
    c.tx_jitter_ms = 0;
    c.output_enabled = 1;
    c.ddp_enabled = 1;
    c.e131_enabled = 0;  // off by default: a shared universe could drive the devices unintentionally
    c.e131_multicast = 1;
    c.e131_universe = 1;
    for (uint8_t i = 0; i < MAX_ZONES; i++)
        zone_defaults(c.zones[i], i, rfproto::PROTOCOL_BIT[c.protocol & 1]);
    c.lbt_enabled = 1;  // new controllers only: saved settings keep their own
    c.lbt_threshold = LBT_DEFAULT_THRESHOLD;
    c.unused_radio_off = 0;
    strlcpy(c.update_repo, updater::DEFAULT_REPO, sizeof(c.update_repo));
    c.base_layer = 1;  // new controllers only: saved settings keep the old behaviour until switched on
    c.display_sleep = 0;  // the default (10 minutes)
    c.receiver = 0;
    c.rx_profile = RX_PROFILE_NORMAL;
    c.sync_enabled = 0;
    c.sync_master = 0;
    c.sync_copies = 1;
}

// The record as saved by firmware before the coverage group fields were appended (same magic): accepted, and the
// new fields keep their defaults.
static const size_t APP_PREV_SIZE = (offsetof(AppConfig, sync_enabled) + alignof(AppConfig) - 1) & ~(alignof(AppConfig) - 1);

void config_defaults_net(NetConfig &c) {
    memset(&c, 0, sizeof(c));
    c.magic = NET_MAGIC;
    strlcpy(c.hostname, default_hostname().c_str(), sizeof(c.hostname));
    c.eth_enabled = 1;
    c.dhcp = 1;
    c.subnet = (uint32_t) IPAddress(255, 255, 255, 0);
    c.ap_mode = AP_NO_CONNECTION;
    strlcpy(c.ap_pass, "net2rf1234", sizeof(c.ap_pass));
}

// Settings are kept as two separate records, so either can be cleared, exported or imported without the other:
//   "net": NetConfig - how to reach the controller (hostname, Ethernet / Wi-Fi, IP, setup hotspot, admin password)
//   "app": AppConfig - everything else (zones, devices, radio, input, display, updates)
// A record with another layout (older firmware) is not converted: that part starts from its defaults. When a
// layout changes, bump its magic.
void config_load() {
    Preferences p;
    p.begin(NVS_NS, true);
    size_t app_len = p.getBytesLength("app");
    bool app_ok = false;
    if (app_len == sizeof(AppConfig) || (app_len == APP_PREV_SIZE && APP_PREV_SIZE < sizeof(AppConfig))) {
        config_defaults_app(g_app);  // fields a shorter record lacks keep their defaults
        app_ok = p.getBytes("app", &g_app, app_len) == app_len && g_app.magic == APP_MAGIC;
    }
    bool net_ok = p.getBytesLength("net") == sizeof(NetConfig) && p.getBytes("net", &g_net, sizeof(g_net)) &&
                  g_net.magic == NET_MAGIC;
    p.end();
    if (!app_ok) {
        log_i("No saved settings (or from another firmware layout), using defaults");
        config_defaults_app(g_app);
    }
    if (!net_ok) {
        log_i("No saved network settings (or from another firmware layout), using defaults");
        config_defaults_net(g_net);
    }
    if (g_app.num_zones == 0 || g_app.num_zones > MAX_ZONES)
        g_app.num_zones = 1;
    if (g_app.sync_copies < 1 || g_app.sync_copies > 3)
        g_app.sync_copies = 1;
    if (g_app.radio_type >= NUM_RADIO_TYPES)
        g_app.radio_type = NET2RF_DEFAULT_RADIO;
    g_app.update_repo[sizeof(g_app.update_repo) - 1] = 0;
    if (!updater::valid_repo(g_app.update_repo))
        strlcpy(g_app.update_repo, updater::DEFAULT_REPO, sizeof(g_app.update_repo));
}

void config_save_app(const AppConfig &c) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putBytes("app", &c, sizeof(c));
    p.end();
}

void config_save_net(const NetConfig &c) {
    Preferences p;
    p.begin(NVS_NS, false);
    p.putBytes("net", &c, sizeof(c));
    p.end();
}

static const uint32_t BOOT_TEST_MAGIC = 0x4E325431;  // "N2T1"
struct BootTestRecord {
    uint32_t magic;
    BootTest t;
};

bool config_load_boot_test(BootTest &out) {
    Preferences p;
    p.begin(NVS_NS, true);
    BootTestRecord r{};
    bool ok = p.getBytesLength("boot_test") == sizeof(r) && p.getBytes("boot_test", &r, sizeof(r)) == sizeof(r) &&
              r.magic == BOOT_TEST_MAGIC && r.t.mode != 0 && r.t.fade.n >= 1 && r.t.fade.n <= MAX_FADE_COLORS;
    p.end();
    if (ok)
        out = r.t;
    return ok;
}

void config_save_boot_test(const BootTest *t) {
    Preferences p;
    p.begin(NVS_NS, false);
    if (t) {
        BootTestRecord r{BOOT_TEST_MAGIC, *t};
        p.putBytes("boot_test", &r, sizeof(r));
    } else {
        p.remove("boot_test");
    }
    p.end();
}

static const uint32_t AMP_MAGIC = 0x4E324132;     // "N2A2"
static const uint32_t AMP_MAGIC_OLD = 0x4E324131;  // "N2A1": the two records below, accepted with follow_output on
struct AmpRecord {
    uint32_t magic;
    AmpConfig c;
};
struct AmpRecordV2 {  // before follow_output
    uint32_t magic;
    uint8_t fitted, active_low, on;
    uint16_t idle_min;
};
struct AmpRecordV1 {  // before idle_min
    uint32_t magic;
    uint8_t fitted, active_low, on;
};

void config_load_amp(AmpConfig &out) {
    out = AmpConfig{0, 1, 1, 0, 1};
    Preferences p;
    p.begin(NVS_NS, true);
    AmpRecord r{};
    AmpRecordV2 v2{};
    AmpRecordV1 v1{};
    size_t len = p.getBytesLength("amp");
    if (len == sizeof(r) && p.getBytes("amp", &r, sizeof(r)) == sizeof(r) && r.magic == AMP_MAGIC)
        out = r.c;
    else if (len == sizeof(v2) && p.getBytes("amp", &v2, sizeof(v2)) == sizeof(v2) && v2.magic == AMP_MAGIC_OLD)
        out = AmpConfig{v2.fitted, v2.active_low, v2.on, v2.idle_min, 1};
    else if (len == sizeof(v1) && p.getBytes("amp", &v1, sizeof(v1)) == sizeof(v1) && v1.magic == AMP_MAGIC_OLD)
        out = AmpConfig{v1.fitted, v1.active_low, v1.on, 0, 1};
    p.end();
}

void config_save_amp(const AmpConfig &c) {
    Preferences p;
    p.begin(NVS_NS, false);
    AmpRecord r{AMP_MAGIC, c};
    p.putBytes("amp", &r, sizeof(r));
    p.end();
}

static const uint32_t PRESETS_MAGIC = 0x4E325031;  // "N2P1"
struct PresetsRecord {
    uint32_t magic;
    FadePresets p;
};

void config_load_fade_presets(FadePresets &out) {
    memset(&out, 0, sizeof(out));
    Preferences p;
    p.begin(NVS_NS, true);
    PresetsRecord r{};
    if (p.getBytesLength("fade_pre") == sizeof(r) && p.getBytes("fade_pre", &r, sizeof(r)) == sizeof(r) &&
        r.magic == PRESETS_MAGIC && r.p.n <= MAX_FADE_PRESETS)
        out = r.p;
    p.end();
}

void config_save_fade_presets(const FadePresets &c) {
    Preferences p;
    p.begin(NVS_NS, false);
    PresetsRecord r{PRESETS_MAGIC, c};
    p.putBytes("fade_pre", &r, sizeof(r));
    p.end();
}

void config_factory_reset() {
    Preferences p;
    p.begin(NVS_NS, false);
    p.clear();
    p.end();
}

void config_network_reset() {
    config_defaults_net(g_net);
    config_save_net(g_net);
}

void config_settings_reset() {
    config_defaults_app(g_app);
    config_save_app(g_app);
    Preferences p;
    p.begin(NVS_NS, false);
    p.remove("boot_test");
    p.remove("amp");
    p.remove("fade_pre");
    p.end();
}

// ---------------------------------------------------------------------------------------------
// Admin password
// ---------------------------------------------------------------------------------------------

static void auth_hash(const uint8_t *salt, const char *password, uint8_t *out) {
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, salt, 16);
    mbedtls_sha256_update(&ctx, (const uint8_t *) password, strlen(password));
    mbedtls_sha256_finish(&ctx, out);
    mbedtls_sha256_free(&ctx);
}

void auth_set_password(NetConfig &c, const char *password) {
    if (password == nullptr || password[0] == '\0') {
        c.auth_enabled = 0;
        memset(c.auth_salt, 0, sizeof(c.auth_salt));
        memset(c.auth_hash, 0, sizeof(c.auth_hash));
        return;
    }
    esp_fill_random(c.auth_salt, sizeof(c.auth_salt));
    auth_hash(c.auth_salt, password, c.auth_hash);
    c.auth_enabled = 1;
}

bool auth_check(const NetConfig &c, const char *password) {
    if (!c.auth_enabled)
        return true;
    uint8_t h[32];
    auth_hash(c.auth_salt, password, h);
    uint8_t diff = 0;  // constant-time compare
    for (size_t i = 0; i < sizeof(h); i++)
        diff |= h[i] ^ c.auth_hash[i];
    return diff == 0;
}

// ---------------------------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------------------------

static void protocols_to_json(uint8_t set, JsonArray out) {
    for (uint8_t p = 0; p < rfproto::NUM_PROTOCOLS; p++)
        if (rfproto::has_protocol(set, p))
            out.add(p);
}

// [0], [1] or [0, 1] -> protocol set. False (and `err`) for anything else, including an empty list.
static bool protocols_from_json(JsonVariantConst v, uint8_t &set, const String &where, String &err) {
    uint8_t s = 0;
    if (!v.is<JsonArrayConst>()) {
        err = where + ": protocols is a list: [0], [1] or [0, 1]";
        return false;
    }
    for (JsonVariantConst p : v.as<JsonArrayConst>()) {
        if (!p.is<int>() || p.as<int>() < 0 || p.as<int>() >= rfproto::NUM_PROTOCOLS) {
            err = where + ": the protocols are 0 and 1";
            return false;
        }
        s |= rfproto::PROTOCOL_BIT[p.as<int>()];
    }
    if (!s) {
        err = where + ": at least one protocol";
        return false;
    }
    set = s;
    return true;
}

static String hex_string(const uint8_t *data, size_t len) {
    String s;
    char b[3];
    for (size_t i = 0; i < len; i++) {
        snprintf(b, sizeof(b), "%02X", data[i]);
        s += b;
    }
    return s;
}

static const char *const TEST_MODE_NAMES[] = {"off", "solid", "cycle", "fade", "flicker"};  // TestMode

void fade_to_json(const FadePalette &p, JsonObject out) {
    JsonArray pal = out["palette"].to<JsonArray>();
    for (uint8_t i = 0; i < p.n && i < MAX_FADE_COLORS; i++)
        pal.add(hex_string(p.rgb[i], 3));
    out["fade_s"] = p.fade_ms / 1000.0f;
    out["hold_s"] = p.hold_ms / 1000.0f;
}

bool fade_from_json(JsonObjectConst in, FadePalette &p, String &err) {
    FadePalette next = p;
    JsonArrayConst pal = in["palette"].as<JsonArrayConst>();
    if (!pal.isNull()) {
        next.n = 0;
        for (JsonVariantConst c : pal)
            if (next.n < MAX_FADE_COLORS && parse_hex(c | "", next.rgb[next.n], 3))
                next.n++;
        if (next.n == 0) {
            err = "palette needs at least one RRGGBB colour";
            return false;
        }
    }
    float fade_s = in["fade_s"] | p.fade_ms / 1000.0f, hold_s = in["hold_s"] | p.hold_ms / 1000.0f;
    if (!(fade_s >= 0 && fade_s <= 600 && hold_s >= 0 && hold_s <= 600)) {
        err = "fade_s and hold_s must be 0-600";
        return false;
    }
    next.fade_ms = (uint32_t) (fade_s * 1000 + 0.5f), next.hold_ms = (uint32_t) (hold_s * 1000 + 0.5f);
    p = next;
    return true;
}

static int find_preset(const FadePresets &p, const char *name) {
    for (uint8_t i = 0; i < p.n; i++)
        if (strcmp(p.p[i].name, name) == 0)
            return i;
    return -1;
}

bool fade_presets_apply(FadePresets &p, JsonObjectConst in, String &err) {
    if (in["delete"].is<const char *>()) {
        int i = find_preset(p, in["delete"]);
        if (i < 0) {
            err = "no preset of that name";
            return false;
        }
        for (uint8_t k = i; k + 1 < p.n; k++)
            p.p[k] = p.p[k + 1];
        p.n--;
        return true;
    }
    const char *name = in["name"] | "";
    if (name[0] == '\0' || strlen(name) >= sizeof(p.p[0].name)) {
        err = "name must be 1-20 characters";
        return false;
    }
    if (in["palette"].isNull()) {
        err = "palette needs at least one RRGGBB colour";
        return false;
    }
    FadePreset n{};
    strlcpy(n.name, name, sizeof(n.name));
    n.fade.fade_ms = 3000, n.fade.hold_ms = 4000;
    if (!fade_from_json(in, n.fade, err))
        return false;
    int i = find_preset(p, name);
    if (i < 0 && p.n >= MAX_FADE_PRESETS) {
        err = String(MAX_FADE_PRESETS) + " presets saved: delete one first";
        return false;
    }
    p.p[i < 0 ? p.n++ : i] = n;
    return true;
}

void fade_presets_to_json(const FadePresets &p, JsonArray out) {
    for (uint8_t i = 0; i < p.n; i++) {
        JsonObject o = out.add<JsonObject>();
        o["name"] = String(p.p[i].name);  // copied: a char array would be kept as a pointer into `p`
        fade_to_json(p.p[i].fade, o);
    }
}

void extras_to_json(JsonObject out) {
    BootTest t;
    if (config_load_boot_test(t)) {
        JsonObject o = out["boot_test"].to<JsonObject>();
        o["mode"] = TEST_MODE_NAMES[t.mode < 5 ? t.mode : 0];
        o["rgb"] = hex_string(t.rgb, 3);
        fade_to_json(t.fade, o["fade"].to<JsonObject>());
    }
    AmpConfig a;
    config_load_amp(a);
    JsonObject amp = out["amp"].to<JsonObject>();
    amp["fitted"] = (bool) a.fitted;
    amp["active_low"] = (bool) a.active_low;
    amp["on"] = (bool) a.on;
    amp["idle_min"] = a.idle_min;
    amp["follow_output"] = (bool) a.follow_output;
    FadePresets p;
    config_load_fade_presets(p);
    if (p.n)
        fade_presets_to_json(p, out["fade_presets"].to<JsonArray>());
}

bool extras_from_json(JsonObjectConst in, SettingsExtras &x, String &err) {
    memset(&x, 0, sizeof(x));
    JsonObjectConst boot = in["boot_test"].as<JsonObjectConst>();
    if (!boot.isNull()) {
        const char *mode = boot["mode"] | "";
        for (uint8_t m = 1; m < 5; m++)
            if (strcmp(mode, TEST_MODE_NAMES[m]) == 0)
                x.boot.mode = m;
        if (!x.boot.mode || !parse_hex(boot["rgb"] | "", x.boot.rgb, 3) ||
            !fade_from_json(boot["fade"].as<JsonObjectConst>(), x.boot.fade, err) || !x.boot.fade.n) {
            err = "boot_test: " + (err.length() ? err : String("bad mode or colour"));
            return false;
        }
        x.has_boot = true;
    }
    JsonObjectConst amp = in["amp"].as<JsonObjectConst>();
    if (!amp.isNull()) {
        int idle = amp["idle_min"] | 0;
        if (idle < 0 || idle > AMP_IDLE_MAX_MIN) {
            err = "amp: idle_min out of range";
            return false;
        }
        x.amp = AmpConfig{(uint8_t) (amp["fitted"] | false), (uint8_t) (amp["active_low"] | true),
                          (uint8_t) (amp["on"] | true), (uint16_t) idle,
                          (uint8_t) (amp["follow_output"] | true)};
        x.has_amp = true;
    }
    JsonArrayConst presets = in["fade_presets"].as<JsonArrayConst>();
    if (!presets.isNull()) {
        for (JsonVariantConst v : presets)
            if (!fade_presets_apply(x.presets, v.as<JsonObjectConst>(), err)) {
                err = "fade_presets: " + err;
                return false;
            }
        x.has_presets = true;
    }
    return true;
}

void extras_save(const SettingsExtras &x) {
    if (x.has_boot)
        config_save_boot_test(&x.boot);
    if (x.has_amp)
        config_save_amp(x.amp);
    if (x.has_presets)
        config_save_fade_presets(x.presets);
}

bool parse_hex(const char *str, uint8_t *out, size_t len) {
    if (str == nullptr)
        return false;
    String clean;
    for (const char *p = str; *p; p++) {
        if (isxdigit((unsigned char) *p))
            clean += *p;
    }
    if (clean.length() != len * 2)
        return false;
    for (size_t i = 0; i < len; i++)
        out[i] = (uint8_t) strtoul(clean.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
    return true;
}

static bool parse_ip(JsonVariantConst v, uint32_t &out) {
    if (!v.is<const char *>())
        return false;
    IPAddress ip;
    if (!ip.fromString(v.as<const char *>()))
        return false;
    out = (uint32_t) ip;
    return true;
}

static void copy_name(char *dst, size_t size, const char *src) {
    size_t i = 0;
    for (; src[i] && i < size - 1; i++) {
        char ch = src[i];
        dst[i] = (ch == '"' || ch == '\\' || ch == '<' || ch == '>' || (unsigned char) ch < 0x20) ? '_' : ch;
    }
    dst[i] = '\0';
}

template<typename T> static bool read_int(JsonObjectConst in, const char *key, long lo, long hi, T &out, String &err) {
    JsonVariantConst v = in[key];
    if (v.isNull())
        return true;
    if (!v.is<long>()) {
        err = String(key) + " must be a number";
        return false;
    }
    long n = v.as<long>();
    if (n < lo || n > hi) {
        err = String(key) + " must be " + lo + ".." + hi;
        return false;
    }
    out = (T) n;
    return true;
}

void app_to_json(const AppConfig &c, JsonObject o) {
    o["name"] = c.name;
    o["output_enabled"] = (bool) c.output_enabled;
    o["role"] = c.receiver ? "receiver" : "controller";
    o["rx_profile"] = RX_PROFILE_NAMES[c.rx_profile < NUM_RX_PROFILES ? c.rx_profile : 0];
    JsonObject sync = o["sync"].to<JsonObject>();
    sync["enabled"] = (bool) c.sync_enabled;
    sync["master"] = (bool) c.sync_master;
    sync["copies"] = c.sync_copies;

    JsonObject br = o["devices"].to<JsonObject>();
    // "protocols": what every zone drives, or what the zones drive between them when they differ ("per_zone").
    uint8_t common = zones_common_protocols(c), all = 0;
    for (uint8_t i = 0; i < c.num_zones; i++)
        all |= c.zones[i].protocols;
    protocols_to_json(common ? common : all, br["protocols"].to<JsonArray>());
    br["per_zone"] = common == 0;
    br["protocol"] = c.protocol;  // the default protocol (read-only: "protocols" sets it)
    br["mode"] = c.mode == MODE_VENDOR ? "vendor" : c.mode == MODE_DMX ? "dmx" : "pixel";
    br["color_order"] = rfproto::COLOR_ORDER_NAMES[c.color_order % rfproto::NUM_ORDERS];
    br["base_layer"] = (bool) c.base_layer;

    JsonObject input = o["input"].to<JsonObject>();
    input["ddp_enabled"] = (bool) c.ddp_enabled;
    input["ddp_port"] = c.ddp_port;
    input["e131_enabled"] = (bool) c.e131_enabled;
    input["e131_universe"] = c.e131_universe;
    input["e131_multicast"] = (bool) c.e131_multicast;
    input["start_channel"] = c.start_channel;
    input["timeout_s"] = c.input_timeout_s;

    JsonObject radio = o["radio"].to<JsonObject>();
    radio["type"] = radio_type_name(c.radio_type);
    radio["tx_power"] = c.tx_power;
    radio["freq_p0"] = c.freq[0];
    radio["freq_p1"] = c.freq[1];
    radio["repeats"] = c.repeats;
    radio["off_threshold"] = c.off_threshold;
    radio["refresh_ms"] = c.refresh_ms;
    radio["tx_jitter_ms"] = c.tx_jitter_ms;
    radio["lbt_enabled"] = (bool) c.lbt_enabled;
    radio["lbt_threshold_dbm"] = c.lbt_threshold;

    o["display"]["sleep_min"] = display_sleep_minutes(c);
    JsonObject update = o["update"].to<JsonObject>();
    update["repo"] = c.update_repo;

    JsonArray zones = o["zones"].to<JsonArray>();
    for (uint8_t i = 0; i < c.num_zones; i++) {
        const ZoneConfig &z = c.zones[i];
        JsonObject j = zones.add<JsonObject>();
        j["enabled"] = (bool) z.enabled;
        j["name"] = z.name;
        protocols_to_json(z.protocols, j["protocols"].to<JsonArray>());
        j["addr_p0"] = hex_string(z.addr, 4);
        j["addr_p1"] = hex_string(z.p1_addr, 2);
        j["start"] = zone_start_channel(c, i);  // informational
    }
}

bool app_from_json(JsonObjectConst in, AppConfig &c, String &err) {

    if (in["name"].is<const char *>())
        copy_name(c.name, sizeof(c.name), in["name"]);
    if (!in["output_enabled"].isNull())
        c.output_enabled = in["output_enabled"].as<bool>();
    if (in["role"].is<const char *>()) {
        String role = in["role"].as<const char *>();
        if (role != "receiver" && role != "controller") {
            err = "role must be \"controller\" or \"receiver\"";
            return false;
        }
        c.receiver = role == "receiver";
    }
    if (in["rx_profile"].is<const char *>()) {
        const char *p = in["rx_profile"];
        bool found = false;
        for (uint8_t i = 0; i < NUM_RX_PROFILES; i++) {
            if (strcmp(p, RX_PROFILE_NAMES[i]) == 0) {
                c.rx_profile = i;
                found = true;
            }
        }
        if (!found) {
            err = "rx_profile must be \"normal\", \"near\" or \"wide\"";
            return false;
        }
    }

    JsonObjectConst sync = in["sync"];
    if (!sync.isNull()) {
        if (sync["enabled"].is<bool>())
            c.sync_enabled = sync["enabled"].as<bool>();
        if (sync["master"].is<bool>())
            c.sync_master = sync["master"].as<bool>();
        if (!read_int(sync, "copies", 1, 3, c.sync_copies, err))
            return false;
    }

    // devices.protocols sets every zone's protocols. Zones keep both addresses, so nothing has to be re-entered.
    JsonObjectConst br = in["devices"];
    uint8_t every_zone = 0;
    if (!br.isNull()) {
        // With "per_zone" set, "protocols" is only what the zones use between them (as /api/config reports it),
        // not an instruction.
        if (!br["protocols"].isNull() && !(br["per_zone"] | false)) {
            if (!protocols_from_json(br["protocols"], every_zone, "devices", err))
                return false;
            c.protocol = every_zone == rfproto::PROTOCOL_BIT[1] ? 1 : 0;
        }
        if (br["mode"].is<const char *>()) {
            String m = br["mode"].as<const char *>();
            c.mode = m == "vendor" ? MODE_VENDOR : m == "dmx" ? MODE_DMX : MODE_PIXEL;
        }
        if (br["base_layer"].is<bool>())
            c.base_layer = br["base_layer"].as<bool>();
        if (br["color_order"].is<const char *>()) {
            String order = br["color_order"].as<const char *>();
            bool found = false;
            for (uint8_t k = 0; k < rfproto::NUM_ORDERS; k++) {
                if (order.equalsIgnoreCase(rfproto::COLOR_ORDER_NAMES[k])) {
                    c.color_order = k;
                    found = true;
                }
            }
            if (!found) {
                err = "unknown colour order";
                return false;
            }
        }
    }

    JsonObjectConst input = in["input"];
    if (!input.isNull()) {
        if (!input["ddp_enabled"].isNull())
            c.ddp_enabled = input["ddp_enabled"].as<bool>();
        if (!input["e131_enabled"].isNull())
            c.e131_enabled = input["e131_enabled"].as<bool>();
        if (!input["e131_multicast"].isNull())
            c.e131_multicast = input["e131_multicast"].as<bool>();
        if (!read_int(input, "e131_universe", 1, 63999, c.e131_universe, err) ||
            !read_int(input, "ddp_port", 1, 65535, c.ddp_port, err) ||
            !read_int(input, "start_channel", 1, MAX_CHANNELS, c.start_channel, err) ||
            !read_int(input, "timeout_s", 0, 86400, c.input_timeout_s, err))
            return false;
    }

    JsonObjectConst radio = in["radio"];
    if (!radio.isNull()) {
        if (radio["type"].is<const char *>()) {
            String t = radio["type"].as<const char *>();
            bool found = false;
            for (uint8_t i = 0; i < NUM_RADIO_TYPES; i++) {
                if (t == RADIO_NAMES[i]) {
                    c.radio_type = i;
                    found = true;
                }
            }
            if (!found) {
                err = "unknown radio type";
                return false;
            }
        }
        if (!read_int(radio, "tx_power", -30, 20, c.tx_power, err) ||
            !read_int(radio, "freq_p0", 300000000, 928000000, c.freq[0], err) ||
            !read_int(radio, "freq_p1", 300000000, 928000000, c.freq[1], err) ||
            !read_int(radio, "repeats", 1, 10, c.repeats, err) ||
            !read_int(radio, "off_threshold", 0, 255, c.off_threshold, err) ||
            !read_int(radio, "refresh_ms", 0, 60000, c.refresh_ms, err) ||
            !read_int(radio, "tx_jitter_ms", 0, 200, c.tx_jitter_ms, err) ||
            !read_int(radio, "lbt_threshold_dbm", -120, -30, c.lbt_threshold, err))
            return false;
        if (!radio["lbt_enabled"].isNull())
            c.lbt_enabled = radio["lbt_enabled"].as<bool>();
    }

    if (in["update"]["repo"].is<const char *>()) {
        const char *repo = in["update"]["repo"];
        if (!*repo)
            repo = updater::DEFAULT_REPO;  // empty = back to the default
        if (strlen(repo) >= sizeof(c.update_repo) || !updater::valid_repo(repo)) {
            err = "update source must be a GitHub repository as owner/name";
            return false;
        }
        strlcpy(c.update_repo, repo, sizeof(c.update_repo));
    }
    if (!in["display"]["sleep_min"].isNull()) {
        uint8_t minutes = 0;
        if (!read_int(in["display"].as<JsonObjectConst>(), "sleep_min", 0, 240, minutes, err))
            return false;
        c.display_sleep = minutes == 0 ? DISPLAY_SLEEP_NEVER : minutes;
    }
    JsonObjectConst update = in["update"];
    if (!update.isNull()) {
    }

    JsonArrayConst zones = in["zones"];
    if (!zones.isNull()) {
        if (zones.size() < 1 || zones.size() > MAX_ZONES) {
            err = "1-16 zones required";
            return false;
        }
        uint8_t n = 0, added = new_zone_protocols(c);
        for (JsonObjectConst j : zones) {
            ZoneConfig &z = c.zones[n];
            String where = String("zone ") + (n + 1);
            if (n >= c.num_zones)
                zone_defaults(z, n, added);  // a zone being added starts from its defaults
            z.enabled = j["enabled"] | true;
            if (j["name"].is<const char *>())
                copy_name(z.name, sizeof(z.name), j["name"]);
            uint8_t tmp[4] = {0, 0, 0, 0};
            if (!j["addr_p0"].isNull()) {
                if (!j["addr_p0"].is<const char *>() || !parse_hex(j["addr_p0"], tmp, 4)) {
                    err = where + ": a protocol 0 address is 4 bytes";
                    return false;
                }
                memcpy(z.addr, tmp, 4);
            }
            if (!j["addr_p1"].isNull()) {
                if (!j["addr_p1"].is<const char *>() || !parse_hex(j["addr_p1"], tmp, 2)) {
                    err = where + ": a protocol 1 address is 2 bytes (group code, byte 6)";
                    return false;
                }
                memcpy(z.p1_addr, tmp, 2);
            }
            if (!j["protocols"].isNull() && !protocols_from_json(j["protocols"], z.protocols, where, err))
                return false;
            n++;
        }
        c.num_zones = n;
    }
    // Set after the zones, so "protocols" on the controller wins over the zones' own when both are sent.
    if (every_zone)
        for (uint8_t i = 0; i < MAX_ZONES; i++)
            c.zones[i].protocols = every_zone;

    if (c.mode == MODE_VENDOR) {
        for (uint8_t i = 0; i < c.num_zones; i++) {
            if (c.zones[i].enabled && !zone_uses(c.zones[i], 1)) {
                err = String("vendor 5-channel mode drives protocol 1 (LedGiftSupplier) devices only: zone ") +
                      (i + 1) + " doesn't use protocol 1";
                return false;
            }
        }
    }
    uint32_t last = (uint32_t) c.start_channel + (uint32_t) c.num_zones * zone_width(c) - 1;
    if (last > MAX_CHANNELS) {
        err = String("zones would end at channel ") + last + " (max " + MAX_CHANNELS + ")";
        return false;
    }
    return true;
}

static String ip_string(uint32_t v) { return v ? IPAddress(v).toString() : String(""); }

void net_to_json(const NetConfig &c, JsonObject o, bool include_secrets) {
    o["hostname"] = c.hostname;
    o["eth_enabled"] = (bool) c.eth_enabled;
    o["wifi_ssid"] = c.wifi_ssid;
    o["wifi_pass_set"] = c.wifi_pass[0] != '\0';
    if (include_secrets)
        o["wifi_pass"] = c.wifi_pass;
    o["dhcp"] = (bool) c.dhcp;
    o["ip"] = ip_string(c.ip);
    o["gateway"] = ip_string(c.gateway);
    o["subnet"] = ip_string(c.subnet);
    o["dns"] = ip_string(c.dns);
    static const char *const AP_MODES[] = {"no_connection", "always", "never"};
    o["ap_mode"] = AP_MODES[c.ap_mode % 3];
    o["auth_enabled"] = (bool) c.auth_enabled;
}

bool net_from_json(JsonObjectConst in, NetConfig &c, String &err) {
    if (in["hostname"].is<const char *>()) {
        String h = in["hostname"].as<const char *>();
        h.trim();
        if (h.length() == 0 || h.length() > 32) {
            err = "hostname must be 1-32 characters";
            return false;
        }
        for (char ch : h) {
            if (!isalnum((unsigned char) ch) && ch != '-') {
                err = "hostname may only contain letters, digits and '-'";
                return false;
            }
        }
        strlcpy(c.hostname, h.c_str(), sizeof(c.hostname));
    }
    if (!in["eth_enabled"].isNull())
        c.eth_enabled = in["eth_enabled"].as<bool>();
    if (in["wifi_ssid"].is<const char *>())
        strlcpy(c.wifi_ssid, in["wifi_ssid"], sizeof(c.wifi_ssid));
    if (in["wifi_pass"].is<const char *>())  // omitted = keep existing
        strlcpy(c.wifi_pass, in["wifi_pass"], sizeof(c.wifi_pass));
    if (!in["dhcp"].isNull())
        c.dhcp = in["dhcp"].as<bool>();
    if (!c.dhcp) {
        if (!parse_ip(in["ip"], c.ip) || !parse_ip(in["subnet"], c.subnet)) {
            err = "static IP needs a valid address and subnet mask";
            return false;
        }
        if (!in["gateway"].isNull() && !parse_ip(in["gateway"], c.gateway))
            c.gateway = 0;
        if (!in["dns"].isNull() && !parse_ip(in["dns"], c.dns))
            c.dns = 0;
    }
    if (in["ap_mode"].is<const char *>()) {
        String m = in["ap_mode"].as<const char *>();
        c.ap_mode = m == "always" ? AP_ALWAYS : m == "never" ? AP_NEVER : AP_NO_CONNECTION;
    }
    if (in["ap_pass"].is<const char *>()) {
        String p = in["ap_pass"].as<const char *>();
        if (p.length() > 0 && p.length() < 8) {
            err = "AP password must be at least 8 characters";
            return false;
        }
        strlcpy(c.ap_pass, p.c_str(), sizeof(c.ap_pass));
    }
    if (!c.eth_enabled && c.wifi_ssid[0] == '\0' && c.ap_mode == AP_NEVER) {
        err = "that would leave the device with no way to connect";
        return false;
    }
    return true;
}
