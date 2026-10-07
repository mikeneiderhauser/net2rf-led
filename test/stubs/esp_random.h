#pragma once
#include <cstdint>
inline uint32_t esp_random() { return 4; }
#include <cstddef>
inline void esp_fill_random(void *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        ((uint8_t *) buf)[i] = (uint8_t) (i * 37 + 11);
}
