#pragma once

// Network input parsers (no Arduino deps, host-testable): DDP and E1.31 (sACN) data packets.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bracelet {

enum class ParseResult { OK, MALFORMED, IGNORED };

struct InputFrame {
    uint32_t offset;     // 0-based channel offset of data[0]
    const uint8_t *data;
    size_t len;
    bool frame_end;      // last packet of a frame (DDP PUSH flag; every E1.31 packet)
};

// DDP (http://www.3waylabs.com/ddp/). Header byte 0 flags: VV.T SRQP.
inline ParseResult parse_ddp(const uint8_t *buf, size_t len, InputFrame &out) {
    const uint8_t VER_MASK = 0xC0, VER1 = 0x40, TIMECODE = 0x10, QUERY = 0x02, PUSH = 0x01, ID_DISPLAY = 1;
    if (len < 10 || (buf[0] & VER_MASK) != VER1)
        return ParseResult::MALFORMED;
    uint8_t flags = buf[0];
    uint8_t id = buf[3];
    if ((flags & QUERY) || (id != ID_DISPLAY && id != 0))
        return ParseResult::IGNORED;  // queries, config/status/control destinations
    size_t header = (flags & TIMECODE) ? 14 : 10;
    if (len < header)
        return ParseResult::MALFORMED;
    out.offset = ((uint32_t) buf[4] << 24) | ((uint32_t) buf[5] << 16) | ((uint32_t) buf[6] << 8) | buf[7];
    size_t declared = ((size_t) buf[8] << 8) | buf[9];
    out.data = &buf[header];
    out.len = declared < len - header ? declared : len - header;
    out.frame_end = flags & PUSH;
    return ParseResult::OK;
}

// E1.31 (ANSI E1.31-2016) data packet for `universe`. Slot 1 of the universe = channel offset 0.
inline ParseResult parse_e131(const uint8_t *buf, size_t len, uint16_t universe, InputFrame &out) {
    static const uint8_t ACN_ID[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
    const size_t HEADER = 126;
    if (len < HEADER || memcmp(&buf[4], ACN_ID, sizeof(ACN_ID)) != 0)
        return ParseResult::MALFORMED;
    auto be16 = [&](size_t o) { return (uint16_t) ((buf[o] << 8) | buf[o + 1]); };
    auto be32 = [&](size_t o) {
        return ((uint32_t) buf[o] << 24) | ((uint32_t) buf[o + 1] << 16) | ((uint32_t) buf[o + 2] << 8) | buf[o + 3];
    };
    uint8_t options = buf[112];
    // Root vector 4 = data (8 = extended: sync / universe discovery). Framing vector 2, DMP vector 2,
    // DMX start code 0. Preview data (bit 7) and stream-terminated (bit 6) packets carry no live levels.
    if (be32(18) != 0x00000004 || be32(40) != 0x00000002 || buf[117] != 0x02 || buf[125] != 0x00)
        return ParseResult::IGNORED;
    if (be16(113) != universe || (options & 0x80) || (options & 0x40))
        return ParseResult::IGNORED;
    uint16_t count = be16(123);  // property values, including the start code
    size_t slots = count > 0 ? count - 1u : 0;
    if (slots > len - HEADER)
        slots = len - HEADER;
    if (slots > 512)
        slots = 512;
    out.offset = 0;
    out.data = &buf[HEADER];
    out.len = slots;
    out.frame_end = true;
    return ParseResult::OK;
}

}  // namespace bracelet
