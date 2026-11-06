#include "amp.h"

#include <Arduino.h>

#include "config.h"
#include "pins.h"

namespace amp {

static AmpConfig s_cfg{};
static volatile bool s_asleep = false;       // on, but powered down by the idle timer
static volatile bool s_powered = false;      // what the pin was last set to
static volatile uint32_t s_last_tx_ms = 0;

static bool wanted() {
    return s_cfg.on && !s_asleep && (!s_cfg.follow_output || g_app.output_enabled);
}

static void drive() {
    if (!s_cfg.fitted)
        return;
    s_powered = wanted();
    pinMode(pins::AMP_EN, OUTPUT);
    digitalWrite(pins::AMP_EN, s_powered != (s_cfg.active_low != 0) ? HIGH : LOW);
}

static void wake() {
    s_asleep = false;
    s_last_tx_ms = millis();
}

void begin() {
    config_load_amp(s_cfg);
    wake();
    drive();
    if (s_cfg.fitted)
        log_i("Amp switch on IO%d (%s): amp %s", pins::AMP_EN, s_cfg.active_low ? "active low" : "active high",
              s_cfg.on ? "on" : "off");
}

void loop() {
    if (!s_cfg.fitted)
        return;
    if (s_cfg.on && s_cfg.idle_min && !s_asleep && millis() - s_last_tx_ms >= s_cfg.idle_min * 60000UL) {
        s_asleep = true;
        log_i("Amp idle for %u min: powering down", s_cfg.idle_min);
    }
    if (wanted() != s_powered) {
        drive();
        log_i("Amp %s", s_powered ? "powered up" : "powered down");
    }
}

void before_tx() {
    s_last_tx_ms = millis();
    if (s_powered || !s_cfg.fitted)
        return;
    s_asleep = false;
    drive();
    if (s_powered) {
        log_i("Amp powered up for a transmission");
        delay(WARM_MS);
    }
}

bool fitted() { return s_cfg.fitted; }
bool owns_led_pin() { return s_cfg.fitted && pins::AMP_EN == pins::STATUS_LED; }
bool on() { return s_cfg.fitted && s_cfg.on; }
bool holds_tx() { return s_cfg.fitted && !s_cfg.on; }

void set_on(bool on) {
    if (!s_cfg.fitted || (s_cfg.on != 0) == on)
        return;
    s_cfg.on = on;
    wake();
    drive();
    config_save_amp(s_cfg);
    log_i("Amp %s", on ? "on" : "off");
}

bool apply_json(JsonObjectConst in, String &err) {
    for (const char *k : {"fitted", "active_low", "on", "follow_output"})
        if (!in[k].isNull() && !in[k].is<bool>()) {
            err = String(k) + " must be true or false";
            return false;
        }
    if (!in["idle_min"].isNull() && !(in["idle_min"].is<int>() && in["idle_min"].as<int>() >= 0 &&
                                      in["idle_min"].as<int>() <= AMP_IDLE_MAX_MIN)) {
        err = String("idle_min must be 0-") + AMP_IDLE_MAX_MIN;
        return false;
    }
    AmpConfig c = s_cfg;
    if (in["idle_min"].is<int>())
        c.idle_min = in["idle_min"].as<int>();
    if (in["fitted"].is<bool>())
        c.fitted = in["fitted"].as<bool>();
    if (in["active_low"].is<bool>())
        c.active_low = in["active_low"].as<bool>();
    if (in["on"].is<bool>())
        c.on = in["on"].as<bool>();
    if (in["follow_output"].is<bool>())
        c.follow_output = in["follow_output"].as<bool>();
    bool released = s_cfg.fitted && !c.fitted;
    s_cfg = c;
    config_save_amp(s_cfg);
    wake();
    if (released)
        pinMode(pins::AMP_EN, OUTPUT);  // back to the status LED (the panel drives it again)
    drive();
    log_i("Amp switch %s, %s, amp %s%s", c.fitted ? "fitted" : "not fitted",
          c.active_low ? "active low" : "active high", c.on ? "on" : "off",
          c.follow_output ? ", follows RF output" : "");
    return true;
}

void json(JsonObject o) {
    o["fitted"] = (bool) s_cfg.fitted;
    o["active_low"] = (bool) s_cfg.active_low;
    o["on"] = s_cfg.fitted ? (bool) s_cfg.on : false;
    o["idle_min"] = s_cfg.idle_min;
    o["follow_output"] = (bool) s_cfg.follow_output;
    o["asleep"] = s_cfg.fitted && s_asleep;
    o["powered"] = s_cfg.fitted && s_powered;
    o["holds_tx"] = holds_tx();
    o["pin"] = pins::AMP_EN;
    o["shares_status_led"] = pins::AMP_EN == pins::STATUS_LED;
}

}  // namespace amp
