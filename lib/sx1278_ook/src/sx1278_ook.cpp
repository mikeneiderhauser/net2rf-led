#ifdef ARDUINO
#include "sx1278_ook.h"

namespace {
constexpr uint8_t REG_OP_MODE = 0x01, REG_FRF_MSB = 0x06, REG_PA_CONFIG = 0x09, REG_PA_RAMP = 0x0A, REG_OCP = 0x0B,
                  REG_RSSI_VALUE = 0x11, REG_OOK_PEAK = 0x14, REG_RX_TIMEOUT1 = 0x20, REG_PACKET_CONFIG2 = 0x31, REG_IMAGE_CAL = 0x3B, REG_IRQ_FLAGS1 = 0x3E,
                  REG_DIO_MAPPING1 = 0x40, REG_DIO_MAPPING2 = 0x41, REG_VERSION = 0x42, REG_PA_DAC = 0x4D,
                  REG_LNA = 0x0C, REG_RX_CONFIG = 0x0D, REG_RX_BW = 0x12, REG_OOK_FIX = 0x15, REG_OOK_AVG = 0x16;
// OpMode: FSK/OOK modem (LongRangeMode=0), OOK modulation, low-frequency band registers (<525 MHz)
constexpr uint8_t OPMODE_BASE = 0x20 | 0x08;
constexpr uint8_t MODE_SLEEP = 0x00, MODE_STDBY = 0x01, MODE_TX = 0x03, MODE_RX = 0x05;
constexpr uint8_t PACKET_MODE = 0x40;      // RegPacketConfig2 DataMode
constexpr uint8_t DIO2_TIMEOUT = 0x20;     // RegDioMapping1 DIO2 = TimeOut (packet mode): stays low, no timeout set
constexpr uint8_t IRQ1_TX_READY = 0x20, IMAGE_CAL_START = 0x40, IMAGE_CAL_RUNNING = 0x20;
constexpr uint8_t DIO2_DATA = 0x00;        // RegDioMapping1 DIO2 = Data (continuous mode)

// Receiver mode per profile: RegRxBw, RegLna, RegRxConfig.
struct RxRegs {
    uint8_t rx_bw, lna, rx_config;
};
constexpr RxRegs RX_REGS[sx1278_ook::NUM_RX_PROFILES] = {
    {0x11, 0x20, 0x08},  // normal: 166.7 kHz; AGC on (it picks the LNA gain when RX starts, i.e. G1 on a quiet channel)
    {0x11, 0x80, 0x00},  // near: 166.7 kHz; AGC off, LNA fixed at G4 (-24 dB)
    {0x01, 0x20, 0x08},  // wide: 250 kHz
};
}  // namespace

uint32_t Sx1278Ook::rx_bandwidth(uint8_t profile) {
    return sx1278_ook::rx_bandwidth_hz(RX_REGS[profile < sx1278_ook::NUM_RX_PROFILES ? profile : 0].rx_bw);
}

bool Sx1278Ook::rx_data_on(uint8_t profile) {
    const RxRegs &r = RX_REGS[profile < sx1278_ook::NUM_RX_PROFILES ? profile : 0];
    this->set_mode_(MODE_STDBY);
    this->write_(REG_PACKET_CONFIG2, 0x00);  // continuous mode
    this->write_(REG_OOK_PEAK, 0x08);        // bit synchroniser off (raw data out), peak threshold, 0.5 dB steps
    this->write_(REG_OOK_FIX, 0x0C);         // peak mode floor: 6 dB
    this->write_(REG_OOK_AVG, 0x12);         // threshold decays once per chip, the datasheet default
    this->write_(REG_RX_BW, r.rx_bw);
    this->write_(REG_LNA, r.lna);
    this->write_(REG_RX_CONFIG, r.rx_config);
    this->write_(REG_DIO_MAPPING1, DIO2_DATA);  // DIO2 becomes the data *output* in RX
    this->set_mode_(MODE_RX);
    delay(1);  // RX start-up and AGC settling
    return (this->read_(REG_OP_MODE) & 0x07) == MODE_RX;
}

void Sx1278Ook::rx_data_off() {
    this->set_mode_(MODE_STDBY);
    this->write_(REG_PACKET_CONFIG2, 0x00);
    this->write_(REG_DIO_MAPPING1, 0x00);  // DIO2 back to the TX data input
    // Receiver settings back to their reset values, which listen before transmit was tuned with (the near profile's
    // fixed low LNA gain would make it read 24 dB low).
    this->write_(REG_RX_BW, 0x15);
    this->write_(REG_LNA, 0x20);
    this->write_(REG_RX_CONFIG, 0x0E);
}

bool Sx1278Ook::begin() {
    pinMode(this->cs_, OUTPUT);
    digitalWrite(this->cs_, HIGH);
    if (this->reset_ >= 0) {
        pinMode(this->reset_, OUTPUT);
        digitalWrite(this->reset_, LOW);
        delay(1);
        pinMode(this->reset_, INPUT);  // release: the module pulls NRESET up
        delay(10);
    }
    uint8_t version = this->read_(REG_VERSION);
    this->version_ = 0;
    if (version != sx1278_ook::CHIP_VERSION)
        return false;
    this->version_ = version;
    this->write_(REG_OP_MODE, MODE_SLEEP);  // LongRangeMode can only change in sleep
    this->write_(REG_OP_MODE, OPMODE_BASE | MODE_SLEEP);
    this->write_(REG_PACKET_CONFIG2, 0x00);  // continuous mode: DIO2 is the TX data input
    this->write_(REG_OOK_PEAK, 0x08);        // bit synchronizer off (data is not clocked)
    this->write_(REG_PA_RAMP, 0x09);         // 40 us ramp, no shaping
    this->write_(REG_OCP, 0x2F);             // over-current trip 120 mA
    this->write_(REG_DIO_MAPPING1, 0x00);
    this->write_(REG_DIO_MAPPING2, 0x00);
    this->write_(REG_PA_DAC, 0x84);  // normal PA_BOOST (up to +17 dBm)
    this->set_mode_(MODE_STDBY);
    return true;
}

bool Sx1278Ook::tune(uint32_t freq_hz, int8_t power_dbm) {
    this->set_mode_(MODE_STDBY);
    uint32_t f = sx1278_ook::frf(freq_hz);
    this->write_(REG_FRF_MSB, (f >> 16) & 0xFF);
    this->write_(REG_FRF_MSB + 1, (f >> 8) & 0xFF);
    this->write_(REG_FRF_MSB + 2, f & 0xFF);
    this->write_(REG_PA_CONFIG, sx1278_ook::pa_config(power_dbm));
    // Image calibration for the new frequency
    this->write_(REG_IMAGE_CAL, this->read_(REG_IMAGE_CAL) | IMAGE_CAL_START);
    uint32_t start = millis();
    while (this->read_(REG_IMAGE_CAL) & IMAGE_CAL_RUNNING) {
        if (millis() - start > 20)
            return false;
        delay(1);
    }
    return true;
}

bool Sx1278Ook::tx_on() {
    this->set_mode_(MODE_TX);
    uint32_t start = micros();
    while (!(this->read_(REG_IRQ_FLAGS1) & IRQ1_TX_READY)) {
        if (micros() - start > 5000) {
            this->tx_off();
            return false;
        }
    }
    return true;
}

void Sx1278Ook::tx_off() { this->set_mode_(MODE_STDBY); }

void Sx1278Ook::power_down() { this->set_mode_(MODE_SLEEP); }

bool Sx1278Ook::rx_on() {
    this->set_mode_(MODE_STDBY);
    this->write_(REG_RX_TIMEOUT1, 0x00);  // no RSSI timeout: DIO2 (TimeOut) stays low
    this->write_(REG_DIO_MAPPING1, DIO2_TIMEOUT);
    this->write_(REG_PACKET_CONFIG2, PACKET_MODE);
    this->set_mode_(MODE_RX);
    return true;
}

int16_t Sx1278Ook::rssi_dbm() { return -(int16_t) this->read_(REG_RSSI_VALUE) / 2; }

void Sx1278Ook::rx_off() {
    this->set_mode_(MODE_STDBY);
    this->write_(REG_PACKET_CONFIG2, 0x00);  // continuous mode again: DIO2 is the TX data input
    this->write_(REG_DIO_MAPPING1, 0x00);
}

void Sx1278Ook::set_mode_(uint8_t mode) { this->write_(REG_OP_MODE, OPMODE_BASE | mode); }

void Sx1278Ook::write_(uint8_t reg, uint8_t value) {
    this->spi_.beginTransaction(this->settings_);
    digitalWrite(this->cs_, LOW);
    this->spi_.transfer(reg | 0x80);
    this->spi_.transfer(value);
    digitalWrite(this->cs_, HIGH);
    this->spi_.endTransaction();
}

uint8_t Sx1278Ook::read_(uint8_t reg) {
    this->spi_.beginTransaction(this->settings_);
    digitalWrite(this->cs_, LOW);
    this->spi_.transfer(reg & 0x7F);
    uint8_t v = this->spi_.transfer(0x00);
    digitalWrite(this->cs_, HIGH);
    this->spi_.endTransaction();
    return v;
}
#endif  // ARDUINO
