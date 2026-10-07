#ifdef ARDUINO
#include "cc1101_ook.h"

namespace {
// Command strobes
constexpr uint8_t SRES = 0x30, SCAL = 0x33, SRX = 0x34, STX = 0x35, SIDLE = 0x36, SPWD = 0x39;
// Configuration registers
constexpr uint8_t IOCFG0 = 0x02, FREQ2 = 0x0D, MDMCFG4 = 0x10, AGCCTRL2 = 0x1B, AGCCTRL1 = 0x1C, AGCCTRL0 = 0x1D,
                  FREND1 = 0x21, PATABLE = 0x3E;
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

// Receiver mode register sets (see cc1101_ook::RxProfile). MDMCFG4 keeps DRATE_E = 7 (unused in asynchronous
// mode). FREND1 0xB6 is TI's RX front-end setting for channel filters above ~100 kHz.
struct RxRegs {
    uint8_t mdmcfg4, agcctrl2, agcctrl1, agcctrl0, frend1;
};
constexpr RxRegs RX_REGS[cc1101_ook::NUM_RX_PROFILES] = {
    {0x97, 0x03, 0x00, 0x91, 0xB6},  // normal: 162 kHz; AGC: all gains, 33 dB target (DN022 OOK)
    {0x97, 0xD3, 0x09, 0xF1, 0xB6},  // near: 162 kHz; top 3 DVGA steps and some LNA gain off, slower AGC
    {0x57, 0x03, 0x00, 0x91, 0xB6},  // wide: 325 kHz
};
// The transmit set from INIT_REGS, put back when receiver mode ends.
constexpr RxRegs TX_REGS = {0x87, 0x03, 0x00, 0x91, 0x56};

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

void Cc1101Ook::write_rx_set_(uint8_t mdmcfg4, uint8_t agcctrl2, uint8_t agcctrl1, uint8_t agcctrl0, uint8_t frend1) {
    this->write_(MDMCFG4, mdmcfg4);
    this->write_(AGCCTRL2, agcctrl2);
    this->write_(AGCCTRL1, agcctrl1);
    this->write_(AGCCTRL0, agcctrl0);
    this->write_(FREND1, frend1);
}

uint32_t Cc1101Ook::rx_bandwidth(uint8_t profile) {
    uint8_t m = RX_REGS[profile < cc1101_ook::NUM_RX_PROFILES ? profile : 0].mdmcfg4;
    return cc1101_ook::rx_bandwidth_hz(m >> 6, (m >> 4) & 3);
}

bool Cc1101Ook::rx_data_on(uint8_t profile) {
    this->strobe_(SIDLE);
    const RxRegs &r = RX_REGS[profile < cc1101_ook::NUM_RX_PROFILES ? profile : 0];
    this->write_rx_set_(r.mdmcfg4, r.agcctrl2, r.agcctrl1, r.agcctrl0, r.frend1);
    this->write_(IOCFG0, GDO_SERIAL_DATA);  // in RX: asynchronous serial data out
    this->strobe_(SRX);
    if (!this->wait_state_(MARC_RX, 5)) {
        this->tx_off();
        return false;
    }
    return true;
}

void Cc1101Ook::rx_data_off() {
    this->tx_off();
    this->write_rx_set_(TX_REGS.mdmcfg4, TX_REGS.agcctrl2, TX_REGS.agcctrl1, TX_REGS.agcctrl0, TX_REGS.frend1);
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
