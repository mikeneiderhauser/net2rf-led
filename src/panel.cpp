#include "panel.h"
#include "updater.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SH1106Wire.h>
#include <SSD1306Wire.h>
#include <Wire.h>

#include "config.h"
#include "engine.h"
#include "net.h"
#include "pins.h"

namespace panel {

// The OLED is optional: if it is missing, fails to initialise, or stops answering, the panel
// quietly drops it, keeps probing every few seconds, and everything else keeps running.
static OLEDDisplay *s_oled = nullptr;
static uint8_t s_oled_addr = 0;
static uint8_t s_oled_failures = 0;
static uint8_t s_display_type = DISPLAY_SSD1306;
static bool s_bus_started = false;
static bool s_pins_swapped = false;  // SDA/SCL wired the wrong way round: probing alternates orientation
static uint32_t s_next_probe_ms = 0;
static uint8_t s_page = 0;

static const uint8_t OLED_ADDRS[] = {0x3C, 0x3D};  // 0x78 / 0x7A in "8-bit" module markings
static const uint32_t I2C_HZ = 400000;              // the library default (700 kHz) is too fast for jumper wires
static const uint32_t PROBE_INTERVAL_MS = 5000;
static const char *const NVS_NS = "rfb";

// Pages: Status, Input, then one Zones page per ZONES_PER_PAGE enabled zones.
// Receiver mode: Status, Receiver, then one Heard page per ZONES_PER_PAGE groups heard on air.
enum PageKind : uint8_t { PAGE_STATUS, PAGE_INPUT, PAGE_ZONES, PAGE_RECEIVER, PAGE_HEARD };
struct Page {
    PageKind kind;
    uint8_t first;  // index into the list of enabled zones (PAGE_ZONES)
};
static const uint8_t ZONES_PER_PAGE = 4;
static const uint8_t MAX_PAGES = 2 + (rfproto::RxTracker::MAX_ZONES + ZONES_PER_PAGE - 1) / ZONES_PER_PAGE;
static_assert(rfproto::RxTracker::MAX_ZONES >= MAX_ZONES, "MAX_PAGES must cover the controller's zone pages too");

static uint8_t build_pages(Page *pages) {
    uint8_t enabled = 0;
    bool receiver;
    {
        StateLock lock;
        receiver = g_app.receiver;
        for (uint8_t i = 0; i < g_app.num_zones; i++)
            enabled += g_app.zones[i].enabled ? 1 : 0;
    }
    uint8_t n = 0;
    pages[n++] = {PAGE_STATUS, 0};
    if (receiver) {
        uint8_t heard = g_engine.rx_zone_count();
        pages[n++] = {PAGE_RECEIVER, 0};
        uint8_t heard_pages = heard ? (heard + ZONES_PER_PAGE - 1) / ZONES_PER_PAGE : 1;
        for (uint8_t k = 0; k < heard_pages; k++)
            pages[n++] = {PAGE_HEARD, (uint8_t) (k * ZONES_PER_PAGE)};
        return n;
    }
    pages[n++] = {PAGE_INPUT, 0};
    uint8_t zone_pages = enabled ? (enabled + ZONES_PER_PAGE - 1) / ZONES_PER_PAGE : 1;
    for (uint8_t k = 0; k < zone_pages; k++)
        pages[n++] = {PAGE_ZONES, (uint8_t) (k * ZONES_PER_PAGE)};
    return n;
}

static uint8_t num_pages() {
    Page pages[MAX_PAGES];
    return build_pages(pages);
}
static uint32_t s_last_draw = 0, s_last_auto_page = 0;

// Button state. The button is only "armed" if its pin reads released (pulled up) at boot, so an
// unfitted, floating input can never trigger a reset.
static bool s_button_armed = false;
static bool s_pressed = false;
static uint32_t s_press_start = 0, s_last_change = 0;

// Display sleep: the OLED switches off after a while without a USER press. It wakes on the next press (which
// only wakes: it doesn't change the page), from the web UI / API, after a reboot, and by itself when there is
// something to read: the network address changes, the setup hotspot starts or stops, or the radio fails.
// Controllers without a button rely on those.
static bool s_asleep = false;
static bool s_wake_press = false;
static uint32_t s_last_activity = 0;

// Identify (Tools page): blink the LED fast and flash "THIS ONE" on the OLED, to tell controllers apart.
static uint32_t s_identify_until = 0;

static void wake_display(uint32_t now) {
    s_last_activity = now;
    if (!s_asleep)
        return;
    s_asleep = false;
    if (s_oled)
        s_oled->displayOn();
    s_last_draw = 0;
}

static const uint32_t HOLD_NET_RESET_MS = 5000;
static const uint32_t HOLD_FACTORY_MS = 15000;

bool display_present() { return s_oled != nullptr; }

// ---------------------------------------------------------------------------------------------
// Display detection
// ---------------------------------------------------------------------------------------------

static bool bus_start() {
    if (s_bus_started)
        return true;
    int sda = s_pins_swapped ? pins::I2C_SCL : pins::I2C_SDA;
    int scl = s_pins_swapped ? pins::I2C_SDA : pins::I2C_SCL;
    if (!Wire.begin(sda, scl, I2C_HZ)) {
        log_w("I2C init failed");
        return false;
    }
    Wire.setTimeOut(20);  // a missing display / pull-ups must never stall the main loop
    s_bus_started = true;
    return true;
}

static bool i2c_responds(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static void drop_display(const char *why) {
    if (!s_oled)
        return;
    log_w("OLED %s, disabling display (will keep probing)", why);
    delete s_oled;
    s_oled = nullptr;
    s_oled_addr = 0;
    s_next_probe_ms = millis() + PROBE_INTERVAL_MS;
}

// Looks for a display at 0x3C / 0x3D and initialises it. Cheap when nothing is there.
static void probe_display() {
    if (s_oled || !bus_start())
        return;
    for (uint8_t addr : OLED_ADDRS) {
        if (!i2c_responds(addr))
            continue;
        // sda/scl = -1: we own the bus, the library must not re-begin it at its own (too fast) clock.
        if (s_display_type == DISPLAY_SH1106)
            s_oled = new SH1106Wire(addr, -1, -1, GEOMETRY_128_64, I2C_ONE, I2C_HZ);
        else
            s_oled = new SSD1306Wire(addr, -1, -1, GEOMETRY_128_64, I2C_ONE, I2C_HZ);
        if (!s_oled->init()) {
            log_w("OLED at 0x%02X did not initialise", addr);
            delete s_oled;
            s_oled = nullptr;
            continue;
        }
        s_oled_addr = addr;
        s_oled_failures = 0;
        s_asleep = false;  // a freshly initialised display is on
        s_last_activity = millis();
        s_oled->flipScreenVertically();
        s_oled->setFont(ArialMT_Plain_10);
        s_oled->clear();
        s_oled->drawString(0, 0, "Net2RF LED");
        s_oled->drawString(0, 14, "v" FW_VERSION);
    s_oled->drawString(0, 28, NET2RF_BOARD_NAME);
        s_oled->display();
        s_last_draw = millis();  // leave the splash up briefly
        log_i("OLED (%s) at 0x%02X%s", s_display_type == DISPLAY_SH1106 ? "SH1106" : "SSD1306", addr,
              s_pins_swapped ? " (SDA/SCL wired swapped: working anyway)" : "");
        return;
    }
    // Nothing answered: try the other SDA/SCL orientation on the next probe (a common wiring slip).
    Wire.end();
    s_bus_started = false;
    s_pins_swapped = !s_pins_swapped;
}

// Returns false (and drops the display) if it has stopped answering.
static bool oled_ok() {
    if (!s_oled)
        return false;
    if (i2c_responds(s_oled_addr)) {
        s_oled_failures = 0;
        return true;
    }
    if (++s_oled_failures >= 3)
        drop_display("stopped responding");
    return false;
}

void set_display_type(uint8_t type) {
    s_display_type = type == DISPLAY_SH1106 ? DISPLAY_SH1106 : DISPLAY_SSD1306;
    Preferences p;
    p.begin(NVS_NS, false);
    p.putUChar("disp", s_display_type);
    p.end();
    drop_display("type changed");
    s_next_probe_ms = 0;  // re-probe with the new driver right away
}

void display_json(JsonObject o) {
    o["present"] = s_oled != nullptr;
    o["type"] = s_display_type == DISPLAY_SH1106 ? "sh1106" : "ssd1306";
    if (s_oled) {
        char a[5];
        snprintf(a, sizeof(a), "0x%02X", s_oled_addr);
        o["address"] = a;
    }
    o["sda_pin"] = s_pins_swapped ? pins::I2C_SCL : pins::I2C_SDA;
    o["scl_pin"] = s_pins_swapped ? pins::I2C_SDA : pins::I2C_SCL;
    o["pins_swapped"] = s_pins_swapped && s_oled != nullptr;
    o["asleep"] = s_asleep && s_oled != nullptr;
    o["sleep_min"] = display_sleep_minutes(g_app);
    o["button"] = s_button_armed;  // a USER button is fitted (it also wakes the display)
}

void wake() { wake_display(millis()); }

void identify(uint16_t seconds) {
    s_identify_until = seconds ? (millis() + (uint32_t) seconds * 1000UL) | 1 : 0;
    if (!seconds && s_oled)
        s_oled->normalDisplay();
}
bool identifying() { return s_identify_until != 0; }

void i2c_scan_json(JsonObject o) {
    JsonArray found = o["devices"].to<JsonArray>();
    o["bus_ok"] = bus_start();
    if (!s_bus_started)
        return;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_responds(addr)) {
            char a[5];
            snprintf(a, sizeof(a), "0x%02X", addr);
            found.add(a);
        }
    }
    // With the bus idle both lines should read high (pull-ups). Low = short, missing pull-up, or wiring.
    o["sda_high"] = digitalRead(pins::I2C_SDA) == HIGH;
    o["scl_high"] = digitalRead(pins::I2C_SCL) == HIGH;
    int sda = s_pins_swapped ? pins::I2C_SCL : pins::I2C_SDA, scl = s_pins_swapped ? pins::I2C_SDA : pins::I2C_SCL;
    o["orientation"] = String(s_pins_swapped ? "swapped" : "normal") + " (SDA=IO" + sda + ", SCL=IO" + scl + ")";
    o["sda_pin"] = pins::I2C_SDA;
    o["scl_pin"] = pins::I2C_SCL;
}

void begin() {
    pinMode(pins::STATUS_LED, OUTPUT);
    digitalWrite(pins::STATUS_LED, pins::STATUS_LED_ACTIVE_LOW ? HIGH : LOW);

    pinMode(pins::USER_BUTTON, INPUT);  // input-only pin, external pull-up required
    bool high = true;
    for (int i = 0; i < 20 && high; i++) {
        high = digitalRead(pins::USER_BUTTON) == HIGH;
        delay(2);
    }
    s_button_armed = high;
    log_i("User button %s", s_button_armed ? "armed" : "not detected (no pull-up), disabled");

    Preferences p;
    p.begin(NVS_NS, true);
    s_display_type = p.getUChar("disp", DISPLAY_SSD1306);
    p.end();

    probe_display();
    if (!s_oled)
        log_i("No OLED found at 0x3C/0x3D yet; will keep probing every %us", PROBE_INTERVAL_MS / 1000);
    s_next_probe_ms = millis() + PROBE_INTERVAL_MS;
}

static void draw_centered(const String &line1, const String &line2) {
    s_oled->clear();
    s_oled->setTextAlignment(TEXT_ALIGN_CENTER);
    s_oled->drawString(64, 18, line1);
    s_oled->drawString(64, 34, line2);
    s_oled->setTextAlignment(TEXT_ALIGN_LEFT);
    s_oled->display();
}

static void handle_button(uint32_t now) {
    if (!s_button_armed)
        return;
    bool down = digitalRead(pins::USER_BUTTON) == LOW;
    if (down != s_pressed && now - s_last_change > 30) {  // debounce
        s_last_change = now;
        s_pressed = down;
        if (down) {
            s_press_start = now;
            s_wake_press = s_asleep;
            wake_display(now);
        } else {
            uint32_t held = now - s_press_start;
            if (held >= HOLD_FACTORY_MS) {
                if (oled_ok())
                    draw_centered("Factory reset", "Rebooting...");
                config_factory_reset();
                delay(500);
                ESP.restart();
            } else if (held >= HOLD_NET_RESET_MS) {
                if (oled_ok())
                    draw_centered("Network reset", "Password cleared, rebooting");
                config_network_reset();
                delay(500);
                ESP.restart();
            } else if (!s_wake_press) {
                s_page = (s_page + 1) % num_pages();
                s_last_draw = 0;
            }
            s_last_activity = now;
        }
    }
    if (s_pressed && oled_ok()) {
        uint32_t held = now - s_press_start;
        if (held >= HOLD_NET_RESET_MS) {
            draw_centered(held >= HOLD_FACTORY_MS ? "Release: FACTORY reset" : "Release: network reset",
                          held >= HOLD_FACTORY_MS ? "(erases everything)" : "+ clears admin password");
            s_last_draw = now;
        }
    }
}

static String age_string(uint32_t ms) {
    if (ms < 60000)
        return String(ms / 1000) + "s";
    if (ms < 3600000)
        return String(ms / 60000) + "m";
    return String(ms / 3600000) + "h";
}

// Truncates `text` with ".." so it fits in `max_px`.
static String fit(const String &text, uint16_t max_px) {
    if (s_oled->getStringWidth(text) <= max_px)
        return text;
    String t = text;
    while (t.length() > 1 && s_oled->getStringWidth(t + "..") > max_px)
        t.remove(t.length() - 1);
    return t + "..";
}

// Receiver mode pages: the summary, or a list of the groups heard on air with what they were last told.
static void draw_receiver(bool list, uint8_t first, const EngineSnapshot &s) {
    static ReceiverSnapshot rx;  // large: keep it off the loop task's stack
    g_engine.rx_snapshot(rx);
    if (!list) {
        String state = !rx.supported                           ? "radio can't receive"
                       : s.radio_state == RadioState::OFF      ? "radio shut down"
                       : s.radio_state != RadioState::READY    ? "radio not ready"
                       : rx.active                             ? "listening"
                                                               : "starting";
        s_oled->drawString(0, 14, fit("Receiver: " + state, 128));
        String level = rx.active && rx.rssi_dbm > -127 ? String("  " + String(rx.rssi_dbm) + " dBm") : String();
        s_oled->drawString(0, 26, fit(String(rx.freq_hz / 1e6, 3) + " MHz" + level, 128));
        s_oled->drawString(0, 38, fit(String(rx.updates) + " cmds, " + rx.frames + " frames" +
                                          (rx.bad ? String(", " + String(rx.bad) + " bad") : String()),
                                      128));
        s_oled->drawString(0, 50, rx.last_age_ms < 0 ? String("nothing heard yet")
                                                     : String("last heard " + age_string(rx.last_age_ms) + " ago"));
        return;
    }
    if (rx.count == 0) {
        s_oled->drawString(0, 14, "Listening...");
        s_oled->drawString(0, 26, "nothing heard yet");
        return;
    }
    for (uint8_t i = first, row = 0; i < rx.count && row < ZONES_PER_PAGE; i++, row++) {
        const ReceiverSnapshot::Row &z = rx.rows[i];
        uint8_t y = 14 + row * 12;
        String name = String("P") + z.protocol + " " +
                      (z.group == rfproto::RxTracker::ALL_GROUPS ? String("All") : String("G" + String(z.group)));
        char hex[7];
        snprintf(hex, sizeof(hex), "%02X%02X%02X", z.r, z.g, z.b);
        String what = (z.label ? String(z.label) : String(hex)) + " " + age_string(z.age_ms);
        s_oled->drawString(0, y, name);
        s_oled->setTextAlignment(TEXT_ALIGN_RIGHT);
        s_oled->drawString(128, y, fit(what, 128 - 4 - s_oled->getStringWidth(name)));
        s_oled->setTextAlignment(TEXT_ALIGN_LEFT);
    }
}

static void draw_page(uint32_t now) {
    (void) now;
    EngineSnapshot s = g_engine.snapshot();
    Page pages[MAX_PAGES];
    uint8_t n_pages = build_pages(pages);
    if (s_page >= n_pages)
        s_page = 0;
    const Page &page = pages[s_page];

    String title, names[MAX_ZONES], inputs;
    {
        StateLock lock;
        title = g_app.name;
        for (uint8_t i = 0; i < g_app.num_zones; i++)
            names[i] = g_app.zones[i].name;
        if (g_app.ddp_enabled)
            inputs += "DDP " + String(g_app.ddp_port);
        if (g_app.e131_enabled)
            inputs += String(inputs.length() ? ", " : "") + "E1.31 u" + g_app.e131_universe;
    }
    s_oled->clear();
    s_oled->setFont(ArialMT_Plain_10);
    // "U" in front of the page counter: a newer firmware release is available.
    String counter = String(updater::available_version() ? "U  " : "") + String(s_page + 1) + "/" + n_pages;
    s_oled->drawString(0, 0, fit(title, 124 - s_oled->getStringWidth(counter)));
    s_oled->setTextAlignment(TEXT_ALIGN_RIGHT);
    s_oled->drawString(128, 0, counter);
    s_oled->setTextAlignment(TEXT_ALIGN_LEFT);
    s_oled->drawHorizontalLine(0, 12, 128);

    if (page.kind == PAGE_STATUS) {
        // Short interface labels for the small screen (the API keeps "ethernet" / "wifi" / "ap" / "none").
        String iface = net::active_interface();
        iface = iface == "ethernet" ? "ETH" : iface == "wifi" ? "WiFi" : iface == "ap" ? "AP" : "offline";
        s_oled->drawString(0, 14, fit(net::ip().toString() + "  " + iface, 128));
        if (net::ap_active())
            s_oled->drawString(0, 26, fit("AP " + net::ap_ssid(), 128));
        else
            s_oled->drawString(0, 26, fit(g_net.hostname, 128));
        String radio = String(s.radio_name) + ": ";
        radio += s.radio_state == RadioState::READY          ? (g_engine.receiving() ? "receiving" : "ready")
                 : s.radio_state == RadioState::NOT_DETECTED ? "NOT DETECTED"
                 : s.radio_state == RadioState::OFF          ? "OFF"
                                                              : "starting";
        s_oled->drawString(0, 38, radio);
        const char *newer = updater::available_version();
        s_oled->drawString(0, 50, newer ? fit(String("v" FW_VERSION " > ") + newer + " avail", 128) : String("v" FW_VERSION));
    } else if (page.kind == PAGE_RECEIVER || page.kind == PAGE_HEARD) {
        draw_receiver(page.kind == PAGE_HEARD, page.first, s);
    } else if (page.kind == PAGE_INPUT) {
        const char *proto = s.last_source == InputSource::E131 ? "E1.31"
                            : s.last_source == InputSource::DDP ? "DDP"
                                                                : "Input";
        String state = !s.output_enabled ? "OUTPUT OFF"
                       : s.test_active   ? "TEST MODE"
                       : !s.input_seen   ? "nothing yet"
                       : s.timed_out     ? "timed out (blank)"
                                         : "live";
        s_oled->drawString(0, 14, fit(String(proto) + ": " + state, 128));
        s_oled->drawString(0, 26, fit("fps " + String(s.fps, 1) + "  pkts " + String(s.packets), 128));
        if (s.input_seen)
            s_oled->drawString(0, 38, "last rx " + age_string(s.input_age_ms) + " ago");
        s_oled->drawString(0, 50, fit(inputs.length() ? "on " + inputs : "no input enabled!", 128));
    } else {
        // Zones: number, name, current colour (hex)
        uint8_t shown = 0, seen = 0;
        for (uint8_t i = 0; i < s.num_zones && shown < ZONES_PER_PAGE; i++) {
            if (!s.enabled[i])
                continue;
            if (seen++ < page.first)
                continue;
            uint8_t y = 14 + shown * 12;
            char hex[7];
            snprintf(hex, sizeof(hex), "%02X%02X%02X", s.r[i], s.g[i], s.b[i]);
            s_oled->drawString(0, y, String(i + 1));
            s_oled->drawString(14, y, fit(names[i], 128 - 14 - 4 - s_oled->getStringWidth(hex)));
            s_oled->setTextAlignment(TEXT_ALIGN_RIGHT);
            s_oled->drawString(128, y, hex);
            s_oled->setTextAlignment(TEXT_ALIGN_LEFT);
            shown++;
        }
        if (shown == 0)
            s_oled->drawString(0, 14, "No zones enabled");
    }
    s_oled->display();
}

static void update_led(uint32_t now) {
    EngineSnapshot s = g_engine.snapshot();
    bool on;
    if (s_identify_until)
        on = (now / 60) % 2;  // identify: a rapid flicker, unlike any status pattern
    else if (s.radio_state != RadioState::READY && s.radio_state != RadioState::OFF)
        on = (now / 150) % 2;  // fast blink: radio problem
    else if (net::ap_active() && !net::connected())
        on = (now % 1000) < 100 || ((now % 1000) > 200 && (now % 1000) < 300);  // double blink: AP mode
    else
        on = (now % 2000) < 80;  // heartbeat
    digitalWrite(pins::STATUS_LED, on != pins::STATUS_LED_ACTIVE_LOW);
}

void loop() {
    uint32_t now = millis();
    handle_button(now);
    if (s_identify_until && (int32_t) (now - s_identify_until) >= 0) {  // identify is over (with or without an OLED)
        s_identify_until = 0;
        if (s_oled)
            s_oled->normalDisplay();
        s_last_draw = 0;
    }
    update_led(now);
    if (!s_oled) {
        if ((int32_t) (now - s_next_probe_ms) >= 0) {
            probe_display();
            s_next_probe_ms = now + PROBE_INTERVAL_MS;
        }
        return;
    }
    if (!s_button_armed && now - s_last_auto_page > 5000) {  // no button: rotate pages
        s_page = (s_page + 1) % num_pages();
        s_last_auto_page = now;
        s_last_draw = 0;
    }
    if (s_identify_until) {
        {
            wake_display(now);
            if (now - s_last_draw > 350 && oled_ok()) {
                static bool inverted = false;
                inverted = !inverted;
                if (inverted)
                    s_oled->invertDisplay();
                else
                    s_oled->normalDisplay();
                draw_centered("THIS ONE", g_net.hostname);
                s_last_draw = now;
            }
            return;
        }
    }
    // Things worth waking for, checked once a second.
    static uint32_t last_check = 0, last_ip = 0;
    static bool last_ap = false, last_fault = false;
    if (now - last_check >= 1000) {
        last_check = now;
        uint32_t ip = (uint32_t) net::ip();
        bool ap = net::ap_active();
        bool fault = g_engine.snapshot().radio_state == RadioState::NOT_DETECTED;
        if (ip != last_ip || ap != last_ap || (fault && !last_fault))
            wake_display(now);
        last_ip = ip;
        last_ap = ap;
        last_fault = fault;
    }
    uint32_t sleep_ms = (uint32_t) display_sleep_minutes(g_app) * 60000UL;
    if (sleep_ms && !s_asleep && !s_pressed && now - s_last_activity >= sleep_ms) {
        s_oled->displayOff();
        s_asleep = true;
        log_i("OLED asleep");
    }
    if (!s_asleep && !s_pressed && now - s_last_draw > 500) {
        if (oled_ok())
            draw_page(now);
        s_last_draw = now;
    }
}

}  // namespace panel
