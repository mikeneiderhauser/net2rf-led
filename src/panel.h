#pragma once

#include <ArduinoJson.h>

// Local front panel: optional SSD1306 OLED, optional user button (GPIO39), optional status LED (GPIO2).
//
// Button:  short press      -> next OLED page
//          hold 5 s         -> network reset (DHCP, default AP) + reboot
//          hold 15 s        -> factory reset + reboot
namespace panel {

void begin();
void loop();
bool display_present();

// OLED controller type (saved). The two look identical on I2C, so this is a user setting.
enum DisplayType : uint8_t { DISPLAY_SSD1306 = 0, DISPLAY_SH1106 = 1 };
void set_display_type(uint8_t type);
void display_json(JsonObject out);   // present, type, address, pins
void i2c_scan_json(JsonObject out);  // every responding address + idle line levels (diagnostics)

}  // namespace panel
