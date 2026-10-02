#include "net.h"

#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ETH.h>
#include <WiFi.h>
#include <mdns.h>

#include "config.h"
#include "engine.h"
#include "peers.h"
#include "pins.h"

namespace net {

static const uint32_t ETH_GRACE_MS = 5000;     // give Ethernet this long before starting Wi-Fi
static const uint32_t AP_FALLBACK_MS = 15000;  // no connection this long -> open the AP
static const uint32_t STA_RETRY_MS = 30000;
static const IPAddress AP_IP(192, 168, 4, 1);

static volatile bool s_eth_link = false, s_eth_ip = false, s_sta_ip = false;
static bool s_eth_started = false, s_sta_started = false, s_ap_started = false, s_mdns_started = false;
static uint32_t s_boot_ms = 0, s_last_connected_ms = 0, s_sta_started_ms = 0;
static DNSServer s_dns;
static String s_ap_ssid;
// Shared alias (net2rf.local): exactly one controller should answer it. Every discovery round elects the
// controller with the lowest ID among those found; the others leave the alias alone.
static bool s_alias_claimed = false;              // our delegated host record is registered
static uint32_t s_alias_ip = 0;                   // address it currently points at
static uint32_t s_alias_retry_ms = 0;             // after a failed claim, don't retry before this
static const uint32_t ALIAS_RETRY_MS = 30000;
static volatile uint8_t s_sta_reason = 0;       // last station disconnect reason (wifi_err_reason_t), 0 = none
static volatile bool s_sta_paused = false;      // station retries paused so AP clients stay connected

static volatile bool s_discovery_requested = true, s_discovery_running = false;
static const uint32_t DISCOVERY_INTERVAL_MS = 60000;

static void on_event(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:
            s_eth_link = true;
            log_i("Ethernet link up (%u Mbps)", ETH.linkSpeed());
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            s_eth_ip = true;
            log_i("Ethernet IP %s", ETH.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
        case ARDUINO_EVENT_ETH_STOP:
            s_eth_link = false;
            s_eth_ip = false;
            log_i("Ethernet down");
            break;
        case ARDUINO_EVENT_ETH_LOST_IP:
            s_eth_ip = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            s_sta_ip = true;
            s_sta_reason = 0;
            log_i("Wi-Fi IP %s", WiFi.localIP().toString().c_str());
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            s_sta_ip = false;
            if (!s_sta_paused && info.wifi_sta_disconnected.reason != WIFI_REASON_ASSOC_LEAVE) {
                s_sta_reason = info.wifi_sta_disconnected.reason;
                log_w("Wi-Fi join failed: %s (%u)", WiFi.STA.disconnectReasonName((wifi_err_reason_t) s_sta_reason),
                      s_sta_reason);
            }
            break;
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            s_sta_ip = false;
            break;
        default:
            break;
    }
}

// Station failure in words a user can act on.
static const char *reason_text(uint8_t r) {
    switch (r) {
        case 0:
            return "";
        case WIFI_REASON_NO_AP_FOUND:
            return "network not found (out of range, typo in the name, or a 5 GHz-only network; the ESP32 is 2.4 GHz only)";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_802_1X_AUTH_FAILED:
        case WIFI_REASON_MIC_FAILURE:
            return "wrong password";
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_BEACON_TIMEOUT:
            return "weak signal / network stopped answering";
        case WIFI_REASON_ASSOC_FAIL:
        case WIFI_REASON_CONNECTION_FAIL:
            return "the router refused the connection (MAC filter or client limit?)";
        default:
            return "connection failed";
    }
}

static void apply_static(NetworkInterface &iface) {
    if (g_net.dhcp)
        return;
    IPAddress dns = g_net.dns ? IPAddress(g_net.dns) : IPAddress(g_net.gateway);
    iface.config(IPAddress(g_net.ip), IPAddress(g_net.gateway), IPAddress(g_net.subnet), dns);
}

static void start_sta() {
    if (s_sta_started || g_net.wifi_ssid[0] == '\0')
        return;
    log_i("Starting Wi-Fi station for '%s'", g_net.wifi_ssid);
    WiFi.mode(s_ap_started ? WIFI_AP_STA : WIFI_STA);
    WiFi.setHostname(g_net.hostname);
    apply_static(WiFi.STA);
    WiFi.begin(g_net.wifi_ssid, g_net.wifi_pass);
    s_sta_started = true;
    s_sta_started_ms = millis();
}

static void stop_sta() {
    if (!s_sta_started)
        return;
    log_i("Stopping Wi-Fi station (Ethernet is up)");
    WiFi.disconnect(true);
    WiFi.mode(s_ap_started ? WIFI_AP : WIFI_OFF);
    s_sta_started = false;
    s_sta_ip = false;
}

static void start_ap() {
    if (s_ap_started)
        return;
    s_ap_ssid = "NET2RF-" + device_suffix();
    WiFi.mode(s_sta_started ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    const char *pass = strlen(g_net.ap_pass) >= 8 ? g_net.ap_pass : nullptr;
    WiFi.softAP(s_ap_ssid.c_str(), pass);
    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", AP_IP);  // captive portal: every name resolves to us
    s_ap_started = true;
    log_i("Access point '%s' up at %s", s_ap_ssid.c_str(), AP_IP.toString().c_str());
}

static void stop_ap() {
    if (!s_ap_started)
        return;
    s_dns.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(s_sta_started ? WIFI_STA : WIFI_OFF);
    s_ap_started = false;
    log_i("Access point closed");
}

static void update_txt() {
    String name;
    uint8_t zones;
    {
        StateLock lock;
        name = g_app.name;
        zones = g_app.num_zones;
    }
    MDNS.addServiceTxt("net2rf", "tcp", "name", name);
    MDNS.addServiceTxt("net2rf", "tcp", "fw", FW_VERSION);
    MDNS.addServiceTxt("net2rf", "tcp", "id", device_suffix());
    MDNS.addServiceTxt("net2rf", "tcp", "zones", String(zones));
}

// Background task: browse for other controllers (a query blocks for a few seconds).
static void discovery_task(void *) {
    uint32_t last = 0;
    for (;;) {
        bool due = s_discovery_requested || millis() - last > DISCOVERY_INTERVAL_MS;
        if (!s_mdns_started || !due) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        s_discovery_requested = false;
        s_discovery_running = true;
        update_txt();  // keep our own advertisement current (name / zone count may have changed)
        int n = MDNS.queryService("net2rf", "tcp");
        for (int i = 0; i < n; i++)
            peers::note_mdns(MDNS.txt(i, "id"), MDNS.txt(i, "name"), MDNS.hostname(i), MDNS.address(i).toString(),
                             MDNS.txt(i, "fw"));
        s_discovery_running = false;
        last = millis();
    }
}

void request_discovery() { s_discovery_requested = true; }

void discovery_json(JsonObject o) {
    o["running"] = (bool) s_discovery_running;
    o["alias"] = String(SHARED_ALIAS) + ".local";
    o["alias_claimed"] = s_alias_claimed || strcasecmp(g_net.hostname, SHARED_ALIAS) == 0;
    peers::json(o["controllers"].to<JsonArray>());
}

void begin() {
    s_boot_ms = millis();
    xTaskCreatePinnedToCore(discovery_task, "discovery", 4096, nullptr, 1, nullptr, 0);
    Network.onEvent(on_event);
    Network.setHostname(g_net.hostname);
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);

    if (g_net.eth_enabled) {
        s_eth_started = ETH.begin(ETH_PHY_LAN8720, pins::ETH_ADDR, pins::ETH_MDC_PIN, pins::ETH_MDIO_PIN,
                                  pins::ETH_POWER_PIN, ETH_CLOCK_GPIO0_IN);
        if (s_eth_started)
            apply_static(ETH);
        else
            log_e("Ethernet init failed");
    }
    if (g_net.ap_mode == AP_ALWAYS)
        start_ap();
    if (!s_eth_started)
        start_sta();
}

void loop() {
    uint32_t now = millis();
    bool eth_up = s_eth_link && s_eth_ip;
    bool is_connected = eth_up || s_sta_ip;

    if (is_connected)
        s_last_connected_ms = now;

    // Ethernet wins; Wi-Fi station only when Ethernet has no link.
    if (eth_up) {
        stop_sta();
    } else if (!s_eth_link && (!s_eth_started || now - s_boot_ms > ETH_GRACE_MS)) {
        start_sta();
        // While someone is using the setup AP and the station has failed to join, stop the station from retrying:
        // each attempt scans every channel, which knocks phones off the AP mid-setup.
        bool ap_in_use = s_ap_started && WiFi.softAPgetStationNum() > 0;
        if (s_sta_started && !s_sta_ip && s_sta_reason && ap_in_use && !s_sta_paused) {
            s_sta_paused = true;
            WiFi.setAutoReconnect(false);
            WiFi.disconnect(false);
            log_i("Setup AP in use: pausing Wi-Fi station retries");
        } else if (s_sta_paused && !ap_in_use) {
            s_sta_paused = false;
            WiFi.setAutoReconnect(true);
            WiFi.begin(g_net.wifi_ssid, g_net.wifi_pass);
            s_sta_started_ms = now;
        }
        // Retry a station that never connected (bad password, AP out of range...).
        if (s_sta_started && !s_sta_ip && !s_sta_paused && now - s_sta_started_ms > STA_RETRY_MS) {
            WiFi.reconnect();
            s_sta_started_ms = now;
        }
    }

    // Fallback access point
    if (g_net.ap_mode == AP_ALWAYS) {
        start_ap();
    } else if (g_net.ap_mode == AP_NEVER) {
        stop_ap();
    } else {
        uint32_t since = s_last_connected_ms ? s_last_connected_ms : s_boot_ms;
        if (!is_connected && now - since > AP_FALLBACK_MS)
            start_ap();
        else if (is_connected && s_ap_started && WiFi.softAPgetStationNum() == 0)
            stop_ap();  // keep it while someone is using it
    }
    if (s_ap_started)
        s_dns.processNextRequest();

    if (!s_mdns_started && (is_connected || s_ap_started)) {
        if (MDNS.begin(g_net.hostname)) {
            MDNS.addService("http", "tcp", 80);
            MDNS.addService("net2rf", "tcp", 80);
            MDNS.setInstanceName(g_net.hostname);
            s_mdns_started = true;
            update_txt();
        }
    }
    // Shared alias: point net2rf.local at our current address while we hold it (see the election in
    // discovery_task), unless it is already our own hostname.
    if (s_mdns_started && strcasecmp(g_net.hostname, SHARED_ALIAS) != 0) {
        uint32_t cur = (uint32_t) ip();
        bool decided = false;
        bool hold = peers::alias_should_hold(decided);
        bool want = decided && hold && cur != 0;
        if (want && (!s_alias_claimed || cur != s_alias_ip) && (int32_t) (now - s_alias_retry_ms) >= 0) {
            mdns_ip_addr_t addr{};
            addr.addr.type = ESP_IPADDR_TYPE_V4;
            addr.addr.u_addr.ip4.addr = cur;
            addr.next = nullptr;
            esp_err_t err = s_alias_claimed ? mdns_delegate_hostname_set_address(SHARED_ALIAS, &addr)
                                            : mdns_delegate_hostname_add(SHARED_ALIAS, &addr);
            if (err == ESP_OK) {
                if (!s_alias_claimed)
                    log_i("Also answering as %s.local", SHARED_ALIAS);
                s_alias_claimed = true;
                s_alias_ip = cur;
            } else {
                log_w("Could not claim %s.local (%s), retrying in %us", SHARED_ALIAS, esp_err_to_name(err),
                      ALIAS_RETRY_MS / 1000);
                s_alias_retry_ms = now + ALIAS_RETRY_MS;
            }
        } else if (!want && s_alias_claimed) {
            mdns_delegate_hostname_remove(SHARED_ALIAS);
            s_alias_claimed = false;
            s_alias_ip = 0;
            log_i("Released %s.local (%s)", SHARED_ALIAS,
                  cur ? "another controller holds it" : "no network address");
        }
    }
}

bool connected() { return (s_eth_link && s_eth_ip) || s_sta_ip; }
bool eth_up() { return s_eth_link && s_eth_ip; }
bool wifi_up() { return s_sta_ip; }

const char *active_interface() {
    if (s_eth_link && s_eth_ip)
        return "ethernet";
    if (s_sta_ip)
        return "wifi";
    if (s_ap_started)
        return "ap";
    return "none";
}

IPAddress ip() {
    if (s_eth_link && s_eth_ip)
        return ETH.localIP();
    if (s_sta_ip)
        return WiFi.localIP();
    if (s_ap_started)
        return AP_IP;
    return IPAddress();
}

bool ap_active() { return s_ap_started; }
String ap_ssid() { return s_ap_ssid; }

void status_json(JsonObject o) {
    o["interface"] = active_interface();
    o["ip"] = ip().toString();
    o["hostname"] = g_net.hostname;
    o["dhcp"] = (bool) g_net.dhcp;
    JsonObject eth = o["ethernet"].to<JsonObject>();
    eth["enabled"] = (bool) g_net.eth_enabled;
    eth["link"] = (bool) s_eth_link;
    if (s_eth_started) {
        eth["mac"] = ETH.macAddress();
        if (s_eth_link) {
            eth["speed"] = ETH.linkSpeed();
            eth["full_duplex"] = ETH.fullDuplex();
        }
        if (s_eth_ip) {
            eth["ip"] = ETH.localIP().toString();
            eth["gateway"] = ETH.gatewayIP().toString();
            eth["subnet"] = ETH.subnetMask().toString();
        }
    }
    JsonObject wifi = o["wifi"].to<JsonObject>();
    wifi["ssid"] = g_net.wifi_ssid;
    wifi["active"] = s_sta_started;
    wifi["connected"] = (bool) s_sta_ip;
    wifi["paused"] = (bool) s_sta_paused;
    if (!s_sta_ip && s_sta_reason) {
        wifi["reason_code"] = s_sta_reason;
        wifi["error"] = reason_text(s_sta_reason);
    }
    wifi["mac"] = WiFi.macAddress();
    if (s_sta_ip) {
        wifi["ip"] = WiFi.localIP().toString();
        wifi["rssi"] = WiFi.RSSI();
    }
    JsonObject ap = o["ap"].to<JsonObject>();
    ap["active"] = s_ap_started;
    if (s_ap_started) {
        ap["ssid"] = s_ap_ssid;
        ap["ip"] = AP_IP.toString();
        ap["clients"] = WiFi.softAPgetStationNum();
    }
}

void start_scan() {
    // Scanning needs the station interface; keep the AP up if it is running.
    wifi_mode_t m = WiFi.getMode();
    if (m == WIFI_OFF)
        WiFi.mode(WIFI_STA);
    else if (m == WIFI_AP)
        WiFi.mode(WIFI_AP_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
}

void scan_json(JsonObject o) {
    int16_t n = WiFi.scanComplete();
    o["running"] = n == WIFI_SCAN_RUNNING;
    JsonArray arr = o["networks"].to<JsonArray>();
    if (n <= 0)
        return;
    for (int16_t i = 0; i < n && i < 30; i++) {
        JsonObject j = arr.add<JsonObject>();
        j["ssid"] = WiFi.SSID(i);
        j["rssi"] = WiFi.RSSI(i);
        j["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
}

}  // namespace net
