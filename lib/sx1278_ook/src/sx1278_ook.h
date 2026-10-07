#pragma once

// Minimal Semtech SX1276/77/78 driver (Ai-Thinker Ra-02, HopeRF RFM96W/98W, EBYTE E19) for
// continuous-mode OOK transmission.
//
// The chip is only tuned, powered and switched between STANDBY and TX here; the actual on/off
// keying comes from an external pulse source (e.g. the ESP32 RMT) driving DIO2, which the SX127x
// uses as its TX data input in FSK/OOK continuous mode. Output is on PA_BOOST (all Ra-02 variants).
//
// The caller owns the SPI bus: call spi.begin(sck, miso, mosi) before begin().

#include <cstdint>

namespace sx1278_ook {

// ---- Pure register math (no hardware, host-testable) ----

static const uint32_t FXOSC_HZ = 32000000;
static const int8_t MIN_DBM = 2;   // PA_BOOST range without the +20 dBm high-power mode
static const int8_t MAX_DBM = 17;
static const uint8_t CHIP_VERSION = 0x12;

// Receiver profiles (receiver mode), matching the CC1101's: normal 166.7 kHz with AGC, near 166.7 kHz with the
// LNA fixed 24 dB down (a transmitter within a few metres saturates the receiver otherwise), wide 250 kHz.
enum RxProfile : uint8_t { RX_NORMAL = 0, RX_NEAR = 1, RX_WIDE = 2, NUM_RX_PROFILES };

// Receiver bandwidth for RegRxBw (RxBwMant bits 4-3: 16 / 20 / 24, RxBwExp bits 2-0), FSK/OOK mode:
// FXOSC / (mant * 2^(exp + 2)).
inline uint32_t rx_bandwidth_hz(uint8_t reg_rx_bw) {
    static const uint8_t MANT[] = {16, 20, 24, 24};
    return FXOSC_HZ / ((uint32_t) MANT[(reg_rx_bw >> 3) & 3] << ((reg_rx_bw & 7) + 2));
}

// RegFrf (MSB:MID:LSB) for a carrier frequency: f_rf = FXOSC / 2^19 * Frf.
inline uint32_t frf(uint32_t hz) { return (uint32_t) (((uint64_t) hz << 19) / FXOSC_HZ); }

// RegPaConfig for PA_BOOST: PaSelect=1, MaxPower=7, Pout = 17 - (15 - OutputPower) dBm.
inline uint8_t pa_config(int8_t dbm) {
    if (dbm < MIN_DBM)
        dbm = MIN_DBM;
    if (dbm > MAX_DBM)
        dbm = MAX_DBM;
    return (uint8_t) (0x80 | 0x70 | (uint8_t) (dbm - 2));
}

}  // namespace sx1278_ook

#ifdef ARDUINO
#include <Arduino.h>
#include <SPI.h>

class Sx1278Ook {
 public:
    // `reset_pin` may be -1 if NRESET is not wired.
    Sx1278Ook(SPIClass &spi, int cs_pin, int reset_pin, uint32_t spi_hz = 4000000)
        : spi_(spi), cs_(cs_pin), reset_(reset_pin), settings_(spi_hz, MSBFIRST, SPI_MODE0) {}

    // Reset, verify the chip answers (RegVersion 0x12), and configure continuous OOK TX.
    bool begin();
    // Set carrier frequency and output power (dBm, clamped to MIN_DBM..MAX_DBM), then run image calibration.
    bool tune(uint32_t freq_hz, int8_t power_dbm);
    // Enter TX: from here DIO2 keys the carrier. False if the PA did not report TxReady.
    bool tx_on();
    // Back to STANDBY.
    void tx_off();
    // Sleep: oscillator off (about 0.2 uA). Call begin() and tune() again before the next use.
    void power_down();
    // Listen-before-talk: enter RX on the tuned frequency and read the received signal strength, then
    // rx_off(). RX runs in packet mode with DIO2 mapped to an idle-low signal: in continuous mode DIO2 would
    // become a data *output* and fight the MCU driving the TX data line.
    bool rx_on();
    int16_t rssi_dbm();
    void rx_off();
    // Receiver mode: continuous-mode RX with the demodulated OOK signal (bit synchroniser off) on DIO2, which
    // is the TX data line, so the caller must have released its end. rx_data_off() goes back to STANDBY.
    bool rx_data_on(uint8_t profile = sx1278_ook::RX_NORMAL);
    void rx_data_off();
    static uint32_t rx_bandwidth(uint8_t profile);

    uint8_t version() const { return version_; }  // 0 = not detected

 private:
    void set_mode_(uint8_t mode);
    void write_(uint8_t reg, uint8_t value);
    uint8_t read_(uint8_t reg);

    SPIClass &spi_;
    int cs_, reset_;
    SPISettings settings_;
    uint8_t version_{0};
};
#endif  // ARDUINO
