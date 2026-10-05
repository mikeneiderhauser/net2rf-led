// The slice of WLED's JSON API that xLights' WLED upload driver uses (pure C++ + ArduinoJson, unit tested).
// xLights reads /json/info and /json/cfg, then posts /json/cfg back with the pixel count and colour order of
// each port and the input protocol filled in. Answering those lets xLights "upload" to this controller without
// a driver of its own. Port 1's pixel count is the number of zones (pixel mode: one pixel per zone).
//
// WLED's per-port colour order is ignored: it describes how the LED chips are wired, not the order of the data
// xLights sends (that follows the model's String Type), and xLights posts GRB by default. The controller's own
// colour order setting has to match the model's String Type instead.
#pragma once

#include <ArduinoJson.h>
#include <cstdint>

namespace net2rf {

struct WledConfig {
    uint8_t pixels;       // pixels on port 1
    bool ddp;
    bool e131;
    bool multicast;
    uint16_t universe;
};

// xLights refuses builds older than 2105110 and 2112081-2203189; up to 2112080 it also always writes the
// input port, which is how we learn whether it sends DDP or E1.31.
static const int WLED_BUILD_ID = 2112080;
static const int WLED_TYPE_WS2811 = 22;
static const int WLED_PIN = 33;  // the radio data pin: reported so xLights has something to show

static const int WLED_ORDER_RGB = 1;  // reported as a fixed value; see the note above

inline void wled_info_json(JsonObject o, const char *name, const char *version, uint8_t pixels) {
    o["ver"] = version;
    o["vid"] = WLED_BUILD_ID;
    o["name"] = name;
    o["arch"] = "esp32";
    o["brand"] = "Net2RF";  // not "WLED": xLights' discovery must not list this as a WLED device
    o["product"] = "Net2RF LED";
    o["leds"]["count"] = pixels;
}

inline void wled_cfg_json(JsonObject o, const WledConfig &c) {
    JsonObject led = o["hw"]["led"].to<JsonObject>();
    led["total"] = c.pixels;
    JsonObject port = led["ins"].to<JsonArray>().add<JsonObject>();
    port["start"] = 0;
    port["len"] = c.pixels;
    port["pin"].to<JsonArray>().add(WLED_PIN);
    port["order"] = WLED_ORDER_RGB;
    port["rev"] = false;
    port["skip"] = 0;
    port["type"] = WLED_TYPE_WS2811;
    port["ref"] = false;
    JsonObject live = o["if"]["live"].to<JsonObject>();
    live["en"] = true;
    live["port"] = c.e131 && !c.ddp ? 5568 : 4048;
    live["mc"] = c.multicast;
    live["dmx"]["uni"] = c.universe;
    live["dmx"]["addr"] = 1;
}

// Applies a posted /json/cfg. Only the fields xLights sets are read; anything absent is left alone.
// Returns nullptr on success, otherwise why the upload cannot be used.
inline const char *wled_cfg_apply(JsonObjectConst in, WledConfig &c, uint8_t max_pixels) {
    JsonVariantConst ins = in["hw"]["led"]["ins"];
    if (ins.is<JsonArrayConst>()) {
        JsonArrayConst ports = ins.as<JsonArrayConst>();
        if (ports.size() > 1)
            return "this controller has one port: put every model on port 1";
        if (ports.size() == 1) {
            int len = ports[0]["len"] | 0;
            if (len < 1 || len > max_pixels)
                return "port 1 needs between 1 and 16 pixels (one per zone)";
            c.pixels = (uint8_t) len;
        }
    }
    JsonObjectConst live = in["if"]["live"];
    if (!live["port"].isNull()) {
        int port = live["port"] | 0;
        if (port == 4048) {
            c.ddp = true;
        } else if (port == 5568) {
            int uni = live["dmx"]["uni"] | 0;
            if (uni < 1 || uni > 63999)
                return "E1.31 universe out of range";
            c.e131 = true;
            c.universe = (uint16_t) uni;
            c.multicast = live["mc"] | false;
        } else {
            return "only DDP and E1.31 input are supported";
        }
    }
    return nullptr;
}

}  // namespace net2rf
