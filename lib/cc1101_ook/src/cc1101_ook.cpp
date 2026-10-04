#ifdef ARDUINO
#include "cc1101_ook.h"

namespace {
// Command strobes
constexpr uint8_t SRES = 0x30, SCAL = 0x33, SRX = 0x34, STX = 0x35, SIDLE = 0x36, SPWD = 0x39;
// Configuration registers
constexpr uint8_t IOCFG0 = 0x02, FREQ2 = 0x0D, PATABLE = 0x3E;
// Status registers (read with the burst bit set)
constexpr uint8_t PARTNUM = 0x30, VERSION = 0x31, RSSI = 0x34, MARCSTATE = 0x35;
constexpr uint8_t MARC_IDLE = 0x01, MARC_RX = 0x0D, MARC_TX = 0x13;
constexpr int16_t RSSI_OFFSET_DB = 74;  // datasheet table 31, 433 MHz
constexpr uint8_t READ = 0x80, BURST = 0x40;
constexpr uint8_t GDO_SERIAL_DATA = 0x0D, GDO_HIZ = 0x2E, GDO_LOW = 0x2F, GDO_INVERT = 0x40;

// Asynchronous serial OOK: no preamble/sync, infinite length, FS autocal from IDLE.
// Frequency and PATABLE are written by tune().
constexpr uint8_t INIT_REGS[][2] = {
    {0x00, GDO_SERIAL_DATA},  // IOCFG2: serial data out (RX monitor, unused)
    {0x01, GDO_HIZ},          // IOCFG1
    {0x02, GDO_HIZ},          // IOCFG0: hi-Z while idle; switched to serial data around TX
    {0x03, 0x47},             // FIFOTHR
    {0x07, 0x04},             // PKTCTRL1
    {0x08, 0x32},             // PKTCTRL0: asynchronous serial mode, infinite packet length
    {0x0B, 0x06},             // FSCTRL1
    {0x0C, 0x00},             // FSCTRL0
    {0x10, 0x87},             // MDMCFG4: ~203 kHz RX bandwidth
    {0x11, 0x32},             // MDMCFG3
    {0x12, 0x30},             // MDMCFG2: ASK/OOK, no preamble/sync
    {0x13, 0x02},             // MDMCFG1
    {0x14, 0xF8},             // MDMCFG0
    {0x15, 0x15},             // DEVIATN
    {0x17, 0x30},             // MCSM1: return to IDLE after TX/RX
    {0x18, 0x18},             // MCSM0: auto-calibrate IDLE -> TX
    {0x19, 0x16},             // FOCCFG
    {0x1B, 0x03},             // AGCCTRL2
    {0x1C, 0x00},             // AGCCTRL1
    {0x1D, 0x91},             // AGCCTRL0
    {0x21, 0x56},             // FREND1
    {0x22, 0x11},             // FREND0: OOK '1' uses PATABLE[1], '0' uses PATABLE[0] (= off)
    {0x23, 0xE9},             // FSCAL3
    {0x24, 0x2A},             // FSCAL2
    {0x25, 0x00},             // FSCAL1
    {0x26, 0x1F},             // FSCAL0
    {0x2C, 0x81},             // TEST2
    {0x2D, 0x35},             // TEST1
    {0x2E, 0x09},             // TEST0
};
}  // namespace

bool Cc1101Ook::begin() {
    pinMode(this->cs_, OUTPUT);
    digitalWrite(this->cs_, HIGH);
    // Manual power-on reset sequence (datasheet 19.1.2)
    delayMicroseconds(5);
    digitalWrite(this->cs_, LOW);
    delayMicroseconds(10);
    digitalWrite(this->cs_, HIGH);
    delayMicroseconds(45);
    this->version_ = 0;
    if (!this->strobe_(SRES))
        return false;
    delay(1);
    this->part_ = this->read_status_(PARTNUM);
    uint8_t version = this->read_status_(VERSION);
    if (this->part_ != 0x00 || version == 0x00 || version == 0xFF)
        return false;
    this->version_ = version;
    for (const auto &r : INIT_REGS)
        this->write_(r[0], r[1]);
    this->strobe_(SIDLE);
    return true;
}

bool Cc1101Ook::tune(uint32_t freq_hz, int8_t power_dbm) {
    this->strobe_(SIDLE);
    uint32_t f = cc1101_ook::freq_word(freq_hz);
    this->write_(FREQ2, (f >> 16) & 0xFF);
    this->write_(FREQ2 + 1, (f >> 8) & 0xFF);
    this->write_(FREQ2 + 2, f & 0xFF);
    int8_t dbm = power_dbm < cc1101_ook::MIN_DBM   ? cc1101_ook::MIN_DBM
                 : power_dbm > cc1101_ook::MAX_DBM ? cc1101_ook::MAX_DBM
                                                   : power_dbm;
    uint8_t table[8] = {0x00, cc1101_ook::pa_value_433(dbm), 0, 0, 0, 0, 0, 0};
    this->write_burst_(PATABLE, table, sizeof(table));
    this->strobe_(SCAL);
    return this->wait_state_(MARC_IDLE, 10);
}

bool Cc1101Ook::tx_on() {
    this->write_(IOCFG0, GDO_SERIAL_DATA);
    this->strobe_(STX);
    if (!this->wait_state_(MARC_TX, 5)) {
        this->tx_off();
        return false;
    }
    return true;
}

void Cc1101Ook::tx_off() {
    this->strobe_(SIDLE);
    this->write_(IOCFG0, GDO_HIZ);
}

void Cc1101Ook::power_down() {
    this->tx_off();
    this->strobe_(SPWD);  // takes effect when CS goes high; the next SPI access wakes the chip again
}

bool Cc1101Ook::rx_on() {
    this->strobe_(SRX);  // GDO0 keeps its idle (high impedance) setting in RX
    if (!this->wait_state_(MARC_RX, 5)) {
        this->tx_off();
        return false;
    }
    return true;
}

int16_t Cc1101Ook::rssi_dbm() {
    uint8_t raw = this->read_status_(RSSI);
    int16_t v = raw >= 128 ? (int16_t) raw - 256 : (int16_t) raw;
    return v / 2 - RSSI_OFFSET_DB;
}

void Cc1101Ook::gdo0_drive(int level) {
    this->write_(IOCFG0, level < 0 ? GDO_HIZ : level ? (GDO_LOW | GDO_INVERT) : GDO_LOW);
}

bool Cc1101Ook::select_() {
    this->spi_.beginTransaction(this->settings_);
    digitalWrite(this->cs_, LOW);
    uint32_t start = micros();
    while (digitalRead(this->miso_)) {  // chip ready when SO goes low
        if (micros() - start > 5000) {
            this->deselect_();
            return false;
        }
    }
    return true;
}

void Cc1101Ook::deselect_() {
    digitalWrite(this->cs_, HIGH);
    this->spi_.endTransaction();
}

bool Cc1101Ook::strobe_(uint8_t cmd) {
    if (!this->select_())
        return false;
    this->spi_.transfer(cmd);
    this->deselect_();
    return true;
}

void Cc1101Ook::write_(uint8_t addr, uint8_t value) {
    if (!this->select_())
        return;
    this->spi_.transfer(addr);
    this->spi_.transfer(value);
    this->deselect_();
}

void Cc1101Ook::write_burst_(uint8_t addr, const uint8_t *data, size_t len) {
    if (!this->select_())
        return;
    this->spi_.transfer(addr | BURST);
    for (size_t i = 0; i < len; i++)
        this->spi_.transfer(data[i]);
    this->deselect_();
}

uint8_t Cc1101Ook::read_status_(uint8_t addr) {
    if (!this->select_())
        return 0xFF;
    this->spi_.transfer(addr | READ | BURST);
    uint8_t v = this->spi_.transfer(0x00);
    this->deselect_();
    return v;
}

bool Cc1101Ook::wait_state_(uint8_t state, uint32_t timeout_ms) {
    uint32_t start = millis();
    while (millis() - start <= timeout_ms) {
        if ((this->read_status_(MARCSTATE) & 0x1F) == state)
            return true;
        delayMicroseconds(50);
    }
    return false;
}
#endif  // ARDUINO
