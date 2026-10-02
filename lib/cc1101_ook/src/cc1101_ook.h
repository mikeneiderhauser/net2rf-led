#pragma once

// Minimal TI CC1101 driver for asynchronous serial OOK transmission.
//
// The CC1101 is only tuned, powered and switched between IDLE and TX here; the actual on/off
// keying comes from an external pulse source (e.g. the ESP32 RMT) driving the chip's GDO0 pin,
// which the CC1101 uses as its TX data input in asynchronous serial mode.
//
// The caller owns the SPI bus: call spi.begin(sck, miso, mosi) before begin().

#include <cstddef>
#include <cstdint>

namespace cc1101_ook {

// ---- Pure register math (no hardware, host-testable) ----

static const uint32_t XTAL_HZ = 26000000;
static const int8_t MIN_DBM = -30;
static const int8_t MAX_DBM = 10;

// FREQ2:FREQ1:FREQ0 word for a carrier frequency: f_carrier = XTAL / 2^16 * FREQ.
inline uint32_t freq_word(uint32_t hz) { return (uint32_t) (((uint64_t) hz << 16) / XTAL_HZ); }

// PATABLE value for the highest 433 MHz power step not above `dbm` (TI DN013).
inline uint8_t pa_value_433(int8_t dbm) {
    struct Entry {
        int8_t dbm;
        uint8_t value;
    };
    static const Entry TABLE[] = {{-30, 0x12}, {-20, 0x0E}, {-15, 0x1D}, {-10, 0x34}, {0, 0x60},
                                  {5, 0x84},   {7, 0xC8},   {10, 0xC0}};
    uint8_t value = TABLE[0].value;
    for (const Entry &e : TABLE) {
        if (e.dbm <= dbm)
            value = e.value;
    }
    return value;
}

}  // namespace cc1101_ook

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>

class Cc1101Ook {
 public:
    // `miso_pin` is polled for the chip-ready signal (SO low after CS asserts).
    Cc1101Ook(SPIClass &spi, int cs_pin, int miso_pin, uint32_t spi_hz = 4000000)
        : spi_(spi), cs_(cs_pin), miso_(miso_pin), settings_(spi_hz, MSBFIRST, SPI_MODE0) {}

    // Reset, verify the chip answers, and load the async-OOK register set. False if not detected.
    bool begin();
    // Set carrier frequency and output power (dBm, clamped to MIN_DBM..MAX_DBM), then calibrate.
    bool tune(uint32_t freq_hz, int8_t power_dbm);
    // Enter TX: from here GDO0 keys the carrier. False if the chip did not reach TX.
    bool tx_on();
    // Back to IDLE (from TX or RX), GDO0 released to high impedance.
    void tx_off();
    // Listen-before-talk: enter RX on the tuned frequency (GDO0 stays high impedance), read the received
    // signal strength, then tx_off() to go back to IDLE.
    bool rx_on();
    int16_t rssi_dbm();
    // Wiring check: drive GDO0 as a plain output (0 or 1), or -1 to release it (high impedance).
    // Only while idle; the caller reads its own end of the data line to confirm the connection.
    void gdo0_drive(int level);

    uint8_t version() const { return version_; }  // 0 = not detected
    uint8_t part_number() const { return part_; }

 private:
    bool select_();
    void deselect_();
    bool strobe_(uint8_t cmd);
    void write_(uint8_t addr, uint8_t value);
    void write_burst_(uint8_t addr, const uint8_t *data, size_t len);
    uint8_t read_status_(uint8_t addr);
    bool wait_state_(uint8_t state, uint32_t timeout_ms);

    SPIClass &spi_;
    int cs_, miso_;
    SPISettings settings_;
    uint8_t version_{0}, part_{0xFF};
};
#endif  // ARDUINO
