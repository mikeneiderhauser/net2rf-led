// FPP's discovery ping (pure C++, unit tested). Falcon Player and xLights find controllers by sending a
// "discover" ping to UDP 32320 (multicast 239.70.80.80 and broadcast); every device answers with a ping
// describing itself. Layout from FPP's docs/ControlProtocol.txt, ping version 3 (294 bytes of data), which
// every FPP release and xLights accept.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace net2rf {

static const uint16_t FPP_PORT = 32320;
static const uint8_t FPP_MULTICAST[4] = {239, 70, 80, 80};
static const size_t FPP_PING_LEN = 7 + 294;

// FPP has no type code for this controller yet, so it reports "other system": it does not claim another
// product's type. (xLights therefore creates a discovered controller as E1.31 until a code is assigned and
// xLights knows it; see TODO.md.) The model string is the ID of our xLights controller definition
// (tools/xlights/net2rf.xcontroller), ready for xLights to look up once it does.
static const uint8_t FPP_TYPE_OTHER = 0xC0;
static const char *const FPP_MODEL_ID = "Net2RF-LED";
static const uint8_t FPP_MODE_BRIDGE = 0x01;  // takes pixel data (DDP / E1.31), does not play or sync sequences

// True for a ping packet with the "discover" subtype: the sender wants every device to answer.
inline bool fpp_is_discover(const uint8_t *buf, size_t len) {
    return len >= 9 && memcmp(buf, "FPPD", 4) == 0 && buf[4] == 0x04 && buf[8] == 0x01;
}

inline void fpp_put_string(uint8_t *field, size_t field_size, const char *text) {
    strncpy((char *) field, text ? text : "", field_size - 1);  // the field keeps its terminating NUL
}

// Writes our answer into `buf` (at least FPP_PING_LEN bytes) and returns its length.
// `version` like "0.0.4-3-gabc": the leading numbers become the major / minor version fields.
// `channels` is how many channels the controller takes; they are reported as the range "0-(n-1)".
inline size_t fpp_build_ping(uint8_t *buf, const uint8_t ip[4], const char *hostname, const char *version,
                             uint16_t channels) {
    memset(buf, 0, FPP_PING_LEN);
    memcpy(buf, "FPPD", 4);
    buf[4] = 0x04;                 // ping
    buf[5] = 294 & 0xFF;           // extra data length, low byte first
    buf[6] = 294 >> 8;
    buf[7] = 0x03;                 // ping version
    buf[8] = 0x00;                 // subtype: ping (an answer, not a discover)
    buf[9] = FPP_TYPE_OTHER;
    unsigned major = 0, minor = 0;
    const char *v = version && (*version == 'v' || *version == 'V') ? version + 1 : version;
    if (v) {
        char *end;
        major = (unsigned) strtoul(v, &end, 10);
        if (*end == '.')
            minor = (unsigned) strtoul(end + 1, nullptr, 10);
    }
    buf[10] = (major >> 8) & 0xFF;
    buf[11] = major & 0xFF;
    buf[12] = (minor >> 8) & 0xFF;
    buf[13] = minor & 0xFF;
    buf[14] = FPP_MODE_BRIDGE;
    memcpy(buf + 15, ip, 4);
    fpp_put_string(buf + 19, 65, hostname);
    fpp_put_string(buf + 84, 41, version);
    fpp_put_string(buf + 125, 41, FPP_MODEL_ID);
    char ranges[16] = "";
    if (channels)
        snprintf(ranges, sizeof(ranges), "0-%u", (unsigned) (channels - 1));
    fpp_put_string(buf + 166, 121, ranges);
    return FPP_PING_LEN;
}

}  // namespace net2rf
