#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Network manager (WLED-style):
//  * Ethernet (LAN8720) is preferred whenever it has a link.
//  * Wi-Fi station is used when Ethernet has no link and an SSID is configured.
//  * The configured static IP (or DHCP) applies to whichever interface is active.
//  * A fallback access point + captive portal opens when nothing has connected for 15 s
//    ("no_connection"), or always / never, per settings.
namespace net {

void begin();
void loop();  // call from the Arduino loop

bool connected();                 // Ethernet or Wi-Fi station has an IP
bool eth_up();                    // Ethernet has link + IP
bool wifi_up();                   // Wi-Fi station has an IP
const char *active_interface();  // "ethernet", "wifi", "ap", or "none"
IPAddress ip();                   // primary IP (station/ethernet, else AP)
bool ap_active();
String ap_ssid();
void status_json(JsonObject out);

void start_scan();              // async Wi-Fi scan for the settings page
void scan_json(JsonObject out);  // {"running":bool,"networks":[...]}

// Controller discovery. Every controller advertises _net2rf._tcp over mDNS (browsed every 60 s, results fed
// to peers.cpp, which also listens for the UDP heartbeat). Of the controllers that can see each other, the
// one with the lowest ID (last two MAC bytes) also answers the shared alias net2rf.local, so that name
// always reaches one controller, which lists the rest.
static const char *const SHARED_ALIAS = "net2rf";
void request_discovery();             // refresh now (otherwise every 60 s)
void discovery_json(JsonObject out);  // {"running", "alias", "alias_claimed", "controllers": peers::json()}

}  // namespace net
