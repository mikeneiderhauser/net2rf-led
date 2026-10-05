#include "config.h"
#include "updater.h"

#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

AppConfig g_app;
NetConfig g_net;

static const uint32_t APP_MAGIC = 0x52464204;  // "RFB" v4, bump when AppConfig layout changes
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

// Zone 0 reaches every bracelet; zone N (1-15) reaches group N only.
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

static void zone_defaults(ZoneConfig &z, uint8_t index, uint8_t protocol) {
    memset(&z, 0, sizeof(z));
    z.enabled = 1;
    zone_default_address(protocol, index, z.addr);
    if (index == 0)
        strlcpy(z.name, "All Zones", sizeof(z.name));
    else
        snprintf(z.name, sizeof(z.name), "Zone %u", index);
}

void config_reset_zone(AppConfig &c, uint8_t index) {
    if (index < MAX_ZONES)
        zone_defaults(c.zones[index], index, c.protocol);
}

void config_defaults_app(AppConfig &c) {
    memset(&c, 0, sizeof(c));
    c.magic = APP_MAGIC;
    strlcpy(c.name, "Net2RF LED", sizeof(c.name));
    c.protocol = 0;  // the protocol tested on real bracelets
    c.mode = MODE_PIXEL;
    c.color_order = bracelet::ORDER_RGB;
    c.num_zones = 5;  // All Zones + Zone 1-4 (the smaller xLights model, tools/xlights)
    c.start_channel = 1;
    c.ddp_port = 4048;
    c.input_timeout_s = 300;
    c.radio_type = RADIO_CC1101;
    c.tx_power = 10;
    c.freq[0] = bracelet::DEFAULT_FREQ_P0;
    c.freq[1] = bracelet::DEFAULT_FREQ_P1;
    c.repeats = 3;
    c.off_threshold = 16;
    c.refresh_ms = 0;  // bracelets latch the last colour; re-sending only adds airtime (and car-fob interference)
    c.tx_jitter_ms = 0;
    c.output_enabled = 1;
    c.ddp_enabled = 1;
    c.e131_enabled = 0;  // off by default: a shared universe could drive the bracelets unintentionally
    c.e131_multicast = 1;
    c.e131_universe = 1;
    for (uint8_t i = 0; i < MAX_ZONES; i++)
        zone_defaults(c.zones[i], i, c.protocol);
    c.lbt_enabled = 0;  // optional until tested with several controllers in range of each other
    c.lbt_threshold = LBT_DEFAULT_THRESHOLD;
    c.radio_off = 0;
    strlcpy(c.update_repo, updater::DEFAULT_REPO, sizeof(c.update_repo));
    c.update_check_off = 0;
    c.update_check_hours = UPDATE_CHECK_DEFAULT_HOURS;
    c.base_layer = 1;  // new controllers only: saved settings keep the old behaviour until switched on
    c.display_sleep = 0;  // the default (10 minutes)
}

// AppConfig as saved by firmware before the listen-before-talk fields were appended.
static const size_t APP_V4_SIZE = (offsetof(AppConfig, lbt_enabled) + 3) & ~(size_t) 3;
// ... and before the update source was appended.
static const size_t APP_V5_SIZE = (offsetof(AppConfig, update_repo) + 3) & ~(size_t) 3;
// ... and before the automatic update check was appended.
static const size_t APP_V6_SIZE = (offsetof(AppConfig, update_check_off) + 3) & ~(size_t) 3;

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

void config_load() {
    Preferences p;
    p.begin(NVS_NS, true);
    size_t app_len = p.getBytesLength("app");
    bool app_ok = false;
    if (app_len == sizeof(AppConfig) || app_len == APP_V6_SIZE || app_len == APP_V5_SIZE || app_len == APP_V4_SIZE) {
        config_defaults_app(g_app);  // fields missing from an older, shorter record keep their defaults
        app_ok = p.getBytes("app", &g_app, app_len) == app_len && g_app.magic == APP_MAGIC;
        if (app_ok && app_len != sizeof(AppConfig)) {
            log_i("Upgraded saved settings to the current layout");
            g_app.base_layer = 0;  // an existing setup keeps sending exactly as before until this is switched on
        }
    }
    bool net_ok = p.getBytesLength("net") == sizeof(NetConfig) && p.getBytes("net", &g_net, sizeof(g_net)) &&
                  g_net.magic == NET_MAGIC;
    p.end();
    if (!app_ok) {
        log_i("No saved app config, using defaults");
        config_defaults_app(g_app);
    }
    if (!net_ok) {
        log_i("No saved network config, using defaults");
        config_defaults_net(g_net);
    }
    if (g_app.num_zones == 0 || g_app.num_zones > MAX_ZONES)
        g_app.num_zones = 1;
    if (g_app.radio_type >= NUM_RADIO_TYPES)  // e.g. the removed plain-OOK option
        g_app.radio_type = RADIO_CC1101;
    g_app.update_repo[sizeof(g_app.update_repo) - 1] = 0;
    if (!updater::valid_repo(g_app.update_repo))  // also an older record, whose padding lands here
        strlcpy(g_app.update_repo, updater::DEFAULT_REPO, sizeof(g_app.update_repo));
    if (g_app.update_check_hours < 1 || g_app.update_check_hours > 168)
        g_app.update_check_hours = UPDATE_CHECK_DEFAULT_HOURS;
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

static String hex_string(const uint8_t *data, size_t len) {
    String s;
    char b[3];
    for (size_t i = 0; i < len; i++) {
        snprintf(b, sizeof(b), "%02X", data[i]);
        s += b;
    }
    return s;
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

    JsonObject br = o["bracelets"].to<JsonObject>();
    br["protocol"] = c.protocol;
    br["mode"] = c.mode == MODE_VENDOR ? "vendor" : c.mode == MODE_DMX ? "dmx" : "pixel";
    br["color_order"] = bracelet::COLOR_ORDER_NAMES[c.color_order % bracelet::NUM_ORDERS];
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
    radio["power"] = !c.radio_off;

    o["display"]["sleep_min"] = display_sleep_minutes(c);
    JsonObject update = o["update"].to<JsonObject>();
    update["repo"] = c.update_repo;
    update["auto_check"] = !c.update_check_off;
    update["check_hours"] = c.update_check_hours;

    JsonArray zones = o["zones"].to<JsonArray>();
    for (uint8_t i = 0; i < c.num_zones; i++) {
        const ZoneConfig &z = c.zones[i];
        JsonObject j = zones.add<JsonObject>();
        j["enabled"] = (bool) z.enabled;
        j["name"] = z.name;
        j["addr"] = hex_string(z.addr, address_len(c.protocol));
        j["start"] = zone_start_channel(c, i);  // informational
    }
}

bool app_from_json(JsonObjectConst in, AppConfig &c, String &err) {
    uint8_t old_protocol = c.protocol;

    if (in["name"].is<const char *>())
        copy_name(c.name, sizeof(c.name), in["name"]);
    if (!in["output_enabled"].isNull())
        c.output_enabled = in["output_enabled"].as<bool>();

    JsonObjectConst br = in["bracelets"];
    if (!br.isNull()) {
        if (!read_int(br, "protocol", 0, 1, c.protocol, err))
            return false;
        if (br["mode"].is<const char *>()) {
            String m = br["mode"].as<const char *>();
            c.mode = m == "vendor" ? MODE_VENDOR : m == "dmx" ? MODE_DMX : MODE_PIXEL;
        }
        if (br["base_layer"].is<bool>())
            c.base_layer = br["base_layer"].as<bool>();
        if (br["color_order"].is<const char *>()) {
            String order = br["color_order"].as<const char *>();
            bool found = false;
            for (uint8_t k = 0; k < bracelet::NUM_ORDERS; k++) {
                if (order.equalsIgnoreCase(bracelet::COLOR_ORDER_NAMES[k])) {
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
        if (radio["power"].is<bool>())
            c.radio_off = !radio["power"].as<bool>();
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
        if (update["auto_check"].is<bool>())
            c.update_check_off = !update["auto_check"].as<bool>();
        if (!read_int(update, "check_hours", 1, 168, c.update_check_hours, err))
            return false;
    }

    JsonArrayConst zones = in["zones"];
    if (!zones.isNull()) {
        if (zones.size() < 1 || zones.size() > MAX_ZONES) {
            err = "1-16 zones required";
            return false;
        }
        uint8_t n = 0;
        for (JsonObjectConst j : zones) {
            ZoneConfig &z = c.zones[n];
            z.enabled = j["enabled"] | true;
            if (j["name"].is<const char *>())
                copy_name(z.name, sizeof(z.name), j["name"]);
            if (!j["addr"].isNull()) {
                uint8_t tmp[4] = {0, 0, 0, 0};
                if (!parse_hex(j["addr"], tmp, address_len(c.protocol))) {
                    err = String("zone ") + (n + 1) + ": a protocol " + c.protocol + " address is " +
                          address_len(c.protocol) + " bytes";
                    return false;
                }
                memcpy(z.addr, tmp, 4);
            }
            n++;
        }
        c.num_zones = n;
    } else if (c.protocol != old_protocol) {
        // Addresses mean different bytes in each protocol: reset them unless new ones were given.
        for (uint8_t i = 0; i < MAX_ZONES; i++)
            zone_default_address(c.protocol, i, c.zones[i].addr);
    }

    if (c.mode == MODE_VENDOR && c.protocol != 1) {
        err = "vendor 5-channel mode is for LedGiftSupplier (protocol 1) bracelets";
        return false;
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
