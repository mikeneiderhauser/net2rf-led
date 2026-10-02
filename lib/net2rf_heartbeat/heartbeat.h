#pragma once

// Controller heartbeat (no Arduino deps, host-testable): every controller broadcasts one of these on
// UDP HEARTBEAT_PORT every few seconds so the others can list it with its live state.
//
//   {"p":"net2rf","v":1,"id":"24DB","name":"Front Yard","host":"net2rf-24db","fw":"1.2.0",
//    "radio":"ready","input":"live","out":true,"test":false,"zones":4,"air":12,"up":86400,"hello":true}
//
// "air" = share of the last 10 s spent transmitting (%), so controllers in RF range of each other can see when
// they compete for the 433 MHz channel.
//
// "p" + "v" identify the format (anything else is ignored). The sender's IP is taken from the packet's
// source address, not from the payload. "hello" asks every receiver to answer with a beat right away.

#include <ArduinoJson.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net2rf {

static const uint16_t HEARTBEAT_PORT = 4049;
static const uint8_t HEARTBEAT_VERSION = 1;
static const size_t HEARTBEAT_MAX = 400;  // bytes on the wire

struct Heartbeat {
    char id[8];      // last two MAC bytes, e.g. "24DB"
    char name[25];   // controller name
    char host[34];   // hostname (no ".local")
    char fw[33];
    char radio[16];  // "ready" / "initializing" / "not_detected"
    char input[12];  // "live" / "idle" / "timed_out" / "none"
    bool out;        // RF output enabled
    bool test;       // test mode active
    uint8_t zones;
    uint8_t air;     // RF airtime over the last 10 s, %
    uint32_t up;     // uptime, s
    bool hello;      // sender just started: please answer now
};

namespace detail {
inline void copy_str(char *dst, size_t size, const char *src) {
    size_t i = 0;
    if (src)
        for (; src[i] && i + 1 < size; i++)
            dst[i] = (unsigned char) src[i] < 0x20 ? '_' : src[i];
    dst[i] = '\0';
}
inline bool is_id(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 6)
        return false;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}
}  // namespace detail

// Serialise into `buf`; returns the length (0 if it didn't fit).
inline size_t encode_heartbeat(const Heartbeat &h, char *buf, size_t size) {
    JsonDocument doc;
    doc["p"] = "net2rf";
    doc["v"] = HEARTBEAT_VERSION;
    doc["id"] = h.id;
    doc["name"] = h.name;
    doc["host"] = h.host;
    doc["fw"] = h.fw;
    doc["radio"] = h.radio;
    doc["input"] = h.input;
    doc["out"] = h.out;
    doc["test"] = h.test;
    doc["zones"] = h.zones;
    doc["air"] = h.air;
    doc["up"] = h.up;
    if (h.hello)
        doc["hello"] = true;
    size_t need = measureJson(doc);
    if (need + 1 > size)
        return 0;
    return serializeJson(doc, buf, size);
}

// Parse a received datagram. False for anything that isn't a net2rf heartbeat (other software on the port,
// truncated or malformed JSON, missing / bogus id). Strings are truncated to the struct's buffers.
inline bool decode_heartbeat(const uint8_t *data, size_t len, Heartbeat &out) {
    if (data == nullptr || len == 0 || len > HEARTBEAT_MAX)
        return false;
    JsonDocument doc;
    if (deserializeJson(doc, (const char *) data, len))
        return false;
    if (!doc.is<JsonObject>())
        return false;
    const char *p = doc["p"] | "";
    if (strcmp(p, "net2rf") != 0 || (doc["v"] | 0) < 1)
        return false;
    const char *id = doc["id"] | "";
    if (!detail::is_id(id))
        return false;
    memset(&out, 0, sizeof(out));
    detail::copy_str(out.id, sizeof(out.id), id);
    detail::copy_str(out.name, sizeof(out.name), doc["name"] | "");
    detail::copy_str(out.host, sizeof(out.host), doc["host"] | "");
    detail::copy_str(out.fw, sizeof(out.fw), doc["fw"] | "");
    detail::copy_str(out.radio, sizeof(out.radio), doc["radio"] | "");
    detail::copy_str(out.input, sizeof(out.input), doc["input"] | "");
    out.out = doc["out"] | false;
    out.test = doc["test"] | false;
    out.zones = doc["zones"] | 0;
    out.air = doc["air"] | 0;
    out.up = doc["up"] | 0;
    out.hello = doc["hello"] | false;
    return true;
}

}  // namespace net2rf
