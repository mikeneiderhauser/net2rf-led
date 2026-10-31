#pragma once

#include <ArduinoJson.h>

// Amplifier power switch: an optional output on pins::AMP_EN that powers an external RF amplifier up and down
// (e.g. through its buck's EN pin). Not fitted by default; then the pin is left to the status LED.
namespace amp {

void begin();         // after config_load(), before panel::begin()
bool fitted();
bool owns_led_pin();  // the switch is fitted on the status LED's pin: the panel must not drive the LED
bool on();
void set_on(bool on);  // ignored unless fitted; saved
// Applies the fields present ({"fitted", "active_low", "on"}) and saves them. False + err on a bad value.
bool apply_json(JsonObjectConst in, String &err);
void json(JsonObject out);

}  // namespace amp
