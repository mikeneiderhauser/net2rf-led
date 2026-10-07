#include "web.h"

#include <ArduinoJson.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <nvs.h>
#include <mbedtls/base64.h>

#include "config.h"
#include "engine.h"
#include "net.h"
#include "ota_guard.h"
#include "panel.h"
#include "pins.h"
#include "updater.h"
#include "web_ui.h"

#include <wled_compat.h>

namespace web {

static WebServer s_server(80);
static uint32_t s_reboot_at = 0;
static String s_update_error;
static bool s_update_denied = false;
static bool s_update_forbidden = false;

// Brute-force guard: after this many wrong passwords, refuse all attempts for a while.
static const uint8_t AUTH_MAX_FAILURES = 5;
static const uint32_t AUTH_LOCKOUT_MS = 30000;
static uint8_t s_auth_failures = 0;
static uint32_t s_auth_last_failure = 0;
static const char *const AUTH_REALM = "Net2RF LED";

static void schedule_reboot(uint32_t delay_ms = 800) { s_reboot_at = millis() + delay_ms; }

static void send_json(int code, JsonDocument &doc) {
    String body;
    serializeJson(doc, body);
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(code, "application/json", body);
}

static void send_ok(bool reboot = false) {
    JsonDocument doc;
    doc["ok"] = true;
    doc["reboot"] = reboot;
    send_json(200, doc);
}

static void send_error(int code, const String &msg) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = msg;
    send_json(code, doc);
}

// ---------------------------------------------------------------------------------------------
// Cross-site request protection
//
// A web page on another site could otherwise make the browser POST to the controller (and, once the
// admin password is cached, with credentials). Every state-changing request must therefore:
//  * come from this controller's own page (Origin header, when a browser sends one, must match Host);
//  * be addressed to the controller by IP or by its own name (blocks DNS-rebinding tricks);
//  * carry a JSON body (Content-Type: application/json), which browsers cannot send cross-site
//    without a CORS preflight that the controller never approves. Firmware upload is multipart,
//    so it relies on the first two checks.
// ---------------------------------------------------------------------------------------------

static bool is_ipv4_literal(const String &host) {
    IPAddress ip;
    return ip.fromString(host);
}

// Host header must be an IP address, or our hostname / the shared alias, bare or with ".local". Any other
// domain is refused: a DNS-rebinding page can choose its own domain (e.g. "net2rf.attacker.example"), so
// matching only the first label would let it through.
static bool own_name(const String &host, const char *name) {
    if (host.equalsIgnoreCase(name))
        return true;
    String local = String(name) + ".local";
    return host.equalsIgnoreCase(local) || host.equalsIgnoreCase(local + ".");
}

static bool trusted_host(String host) {
    int colon = host.indexOf(':');
    if (colon >= 0)
        host = host.substring(0, colon);
    if (host.isEmpty() || is_ipv4_literal(host))
        return true;
    String hostname;
    {
        StateLock lock;
        hostname = g_net.hostname;
    }
    return own_name(host, hostname.c_str()) || own_name(host, net::SHARED_ALIAS);
}

static bool same_origin() {
    if (!trusted_host(s_server.hostHeader()))
        return false;
    String origin = s_server.header("Origin");
    if (origin.isEmpty())
        return true;  // non-browser clients (curl, Home Assistant) send no Origin
    int p = origin.indexOf("://");
    String host = p >= 0 ? origin.substring(p + 3) : origin;
    return host.equalsIgnoreCase(s_server.hostHeader());
}

static bool json_request() { return s_server.header("Content-Type").startsWith("application/json"); }

// ---------------------------------------------------------------------------------------------
// Admin password (HTTP Basic, user "admin")
// ---------------------------------------------------------------------------------------------

static bool auth_locked_out() {
    return s_auth_failures >= AUTH_MAX_FAILURES && millis() - s_auth_last_failure < AUTH_LOCKOUT_MS;
}

// True if no password is set, or the request carries valid credentials.
static bool authorized() {
    {
        StateLock lock;
        if (!g_net.auth_enabled)
            return true;
    }
    String header = s_server.header("Authorization");
    if (!header.startsWith("Basic "))
        return false;  // no credentials offered: not a failed attempt
    if (auth_locked_out())
        return false;
    String b64 = header.substring(6);
    b64.trim();
    unsigned char decoded[128];
    size_t n = 0;
    bool ok = mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &n, (const unsigned char *) b64.c_str(),
                                    b64.length()) == 0;
    if (ok) {
        decoded[n] = '\0';
        char *colon = strchr((char *) decoded, ':');
        ok = colon != nullptr;
        if (ok) {
            *colon = '\0';
            StateLock lock;
            ok = strcmp((const char *) decoded, ADMIN_USER) == 0 && auth_check(g_net, colon + 1);
        }
    }
    if (ok) {
        s_auth_failures = 0;
    } else {
        if (s_auth_failures < 255)
            s_auth_failures++;
        s_auth_last_failure = millis();
        log_w("Admin login failed (%u)", s_auth_failures);
    }
    return ok;
}

static void send_auth_challenge() {
    if (auth_locked_out()) {
        JsonDocument doc;
        doc["ok"] = false;
        doc["error"] = "too many wrong passwords, try again in 30 s";
        send_json(429, doc);
        return;
    }
    s_server.requestAuthentication(BASIC_AUTH, AUTH_REALM, "{\"ok\":false,\"error\":\"admin password required\"}");
}

// Rejects cross-site POSTs (see above). Returns false after sending an error.
static bool post_allowed() {
    if (s_server.method() != HTTP_POST)
        return true;
    if (!same_origin()) {
        log_w("Blocked cross-site request to %s (Host %s, Origin %s)", s_server.uri().c_str(),
              s_server.hostHeader().c_str(), s_server.header("Origin").c_str());
        send_error(403, "cross-site request blocked");
        return false;
    }
    if (!json_request()) {
        send_error(415, "Content-Type must be application/json");
        return false;
    }
    return true;
}

// Wraps a handler so it only runs for same-origin, authorized requests.
static std::function<void()> protect(std::function<void()> handler) {
    return [handler]() {
        if (!post_allowed())
            return;
        if (authorized())
            handler();
        else
            send_auth_challenge();
    };
}

// Same-origin check only (for harmless POSTs that don't need the password).
static std::function<void()> same_origin_only(std::function<void()> handler) {
    return [handler]() {
        if (post_allowed())
            handler();
    };
}

static bool parse_body(JsonDocument &doc) {
    DeserializationError e = deserializeJson(doc, s_server.arg("plain"));
    if (e) {
        send_error(400, String("invalid JSON: ") + e.c_str());
        return false;
    }
    if (!doc.is<JsonObject>()) {
        send_error(400, "expected a JSON object");
        return false;
    }
    return true;
}

static const char *reset_reason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "power on";
        case ESP_RST_SW:
            return "software restart";
        case ESP_RST_PANIC:
            return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_EXT:
            return "reset button";
        default:
            return "other";
    }
}

// Why a transmission was refused (409).
static const char *tx_refused_reason() {
    StateLock lock;
    return g_app.receiver     ? "receiver mode: this controller only listens"
           : g_app.radio_off  ? "radio is shut down"
           : !g_app.output_enabled ? "RF output is disabled"
                                   : "transmit queue full";
}

// ---------------------------------------------------------------------------------------------
// Handlers
// ---------------------------------------------------------------------------------------------

static void handle_index() {
    s_server.sendHeader("Cache-Control", "no-cache");
    s_server.sendHeader("Content-Encoding", "gzip");
    s_server.send_P(200, "text/html", (const char *) INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

static void handle_status() {
    JsonDocument doc;
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["firmware"] = FW_VERSION;
    dev["built"] = __DATE__ " " __TIME__;
    dev["uptime_s"] = millis() / 1000;
    dev["free_heap"] = ESP.getFreeHeap();
    dev["min_free_heap"] = ESP.getMinFreeHeap();
    dev["heap_bytes"] = ESP.getHeapSize();
    // Firmware slot: how big this firmware is and how much room an update has. Fixed until the next flash.
    static const uint32_t fw_bytes = ESP.getSketchSize(), slot_bytes = ESP.getFreeSketchSpace();
    dev["firmware_bytes"] = fw_bytes;
    dev["firmware_slot_bytes"] = slot_bytes;
    dev["chip"] = ESP.getChipModel();
    dev["board"] = NET2RF_BOARD_ID;  // release assets are per board
    dev["board_name"] = NET2RF_BOARD_NAME;
    dev["chip_rev"] = ESP.getChipRevision();
    dev["reset_reason"] = reset_reason();
    dev["display"] = panel::display_present();
    panel::display_json(dev["display_info"].to<JsonObject>());
    dev["suffix"] = device_suffix();
    dev["update_pending"] = ota_guard::pending();
    dev["update_rolled_back"] = ota_guard::rolled_back();
    updater::status_json(dev["update_job"].to<JsonObject>());
    updater::check_json(dev["update_check"].to<JsonObject>());
    {
        StateLock lock;
        dev["auth"] = (bool) g_net.auth_enabled;
        dev["name"] = g_app.name;
    }
    net::status_json(doc["network"].to<JsonObject>());
    g_engine.status_json(doc["engine"].to<JsonObject>());
    send_json(200, doc);
}

static void handle_get_config() {
    JsonDocument doc;
    {
        StateLock lock;
        app_to_json(g_app, doc["app"].to<JsonObject>());
        net_to_json(g_net, doc["network"].to<JsonObject>(), false);
    }
    send_json(200, doc);
}

static void handle_post_config() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    String err;
    bool reboot = false;
    AppConfig next;
    {
        StateLock lock;
        next = g_app;
        if (!app_from_json(doc.as<JsonObjectConst>(), next, err)) {
            send_error(400, err);
            return;
        }
        reboot = next.radio_type != g_app.radio_type;  // radio driver is chosen at boot
        g_app = next;
        g_engine.config_changed();
    }
    config_save_app(next);
    if (!doc["update"]["repo"].isNull()) {  // a new release source: the old result no longer applies
        if (next.update_check_off)
            updater::forget();
        else
            updater::check_now();
    }
    if (reboot)
        schedule_reboot();
    send_ok(reboot);
}

static void handle_post_network() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    String err;
    NetConfig next;
    {
        StateLock lock;
        next = g_net;
        if (!net_from_json(doc.as<JsonObjectConst>(), next, err)) {
            send_error(400, err);
            return;
        }
        g_net = next;
    }
    config_save_net(next);
    schedule_reboot(1500);
    send_ok(true);
}

// Global RF enable. Disabled = DDP is still received and counted, but nothing is transmitted.
static void handle_output() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    if (!doc["enabled"].is<bool>()) {
        send_error(400, "expected {\"enabled\": true|false}");
        return;
    }
    AppConfig saved;
    {
        StateLock lock;
        g_app.output_enabled = doc["enabled"].as<bool>();
        saved = g_app;
    }
    config_save_app(saved);
    JsonDocument out;
    out["ok"] = true;
    out["enabled"] = doc["enabled"].as<bool>();
    send_json(200, out);
}

// Radio power. Off = the radio chip is put to sleep (persisted); nothing is transmitted until it is switched
// back on, which re-initialises the chip and re-sends the current colours.
static void handle_radio() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    if (!doc["power"].is<bool>()) {
        send_error(400, "expected {\"power\": true|false}");
        return;
    }
    AppConfig saved;
    {
        StateLock lock;
        g_app.radio_off = !doc["power"].as<bool>();
        saved = g_app;
    }
    config_save_app(saved);
    JsonDocument out;
    out["ok"] = true;
    out["power"] = doc["power"].as<bool>();
    send_json(200, out);
}

// Compact counters for monitoring (Home Assistant REST sensor, Grafana, scripts).
static void handle_stats() {
    JsonDocument full;
    g_engine.status_json(full.to<JsonObject>());
    JsonDocument doc;
    doc["uptime_s"] = millis() / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["firmware"] = FW_VERSION;
    updater::check_json(doc["update"].to<JsonObject>());
    {
        StateLock lock;
        doc["name"] = g_app.name;
        doc["output_enabled"] = (bool) g_app.output_enabled;
    }
    doc["radio"] = full["radio"]["state"];
    doc["input"] = full["input"];
    doc["output"] = full["output"];
    doc["test_mode"] = full["test"]["mode"];
    doc["role"] = full["role"];
    if (full["receiver"]["enabled"].as<bool>()) {
        JsonObject rx = doc["receiver"].to<JsonObject>();
        for (const char *k : {"active", "frames", "bad", "updates", "rssi_dbm", "last_age_ms"})
            rx[k] = full["receiver"][k];
    }
    JsonArray zones = doc["zones"].to<JsonArray>();
    {
        StateLock lock;
        JsonArrayConst src = full["zones"];
        for (uint8_t i = 0; i < src.size(); i++) {
            JsonObject z = zones.add<JsonObject>();
            z["name"] = g_app.zones[i].name;
            z["enabled"] = (bool) g_app.zones[i].enabled;
            z["rgb"] = src[i]["rgb"];
            z["updates"] = src[i]["tx"];
            z["last_tx_age_ms"] = src[i]["tx_age_ms"];
        }
    }
    send_json(200, doc);
}

static void handle_test() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    String mode = doc["mode"] | "off";
    uint8_t rgb[3] = {255, 255, 255};
    parse_hex(doc["rgb"] | "FFFFFF", rgb, 3);
    TestMode m = mode == "solid" ? TestMode::SOLID : mode == "cycle" ? TestMode::CYCLE : TestMode::OFF;
    g_engine.set_test(m, rgb[0], rgb[1], rgb[2]);
    send_ok();
}

static void handle_send() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    int zone = doc["zone"] | -1;
    uint8_t num_zones;
    {
        StateLock lock;
        num_zones = g_app.num_zones;
    }
    if (zone >= num_zones) {
        send_error(400, String("no zone ") + (zone + 1) + " (" + num_zones + " saved; save new zones first)");
        return;
    }
    String act = doc["action"] | "color";
    uint8_t rgb[3] = {0, 0, 0};
    parse_hex(doc["rgb"] | "000000", rgb, 3);
    rfproto::Action action = act == "off"   ? rfproto::ACTION_OFF
                              : act == "fxa" ? rfproto::ACTION_FX_A
                              : act == "fxb" ? rfproto::ACTION_FX_B
                              : act == "fxc" ? rfproto::ACTION_FX_C
                                             : rfproto::ACTION_COLOR;
    bool ok = zone < 0 && action == rfproto::ACTION_OFF ? g_engine.all_off()
                                                         : g_engine.send_zone(zone, action, rgb[0], rgb[1], rgb[2]);
    if (!ok) {
        send_error(409, tx_refused_reason());
        return;
    }
    send_ok();
}

static void handle_all_off() {
    if (!g_engine.all_off()) {
        send_error(409, "RF output is disabled");
        return;
    }
    send_ok();
}

static void handle_raw() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    uint8_t pkt[rfproto::PACKET_LEN];
    if (!parse_hex(doc["hex"] | "", pkt, rfproto::PACKET_LEN)) {
        send_error(400, "hex must be 7 bytes");
        return;
    }
    uint8_t repeats;
    {
        StateLock lock;
        repeats = g_app.repeats;
    }
    repeats = doc["repeats"] | repeats;
    if (!g_engine.send_raw(doc["protocol"] | 1, pkt, repeats, doc["fix"] | true)) {
        send_error(409, tx_refused_reason());
        return;
    }
    send_ok();
}

static void handle_export() {
    JsonDocument doc;
    doc["format"] = "net2rf-led";
    doc["firmware"] = FW_VERSION;
    {
        StateLock lock;
        app_to_json(g_app, doc["app"].to<JsonObject>());
        net_to_json(g_net, doc["network"].to<JsonObject>(), false);  // never export the Wi-Fi password
    }
    String body;
    serializeJsonPretty(doc, body);
    String name = String("net2rf-") + device_suffix() + ".json";
    s_server.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    s_server.send(200, "application/json", body);
}

static void handle_import() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    if (doc["app"].isNull()) {
        send_error(400, "not a controller config file");
        return;
    }
    String err;
    bool include_net = doc["include_network"] | false;
    AppConfig next;
    NetConfig next_net;
    {
        StateLock lock;
        next = g_app;
        if (!app_from_json(doc["app"].as<JsonObjectConst>(), next, err)) {
            send_error(400, err);
            return;
        }
        next_net = g_net;
        if (include_net && !doc["network"].isNull() &&
            !net_from_json(doc["network"].as<JsonObjectConst>(), next_net, err)) {
            send_error(400, err);
            return;
        }
        g_app = next;
        if (include_net)
            g_net = next_net;
    }
    config_save_app(next);
    if (include_net)
        config_save_net(next_net);
    schedule_reboot();
    send_ok(true);
}

// Set, change or remove the admin password: {"password": "..."}; "" removes it.
static void handle_auth() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    if (!doc["password"].is<const char *>()) {
        send_error(400, "expected {\"password\": \"...\"}");
        return;
    }
    String pw = doc["password"].as<const char *>();
    if (pw.length() > 0 && pw.length() < MIN_ADMIN_PASSWORD) {
        send_error(400, String("password must be at least ") + MIN_ADMIN_PASSWORD + " characters");
        return;
    }
    if (pw.length() > 64) {
        send_error(400, "password must be at most 64 characters");
        return;
    }
    NetConfig saved;
    {
        StateLock lock;
        auth_set_password(g_net, pw.c_str());
        saved = g_net;
    }
    config_save_net(saved);
    log_i("Admin password %s", pw.length() ? "set" : "removed");
    JsonDocument out;
    out["ok"] = true;
    out["auth"] = pw.length() > 0;
    send_json(200, out);
}

static void handle_update_done() {
    if (s_update_forbidden) {
        s_update_forbidden = false;
        send_error(403, "cross-site request blocked");
        return;
    }
    if (s_update_denied) {
        s_update_denied = false;
        send_auth_challenge();
        return;
    }
    bool ok = !Update.hasError() && s_update_error.isEmpty();
    if (ok) {
        schedule_reboot(1000);
        send_ok(true);
    } else {
        g_engine.set_suspended(false);
        send_error(500, s_update_error.isEmpty() ? String(Update.errorString()) : s_update_error);
    }
}

static void handle_update_upload() {
    HTTPUpload &up = s_server.upload();
    if (up.status == UPLOAD_FILE_START) {
        s_update_error = "";
        s_update_denied = false;
        s_update_forbidden = false;
        // Check origin and credentials before touching flash; the rest of the upload is then discarded.
        if (!same_origin()) {
            s_update_error = "cross-site request blocked";
            s_update_forbidden = true;
            log_w("Blocked cross-site firmware upload");
            return;
        }
        s_update_denied = !authorized();
        if (s_update_denied) {
            s_update_error = "unauthorized";
            return;
        }
        if (updater::busy() || updater::reboot_due()) {
            s_update_error = "an update is already running";
            return;
        }
        log_i("Firmware upload: %s", up.filename.c_str());
        g_engine.set_suspended(true);  // no RF while flashing
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
            s_update_error = Update.errorString();
    } else if (s_update_forbidden) {
        return;  // discard the rest of a blocked upload
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (s_update_error.isEmpty() && Update.write(up.buf, up.currentSize) != up.currentSize)
            s_update_error = Update.errorString();
    } else if (up.status == UPLOAD_FILE_END) {
        if (s_update_error.isEmpty() && !Update.end(true))
            s_update_error = Update.errorString();
        log_i("Firmware upload %s (%u bytes)", s_update_error.isEmpty() ? "complete" : "failed", up.totalSize);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (!s_update_denied && !s_update_forbidden)
            Update.abort();
        g_engine.set_suspended(false);  // resume RF
        s_update_error = "upload aborted";
    }
}

// Flash layout: every partition with its size, plus how full the ones we can measure are (the running
// firmware slot and the settings store). Fixed until the next flash, apart from the settings usage.
static void handle_flash() {
    JsonDocument doc;
    doc["flash_bytes"] = ESP.getFlashChipSize();
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    JsonArray parts = doc["partitions"].to<JsonArray>();
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
    for (; it; it = esp_partition_next(it)) {
        const esp_partition_t *p = esp_partition_get(it);
        JsonObject o = parts.add<JsonObject>();
        o["label"] = p->label;
        o["offset"] = p->address;
        o["bytes"] = p->size;
        const char *kind = "other";
        if (p->type == ESP_PARTITION_TYPE_APP) {
            kind = p == running ? "firmware_running" : p == next ? "firmware_next" : "firmware";
            if (p == running)
                o["used_bytes"] = ESP.getSketchSize();
        } else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_NVS) {
            kind = "settings";
            nvs_stats_t st;
            if (nvs_get_stats(p->label, &st) == ESP_OK && st.total_entries)
                o["used_bytes"] = (uint32_t) ((uint64_t) p->size * st.used_entries / st.total_entries);
        } else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_OTA) {
            kind = "boot_select";
        } else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_COREDUMP) {
            kind = "crash_dump";
        } else if (p->subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS || p->subtype == ESP_PARTITION_SUBTYPE_DATA_FAT ||
                   p->subtype == ESP_PARTITION_SUBTYPE_DATA_LITTLEFS) {
            kind = "files";
        }
        o["kind"] = kind;
    }
    esp_partition_iterator_release(it);
    send_json(200, doc);
}

// ---- xLights upload: the part of WLED's JSON API its WLED driver uses (see lib/net2rf_wled) ----

static net2rf::WledConfig wled_view(const AppConfig &c) {
    net2rf::WledConfig w;
    w.pixels = c.num_zones;
    w.ddp = c.ddp_enabled;
    w.e131 = c.e131_enabled;
    w.multicast = c.e131_multicast;
    w.universe = c.e131_universe;
    return w;
}

static void handle_wled_info() {
    JsonDocument doc;
    StateLock lock;
    net2rf::wled_info_json(doc.to<JsonObject>(), g_app.name, FW_VERSION, g_app.num_zones);
    send_json(200, doc);
}

static void handle_wled_get_cfg() {
    JsonDocument doc;
    {
        StateLock lock;
        net2rf::wled_cfg_json(doc.to<JsonObject>(), wled_view(g_app));
    }
    send_json(200, doc);
}

// xLights "Upload Output" / "Upload Input": the zone count follows the pixels on port 1, plus the input
// protocol. Colour order, device protocol and zone addresses are not xLights' to set and stay as they are.
static void handle_wled_post_cfg() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    AppConfig next;
    {
        StateLock lock;
        next = g_app;
        net2rf::WledConfig w = wled_view(next);
        const char *err = net2rf::wled_cfg_apply(doc.as<JsonObjectConst>(), w, MAX_ZONES);
        if (err) {
            send_error(400, err);
            return;
        }
        if (next.mode == MODE_PIXEL && w.pixels != next.num_zones) {
            // A zone that was never set up still has the address for every group: give it its own group.
            // New zones drive the protocols the existing ones share.
            uint8_t p0_all[4], p1_all[4], common = zones_common_protocols(next);
            default_address(0, p0_all);
            default_address(1, p1_all);
            for (uint8_t i = max<uint8_t>(next.num_zones, 1); i < w.pixels; i++) {
                ZoneConfig &z = next.zones[i];
                if ((!zone_uses(z, 0) || memcmp(z.addr, p0_all, 4) == 0) &&
                    (!zone_uses(z, 1) || memcmp(z.p1_addr, p1_all, 2) == 0))
                    config_reset_zone(next, i);
                if (i >= next.num_zones && common)
                    z.protocols = common;
            }
            next.num_zones = w.pixels;
        }
        next.ddp_enabled = w.ddp;
        next.e131_enabled = w.e131;
        next.e131_multicast = w.multicast;
        next.e131_universe = w.universe;
        next.start_channel = 1;  // xLights numbers this controller's channels from 1
        g_app = next;
        g_engine.config_changed();
    }
    config_save_app(next);
    log_i("xLights upload: %u zones", next.num_zones);
    JsonDocument out;
    out["success"] = true;
    send_json(200, out);
}

// Install a release from GitHub: {"tag": "v1.2.3", "asset": "net2rf-led-1.2.3.bin"}. The repository is the
// configured update source. Progress is reported in /api/status (device.update_job).
static void handle_update_github() {
    JsonDocument doc;
    if (!parse_body(doc))
        return;
    String repo;
    {
        StateLock lock;
        repo = g_app.update_repo;
    }
    String err;
    if (!updater::start(repo, doc["tag"] | "", doc["asset"] | "", err)) {
        send_error(err.startsWith("an update") ? 409 : 400, err);
        return;
    }
    send_ok();
}

static void handle_not_found() {
    // Captive portal: phones probe random hosts; send them to the setup page.
    if (net::ap_active()) {
        String host = s_server.hostHeader();
        if (host != net::ip().toString() && host.indexOf(g_net.hostname) < 0) {
            s_server.sendHeader("Location", String("http://") + net::ip().toString() + "/", true);
            s_server.send(302, "text/plain", "");
            return;
        }
    }
    send_error(404, "not found");
}

void begin() {
    static const char *HEADERS[] = {"Content-Type", "Origin"};  // Authorization is always collected
    s_server.collectHeaders(HEADERS, 2);
    // Open: the page itself and read-only monitoring.
    s_server.on("/", HTTP_GET, handle_index);
    s_server.on("/api/status", HTTP_GET, handle_status);
    s_server.on("/api/stats", HTTP_GET, handle_stats);
    s_server.on("/api/flash", HTTP_GET, handle_flash);
    // ---- Tools page ----
    s_server.on("/api/tools", HTTP_GET, []() {  // transmit log + input channels (live view)
        JsonDocument doc;
        g_engine.tools_json(doc.to<JsonObject>());
        send_json(200, doc);
    });
    // Receiver mode raw capture: ?id=N -> {pulses: [+mark, -space, ...] (us), ...}; add &format=ook for rtl_433's
    // pulse-data text (rtl_433 -r capture.ook -A).
    s_server.on("/api/rx/capture", HTTP_GET, []() {
        uint32_t id = (uint32_t) s_server.arg("id").toInt();
        if (s_server.arg("format") == "ook") {
            String text;
            if (!g_engine.rx_capture_ook(id, text)) {
                send_error(404, "no such capture");
                return;
            }
            s_server.sendHeader("Content-Disposition", "attachment; filename=\"net2rf-capture-" + String(id) + ".ook\"");
            s_server.send(200, "text/plain", text);
            return;
        }
        JsonDocument doc;
        if (!g_engine.rx_capture_json(id, doc.to<JsonObject>())) {
            send_error(404, "no such capture");
            return;
        }
        send_json(200, doc);
    });
    s_server.on("/api/rssi", HTTP_GET, protect([]() {  // signal strength on the device frequency, right now
        int16_t peak, avg;
        uint32_t freq;
        if (!g_engine.measure_rssi(peak, avg, freq)) {
            send_error(409, "the radio can't listen right now (off, not ready or busy)");
            return;
        }
        JsonDocument doc;
        doc["peak_dbm"] = peak;
        doc["avg_dbm"] = avg;
        doc["freq_hz"] = freq;
        send_json(200, doc);
    }));
    s_server.on("/api/identify", HTTP_POST, protect([]() {  // {"seconds": 10}; 0 stops
        JsonDocument doc;
        if (!parse_body(doc))
            return;
        int seconds = doc["seconds"] | 10;
        if (seconds < 0 || seconds > 120) {
            send_error(400, "seconds must be 0..120");
            return;
        }
        panel::identify((uint16_t) seconds);
        send_ok();
    }));
    s_server.on("/api/net/check", HTTP_POST, protect([]() {
        if (!updater::net_check_start()) {
            send_error(409, "a check or an update is already running");
            return;
        }
        send_ok();
    }));
    s_server.on("/api/net/check", HTTP_GET, protect([]() {
        JsonDocument doc;
        updater::net_check_json(doc.to<JsonObject>());
        send_json(200, doc);
    }));
    s_server.on("/json/info", HTTP_GET, handle_wled_info);
    s_server.on("/json/cfg", HTTP_GET, handle_wled_get_cfg);
    s_server.on("/json/cfg", HTTP_POST, protect(handle_wled_post_cfg));
    s_server.on("/api/i2c/scan", HTTP_GET, []() {  // diagnostics: what answers on the OLED bus
        JsonDocument doc;
        panel::i2c_scan_json(doc.to<JsonObject>());
        send_json(200, doc);
    });
    s_server.on("/api/discover", HTTP_GET, []() {
        JsonDocument doc;
        net::discovery_json(doc.to<JsonObject>());
        send_json(200, doc);
    });
    s_server.on("/api/discover", HTTP_POST, same_origin_only([]() {
                    net::request_discovery();
                    send_ok();
                }));
    // Protected when an admin password is set.
    s_server.on("/api/config", HTTP_GET, protect(handle_get_config));
    s_server.on("/api/config", HTTP_POST, protect(handle_post_config));
    s_server.on("/api/network", HTTP_POST, protect(handle_post_network));
    s_server.on("/api/auth", HTTP_POST, protect(handle_auth));
    s_server.on("/api/test", HTTP_POST, protect(handle_test));
    s_server.on("/api/send", HTTP_POST, protect(handle_send));
    s_server.on("/api/all-off", HTTP_POST, protect(handle_all_off));
    s_server.on("/api/display", HTTP_POST, protect([]() {
                    JsonDocument doc;
                    if (!parse_body(doc))
                        return;
                    if (!doc["type"].isNull()) {
                        String type = doc["type"] | "";
                        if (type != "ssd1306" && type != "sh1106") {
                            send_error(400, "type must be ssd1306 or sh1106");
                            return;
                        }
                        panel::set_display_type(type == "sh1106" ? panel::DISPLAY_SH1106 : panel::DISPLAY_SSD1306);
                    }
                    if (!doc["sleep_min"].isNull()) {  // minutes without a USER press before the OLED sleeps; 0 = never
                        int minutes = doc["sleep_min"] | -1;
                        if (minutes < 0 || minutes > 240) {
                            send_error(400, "sleep_min must be 0..240");
                            return;
                        }
                        AppConfig saved;
                        {
                            StateLock lock;
                            g_app.display_sleep = minutes == 0 ? DISPLAY_SLEEP_NEVER : (uint8_t) minutes;
                            saved = g_app;
                        }
                        config_save_app(saved);
                    }
                    if (doc["wake"] | false)
                        panel::wake();
                    send_ok();
                }));
    s_server.on("/api/raw", HTTP_POST, protect(handle_raw));
    s_server.on("/api/output", HTTP_POST, protect(handle_output));
    s_server.on("/api/radio", HTTP_POST, protect(handle_radio));
    s_server.on("/api/stats/reset", HTTP_POST, protect([]() {
                    g_engine.reset_stats();
                    send_ok();
                }));
    s_server.on("/api/wifi/scan", HTTP_POST, protect([]() {
                    net::start_scan();
                    send_ok();
                }));
    s_server.on("/api/wifi/scan", HTTP_GET, protect([]() {
                    JsonDocument doc;
                    net::scan_json(doc.to<JsonObject>());
                    send_json(200, doc);
                }));
    s_server.on("/api/export", HTTP_GET, protect(handle_export));
    s_server.on("/api/import", HTTP_POST, protect(handle_import));
    s_server.on("/api/reboot", HTTP_POST, protect([]() {
                    schedule_reboot();
                    send_ok(true);
                }));
    s_server.on("/api/factory-reset", HTTP_POST, protect([]() {
                    config_factory_reset();
                    schedule_reboot();
                    send_ok(true);
                }));
    // Upload handler checks credentials itself before writing flash.
    s_server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
    s_server.on("/api/update/github", HTTP_POST, protect(handle_update_github));
    s_server.on("/api/update/check", HTTP_POST, protect([]() {
                    updater::check_now();
                    send_ok();
                }));
    s_server.onNotFound(handle_not_found);
    s_server.begin();
}

void loop() {
    s_server.handleClient();
    if (updater::reboot_due() && !s_reboot_at)
        schedule_reboot(2500);  // let the UI read the result first
    if (s_reboot_at && (int32_t) (millis() - s_reboot_at) >= 0) {
        log_i("Rebooting");
        delay(100);
        ESP.restart();
    }
}

}  // namespace web
