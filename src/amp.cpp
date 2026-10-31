#include "amp.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace amp {

static AmpConfig s_cfg{};

static void drive() {
    if (!s_cfg.fitted)
        return;
    pinMode(pins::AMP_EN, OUTPUT);
    digitalWrite(pins::AMP_EN, (s_cfg.on != 0) != (s_cfg.active_low != 0) ? HIGH : LOW);
}

void begin() {
    config_load_amp(s_cfg);
    drive();
    if (s_cfg.fitted)
        log_i("Amp switch on IO%d (%s): amp %s", pins::AMP_EN, s_cfg.active_low ? "active low" : "active high",
              s_cfg.on ? "on" : "off");
}

bool fitted() { return s_cfg.fitted; }
bool owns_led_pin() { return s_cfg.fitted && pins::AMP_EN == pins::STATUS_LED; }
bool on() { return s_cfg.fitted && s_cfg.on; }

void set_on(bool on) {
    if (!s_cfg.fitted || (s_cfg.on != 0) == on)
        return;
    s_cfg.on = on;
    drive();
    config_save_amp(s_cfg);
    log_i("Amp %s", on ? "on" : "off");
}

bool apply_json(JsonObjectConst in, String &err) {
    for (const char *k : {"fitted", "active_low", "on"})
        if (!in[k].isNull() && !in[k].is<bool>()) {
            err = String(k) + " must be true or false";
            return false;
        }
    AmpConfig c = s_cfg;
    if (in["fitted"].is<bool>())
        c.fitted = in["fitted"].as<bool>();
    if (in["active_low"].is<bool>())
        c.active_low = in["active_low"].as<bool>();
    if (in["on"].is<bool>())
        c.on = in["on"].as<bool>();
    bool released = s_cfg.fitted && !c.fitted;
    s_cfg = c;
    config_save_amp(s_cfg);
    if (released)
        pinMode(pins::AMP_EN, OUTPUT);  // back to the status LED (the panel drives it again)
    drive();
    log_i("Amp switch %s, %s, amp %s", c.fitted ? "fitted" : "not fitted", c.active_low ? "active low" : "active high",
          c.on ? "on" : "off");
    return true;
}

void json(JsonObject o) {
    o["fitted"] = (bool) s_cfg.fitted;
    o["active_low"] = (bool) s_cfg.active_low;
    o["on"] = s_cfg.fitted ? (bool) s_cfg.on : false;
    o["pin"] = pins::AMP_EN;
    o["shares_status_led"] = pins::AMP_EN == pins::STATUS_LED;
}

}  // namespace amp
