#pragma once

#include <ArduinoJson.h>

// Amplifier power switch: an optional output on pins::AMP_EN that powers an external RF amplifier up and down
// (e.g. through its buck's EN pin). Not fitted by default; then the pin is left to the status LED.
//
// With idle_min set, an amp that is on powers down after that many minutes without a transmission, and powers up
// again (waiting WARM_MS) when the next one is due. That is not saved: "on" still means the amp is wanted.
namespace amp {

static const uint32_t WARM_MS = 300;  // from powering up to the first packet

void begin();         // after config_load(), before panel::begin()
void loop();          // the idle timer
void before_tx();     // from the engine task: wakes a sleeping amp
bool fitted();
bool owns_led_pin();  // the switch is fitted on the status LED's pin: the panel must not drive the LED
bool on();
void set_on(bool on);  // ignored unless fitted; saved
// Applies the fields present ({"fitted", "active_low", "on", "idle_min"}) and saves them. False + err on a bad value.
bool apply_json(JsonObjectConst in, String &err);
void json(JsonObject out);

}  // namespace amp
