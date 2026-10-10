#pragma once

#include <cstdint>
#include <cstring>

// Board pin maps. Every radio shares the SPI bus, CS and data pins, so the radio type can be chosen at
// runtime. Fit only ONE radio at a time. The board is picked by the PlatformIO environment:
//   wt32-eth01    (default)                 WT32-ETH01: ESP32 + LAN8720 Ethernet
//   xiao-esp32s3  (-DNET2RF_BOARD_XIAO_S3)   Seeed XIAO ESP32-S3: Wi-Fi only, the CC1101 wiring used by
//                                           CrispyPyro/Wireless_DMX_Receiver (docs/ASSEMBLY.md)

#if defined(NET2RF_BOARD_XIAO_S3)

#define NET2RF_BOARD_ID "xiao-esp32s3"
#define NET2RF_BOARD_NAME "XIAO ESP32-S3"
// Release assets: net2rf-led-xiao-esp32s3-<version>.bin (the WT32-ETH01's carry no board tag).
#define NET2RF_HAS_ETHERNET 0
// Radio a fresh or reset controller starts with (RadioType): this wiring is the CC1101 one.
#define NET2RF_DEFAULT_RADIO 0

namespace pins {

// XIAO pad -> GPIO: D0 1, D1 2, D2 3, D3 4, D4 5, D5 6, D6 43, D7 44, D8 7, D9 8, D10 9.

// Radio SPI bus (CC1101 / SX1278)
constexpr int SPI_SCK = 7;    // D8
constexpr int SPI_MOSI = 9;   // D10
constexpr int SPI_MISO = 8;   // D9
constexpr int RADIO_CS = 4;   // D3

// OOK data in and out: CC1101 GDO0 or SX1278 DIO2. (CC1101 GDO2 on D5 is not needed: GDO0 carries the
// received signal too.)
constexpr int RADIO_DATA = 5;   // D4
constexpr int RADIO_RESET = 1;  // D0, SX1278 (Ra-02) reset; unused by the other radios

// SSD1306 128x64 OLED (optional). D6/D7 are UART0's pins, free because the console is on native USB.
constexpr int I2C_SDA = 43;  // D6
constexpr int I2C_SCL = 44;  // D7
constexpr uint8_t OLED_ADDR = 0x3C;

// User button: the board's BOOT button (GPIO0, pulled up on the board). Active low. Holding it while the board
// resets enters the USB bootloader, as always.
constexpr int USER_BUTTON = 0;
// Status LED: the board's orange user LED, wired to 3V3 (on when the pin is low).
constexpr int STATUS_LED = 21;
constexpr bool STATUS_LED_ACTIVE_LOW = true;

}  // namespace pins

#else  // WT32-ETH01

#define NET2RF_BOARD_ID "wt32-eth01"
#define NET2RF_BOARD_NAME "WT32-ETH01"
#define NET2RF_HAS_ETHERNET 1
// Radio a fresh or reset controller starts with (RadioType): the carrier board has an Ra-02 (SX1278) soldered on.
#define NET2RF_DEFAULT_RADIO 1

namespace pins {

// LAN8720 Ethernet (fixed on the WT32-ETH01). GPIO0 carries the 50 MHz RMII clock.
constexpr int ETH_ADDR = 1;
constexpr int ETH_MDC_PIN = 23;
constexpr int ETH_MDIO_PIN = 18;
constexpr int ETH_POWER_PIN = 16;

// Radio SPI bus (CC1101 / SX1278)
constexpr int SPI_SCK = 4;
constexpr int SPI_MOSI = 14;
constexpr int SPI_MISO = 35;  // input-only
constexpr int RADIO_CS = 15;  // strapping pin: wants to be high at reset, which the chip-select pull-up gives it

// OOK data in and out: CC1101 GDO0 or SX1278 DIO2
constexpr int RADIO_DATA = 33;
// SX1278 (Ra-02) reset; unused by the other radios
constexpr int RADIO_RESET = 32;

// SSD1306 128x64 OLED (optional). Not IO2: pull-ups there break serial flashing.
constexpr int I2C_SDA = 17;
constexpr int I2C_SCL = 5;
constexpr uint8_t OLED_ADDR = 0x3C;

// User button (optional): input-only pin, needs an external 10k pull-up to 3V3. Active low.
constexpr int USER_BUTTON = 39;
// Status LED (optional): LED + resistor to GND only.
constexpr int STATUS_LED = 2;
constexpr bool STATUS_LED_ACTIVE_LOW = false;

}  // namespace pins

#endif

// True when a release file name is firmware for this board: the WT32-ETH01's files carry no board tag
// (net2rf-led-1.2.3.bin), every other board's do (net2rf-led-xiao-esp32s3-1.2.3.bin).
inline bool asset_for_this_board(const char *name) {
    static const char *const OTHER_BOARD_TAGS[] = {"-xiao-esp32s3-"};
    if (strcmp(NET2RF_BOARD_ID, "wt32-eth01") != 0)
        return strstr(name, "-" NET2RF_BOARD_ID "-") != nullptr;
    for (const char *tag : OTHER_BOARD_TAGS)
        if (strstr(name, tag))
            return false;
    return true;
}
