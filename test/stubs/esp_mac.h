#pragma once
#include <cstdint>
#include <cstring>
#define ESP_MAC_ETH 3
#define ESP_MAC_WIFI_STA 0
inline int esp_read_mac(uint8_t *mac, int) {
    static const uint8_t M[6] = {0xA8, 0x03, 0x2A, 0x11, 0x3F, 0x2A};
    memcpy(mac, M, 6);
    return 0;
}
