#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "rf_protocol.h"

// Persistent settings (NVS). Network settings are stored separately so a network reset
// (button hold) can recover a device without losing the zone configuration.
//
// Model: one controller runs ONE input mode (pixel, DMX or vendor). Zones are the addressable units: each has a
// name, the device protocols it drives (0, 1 or both) with an address in each, and DDP channels assigned in order
// from the controller's start channel (pixel: 3 per zone, DMX: 4, vendor: 5). Only the protocols a zone is set to
// are transmitted; a zone on both sends each change twice, protocol 0 then protocol 1.

static const uint8_t MAX_ZONES = 16;
static const uint16_t MAX_CHANNELS = 512;

enum RadioType : uint8_t { RADIO_CC1101 = 0, RADIO_SX1278 = 1, NUM_RADIO_TYPES };
// PIXEL: R,G,B per zone. DMX: R,G,B,FX per zone. VENDOR: the LedGiftSupplier DMX transmitter's layout,
// 5 channels per zone: boot code (must be 85 to transmit), group code, R, G, B (protocol 1 only).
enum InputMode : uint8_t { MODE_PIXEL = 0, MODE_DMX = 1, MODE_VENDOR = 2 };
static const uint8_t VENDOR_BOOT_CODE = 85;
enum ApMode : uint8_t { AP_NO_CONNECTION = 0, AP_ALWAYS = 1, AP_NEVER = 2 };

const char *radio_type_name(uint8_t type);

struct ZoneConfig {
    uint8_t enabled;
    uint8_t addr[4];     // protocol 0 address: packet bytes 0-3
    uint8_t protocols;   // rfproto::PROTOCOL_BIT set: 1 = protocol 0, 2 = protocol 1, 3 = both. 0 only in settings
                         // saved before zones had their own protocols (config_load() migrates them).
    uint8_t p1_addr[2];  // protocol 1 address: packet byte 1 (group code) and byte 6
    char name[24];
};
inline bool zone_uses(const ZoneConfig &z, uint8_t protocol) { return rfproto::has_protocol(z.protocols, protocol); }
// The zone's address in `protocol`, in build_packet()'s layout.
inline const uint8_t *zone_addr(const ZoneConfig &z, uint8_t protocol) { return protocol ? z.p1_addr : z.addr; }

struct AppConfig {
    uint32_t magic;
    char name[24];         // controller name, e.g. "Front Yard"
    // Devices / input mapping
    uint8_t protocol;      // default protocol (new zones, channel meter): 0 = Shenzen New Dody (433.889, 10-colour
                           // palette), 1 = LedGiftSupplier (433.920, RGB). Each zone has its own: ZoneConfig::protocols.
    uint8_t mode;          // InputMode
    uint8_t color_order;   // rfproto::ColorOrder (pixel mode)
    uint8_t num_zones;
    uint16_t start_channel;    // 1-based DDP channel of zone 1
    uint16_t ddp_port;      // E1.31 always uses UDP 5568
    uint16_t input_timeout_s;  // blank devices after this long without DDP (0 = hold forever)
    // Radio / output
    uint8_t radio_type;  // RadioType (change requires reboot)
    int8_t tx_power;     // dBm, clamped to the radio's range
    uint32_t freq[rfproto::NUM_PROTOCOLS];
    uint8_t repeats;        // frames per update
    uint8_t off_threshold;  // protocol 0: max(R,G,B) below this = off
    uint16_t refresh_ms;    // resend unchanged state (0 = never)
    uint8_t tx_jitter_ms;   // random delay before each transmission (overlapping controllers)
    uint8_t output_enabled; // global RF kill switch: 0 = receive/consume input but never transmit
    uint8_t ddp_enabled;
    uint8_t e131_enabled;   // E1.31 (sACN): one universe mapped onto channels 1-512
    uint8_t e131_multicast; // also join the universe's multicast group
    uint16_t e131_universe;
    ZoneConfig zones[MAX_ZONES];
    // ---- added after the first release: keep new fields at the end (config_load() accepts the shorter
    // layout saved by older firmware and fills these with defaults) ----
    uint8_t lbt_enabled;    // listen before transmit: wait for a clear channel (other controllers, key fobs)
    int8_t lbt_threshold;   // dBm; a channel louder than this counts as busy
    uint8_t radio_off;      // radio chip powered down: nothing is transmitted until it is switched back on
    char update_repo[64];   // GitHub "owner/name" whose releases the firmware update checks and installs
    uint8_t update_check_off;     // 1 = don't look for new releases automatically (0 = on, so older records opt in)
    uint16_t update_check_hours;  // how often the automatic check runs
    uint8_t base_layer;     // protocol 0: the all-groups zone is a base layer under the other zones
    uint8_t display_sleep;  // OLED sleep after this many minutes without a button press: 0 = default, 255 = never
    uint8_t receiver;       // 1 = receiver mode: listen on 433 MHz and show what the devices are told; never transmits
    uint8_t rx_profile;     // receiver mode: RxProfile
};
// Receiver mode tuning (both radios): normal, near (gain capped: a transmitter within a few metres), wide band.
enum RxProfile : uint8_t { RX_PROFILE_NORMAL = 0, RX_PROFILE_NEAR = 1, RX_PROFILE_WIDE = 2, NUM_RX_PROFILES };
static const char *const RX_PROFILE_NAMES[NUM_RX_PROFILES] = {"normal", "near", "wide"};
static const uint8_t DISPLAY_SLEEP_DEFAULT_MIN = 10;
static const uint8_t DISPLAY_SLEEP_NEVER = 255;
// Minutes until the OLED sleeps (0 = never).
inline uint8_t display_sleep_minutes(const AppConfig &c) {
    return c.display_sleep == 0 ? DISPLAY_SLEEP_DEFAULT_MIN : c.display_sleep == DISPLAY_SLEEP_NEVER ? 0 : c.display_sleep;
}
static const uint16_t UPDATE_CHECK_DEFAULT_HOURS = 12;
// True when transmissions may be queued (call with StateLock held).
inline bool tx_allowed(const AppConfig &c) { return c.output_enabled && !c.radio_off && !c.receiver; }
static const int8_t LBT_DEFAULT_THRESHOLD = -75;

struct NetConfig {
    uint32_t magic;
    char hostname[33];
    uint8_t eth_enabled;
    char wifi_ssid[33];
    char wifi_pass[65];
    uint8_t dhcp;
    uint32_t ip, gateway, subnet, dns;  // IPv4, used when dhcp == 0 (applies to Ethernet and Wi-Fi)
    uint8_t ap_mode;                    // ApMode
    char ap_pass[65];
    // Optional admin password (HTTP Basic, user "admin"). Stored as SHA-256(salt || password).
    // Lives with the network settings so the network-reset button hold also recovers a lost password.
    uint8_t auth_enabled;
    uint8_t auth_salt[16];
    uint8_t auth_hash[32];
};

extern AppConfig g_app;
extern NetConfig g_net;

void config_load();
// Flash writes take tens of ms: callers copy the struct under StateLock, then save the copy after releasing
// it, so DDP input and the engine are never blocked on flash.
void config_save_app(const AppConfig &c);
void config_save_net(const NetConfig &c);
void config_defaults_app(AppConfig &c);
void config_defaults_net(NetConfig &c);
// Zone `index` back to its default name and addresses (zone 0 = every group, zone N = group N); protocols kept.
void config_reset_zone(AppConfig &c, uint8_t index);
void config_factory_reset();  // wipes everything
void config_network_reset();  // network settings back to defaults (DHCP, Ethernet, AP fallback); zones kept

// JSON views (web API, export/import). *_from_json return false and fill `err` on invalid input.
void app_to_json(const AppConfig &c, JsonObject out);
bool app_from_json(JsonObjectConst in, AppConfig &c, String &err);
void net_to_json(const NetConfig &c, JsonObject out, bool include_secrets);
bool net_from_json(JsonObjectConst in, NetConfig &c, String &err);

// Admin password. An empty password disables authentication.
static const char *const ADMIN_USER = "admin";
static const size_t MIN_ADMIN_PASSWORD = 8;
void auth_set_password(NetConfig &c, const char *password);
bool auth_check(const NetConfig &c, const char *password);

// Channel layout
inline uint8_t zone_width(const AppConfig &c) { return c.mode == MODE_VENDOR ? 5 : c.mode == MODE_DMX ? 4 : 3; }
inline uint16_t zone_start_channel(const AppConfig &c, uint8_t zone) {
    return c.start_channel + zone * zone_width(c);
}
uint8_t address_len(uint8_t protocol);                       // 4 or 2
void default_address(uint8_t protocol, uint8_t *addr);       // 00FFFF0F / 00FF
// A zone's protocol is live: the zone is enabled, uses it, and the input mode drives it (vendor mode: protocol 1 only).
inline bool zone_live(const AppConfig &c, uint8_t zone, uint8_t protocol) {
    return c.zones[zone].enabled && zone_uses(c.zones[zone], protocol) && (protocol == 1 || c.mode != MODE_VENDOR);
}
// Any enabled zone drives `protocol`. Broadcasts (All off, blanking, test colours) only go to protocols in use.
inline bool protocol_in_use(const AppConfig &c, uint8_t protocol) {
    for (uint8_t i = 0; i < c.num_zones && i < MAX_ZONES; i++)
        if (zone_live(c, i, protocol))
            return true;
    return false;
}
// The protocols the zones use: their common set, or 0 when zones differ (set per zone).
uint8_t zones_common_protocols(const AppConfig &c);

// Hex string -> bytes. Non-hex characters (spaces, ':') are ignored; false unless exactly `len` bytes remain.
bool parse_hex(const char *str, uint8_t *out, size_t len);

String default_hostname();
String device_suffix();  // last 2 MAC bytes, e.g. "3F2A"
