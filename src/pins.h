#pragma once

// WT32-ETH01 pin map. Every radio shares the SPI bus, CS and data pins, so the radio type can be
// chosen at runtime. Fit only ONE radio at a time.
namespace pins {

// LAN8720 Ethernet (fixed on the WT32-ETH01). GPIO0 carries the 50 MHz RMII clock.
constexpr int ETH_ADDR = 1;
constexpr int ETH_MDC_PIN = 23;
constexpr int ETH_MDIO_PIN = 18;
constexpr int ETH_POWER_PIN = 16;

// Radio SPI bus (CC1101 / SX1278)
constexpr int SPI_SCK = 14;
constexpr int SPI_MOSI = 15;  // strapping pin: fine as MOSI
constexpr int SPI_MISO = 35;  // input-only
constexpr int RADIO_CS = 4;

// OOK data into the radio: CC1101 GDO0 or SX1278 DIO2
constexpr int RADIO_DATA = 33;
// SX1278 (Ra-02) reset; unused by the other radios
constexpr int RADIO_RESET = 32;

// SSD1306 128x64 OLED (optional). Not IO2: pull-ups there break serial flashing.
constexpr int I2C_SDA = 5;
constexpr int I2C_SCL = 17;
constexpr uint8_t OLED_ADDR = 0x3C;

// User button (optional): input-only pin, needs an external 10k pull-up to 3V3. Active low.
constexpr int USER_BUTTON = 39;
// Status LED (optional): LED + resistor to GND only.
constexpr int STATUS_LED = 2;

}  // namespace pins
