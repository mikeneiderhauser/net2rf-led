#pragma once

#include <ArduinoJson.h>

// Local front panel: optional 0.96" 128x64 OLED (SSD1306 or SSD1315), optional user button (GPIO39), optional status LED (GPIO2).
//
// Button:  short press      -> next OLED page
//          hold 5 s         -> network reset (DHCP, default AP) + reboot
//          hold 15 s        -> factory reset + reboot
namespace panel {

void begin();
void loop();
bool display_present();

// OLED controller type (saved). The two look identical on I2C, so this is a user setting.
void display_json(JsonObject out);   // present, address, pins, sleep state
void identify(uint16_t seconds);     // blink the LED and flash the OLED for this long (0 = stop)
bool identifying();
void wake();                         // switch a sleeping OLED back on (as a USER press does)
void i2c_scan_json(JsonObject out);  // every responding address + idle line levels (diagnostics)

}  // namespace panel
