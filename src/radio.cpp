#include "radio.h"

#include <SPI.h>
#include <cc1101_ook.h>
#include <sx1278_ook.h>
#include <vector>

#include "bracelet_protocol.h"
#include "pins.h"

// Both radios share one SPI bus on the WT32-ETH01 (see pins.h); only one is fitted.
static SPIClass s_spi(HSPI);

static void spi_start() {
    static bool started = false;
    if (started)
        return;
    s_spi.begin(pins::SPI_SCK, pins::SPI_MISO, pins::SPI_MOSI, -1);
    started = true;
}

static String version_detail(uint8_t version) {
    return version ? String("version 0x") + String(version, HEX) : String();
}

// Adapters from the app's Radio interface onto the standalone chip libraries.

class Cc1101Radio : public Radio {
 public:
    const char *name() const override { return "CC1101"; }
    int8_t min_power() const override { return cc1101_ook::MIN_DBM; }
    int8_t max_power() const override { return cc1101_ook::MAX_DBM; }
    String detail() const override {
        String d = version_detail(this->chip_.version());
        if (this->data_ok_ == 0)
            d += ", NO SIGNAL ON GDO0: check GDO0 -> IO" + String(pins::RADIO_DATA);
        return d;
    }
    const char *data_line() const override { return this->data_ok_ < 0 ? "untested" : this->data_ok_ ? "ok" : "fault"; }

    bool init() override {
        spi_start();
        if (!this->chip_.begin()) {
            log_w("CC1101 not detected (partnum 0x%02X)", this->chip_.part_number());
            this->data_ok_ = -1;
            return false;
        }
        log_i("CC1101 detected, version 0x%02X", this->chip_.version());
        this->data_ok_ = this->check_data_line_();
        if (!this->data_ok_)
            log_e("CC1101 GDO0 is not reaching GPIO%d: nothing will be transmitted", pins::RADIO_DATA);
        return true;
    }
    bool tune(uint32_t freq_hz, int8_t power_dbm) override { return this->chip_.tune(freq_hz, power_dbm); }
    bool begin_tx() override {
        if (this->chip_.tx_on())
            return true;
        log_w("CC1101 did not enter TX");
        return false;
    }
    void end_tx() override { this->chip_.tx_off(); }
    bool lbt_supported() const override { return true; }
    bool listen_on() override { return this->chip_.rx_on(); }
    int16_t rssi_dbm() override { return this->chip_.rssi_dbm(); }
    void listen_off() override { this->chip_.tx_off(); }

 private:
    // GDO0 is the CC1101's TX data input, so if it isn't wired to the RMT pin the chip keys up but radiates
    // nothing, and every counter still looks healthy. Have the chip drive GDO0 high then low, and read
    // it back against the opposite internal pull so a floating pin can't pass.
    bool check_data_line_() {
        rmtDeinit(pins::RADIO_DATA);
        pinMode(pins::RADIO_DATA, INPUT_PULLDOWN);
        this->chip_.gdo0_drive(1);
        delayMicroseconds(50);
        bool high = digitalRead(pins::RADIO_DATA);
        pinMode(pins::RADIO_DATA, INPUT_PULLUP);
        this->chip_.gdo0_drive(0);
        delayMicroseconds(50);
        bool low = !digitalRead(pins::RADIO_DATA);
        this->chip_.gdo0_drive(-1);
        pinMode(pins::RADIO_DATA, INPUT);
        log_i("CC1101 data line check: high %s, low %s", high ? "ok" : "FAIL", low ? "ok" : "FAIL");
        return high && low;
    }

    Cc1101Ook chip_{s_spi, pins::RADIO_CS, pins::SPI_MISO};
    int8_t data_ok_{-1};
};

class Sx1278Radio : public Radio {
 public:
    const char *name() const override { return "SX1278"; }
    int8_t min_power() const override { return sx1278_ook::MIN_DBM; }
    int8_t max_power() const override { return sx1278_ook::MAX_DBM; }
    String detail() const override { return version_detail(this->chip_.version()); }

    bool init() override {
        spi_start();
        if (!this->chip_.begin()) {
            log_w("SX1278 not detected");
            return false;
        }
        log_i("SX1278 detected, version 0x%02X", this->chip_.version());
        return true;
    }
    bool tune(uint32_t freq_hz, int8_t power_dbm) override { return this->chip_.tune(freq_hz, power_dbm); }
    bool begin_tx() override {
        if (this->chip_.tx_on())
            return true;
        log_w("SX1278 did not enter TX");
        return false;
    }
    void end_tx() override { this->chip_.tx_off(); }
    bool lbt_supported() const override { return true; }
    bool listen_on() override { return this->chip_.rx_on(); }
    int16_t rssi_dbm() override { return this->chip_.rssi_dbm(); }
    void listen_off() override { this->chip_.rx_off(); }

 private:
    Sx1278Ook chip_{s_spi, pins::RADIO_CS, pins::RADIO_RESET};
};

Radio *create_radio(RadioType type) {
    switch (type) {
        case RADIO_SX1278:
            return new Sx1278Radio();
        case RADIO_CC1101:
        default:
            return new Cc1101Radio();
    }
}

// =============================================================================================
// RMT sender
// =============================================================================================

namespace {
// Collects encode_frame() output as RMT symbols: every mark is followed by a space.
struct RmtSink {
    std::vector<rmt_data_t> &out;
    uint32_t pending_mark{0};
    void mark(uint32_t us) { this->pending_mark = us; }
    void space(uint32_t us) {
        rmt_data_t sym;
        sym.duration0 = this->pending_mark;
        sym.level0 = 1;
        sym.duration1 = us;
        sym.level1 = 0;
        this->out.push_back(sym);
    }
};
}  // namespace

bool OokSender::begin() {
    pinMode(pins::RADIO_DATA, OUTPUT);
    digitalWrite(pins::RADIO_DATA, LOW);
    this->ready_ = rmtInit(pins::RADIO_DATA, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 1000000);  // 1 us ticks
    this->symbols_.reserve((4 + bracelet::PACKET_LEN * 8) * 3);  // default repeats; grows once if more are used
    if (!this->ready_)
        log_e("RMT init failed on GPIO%d", pins::RADIO_DATA);
    return this->ready_;
}

bool OokSender::send(uint8_t protocol, const uint8_t *packet, uint8_t repeats) {
    if (!this->ready_)
        return false;
    this->symbols_.clear();
    RmtSink sink{this->symbols_};
    for (uint8_t i = 0; i < repeats; i++)
        bracelet::encode_frame(protocol, packet, sink);
    uint32_t timeout_ms = bracelet::frame_us(protocol) * repeats / 1000 + 100;
    return rmtWrite(pins::RADIO_DATA, this->symbols_.data(), this->symbols_.size(), timeout_ms);
}
