#pragma once

#include <Arduino.h>
#include <vector>

#include "config.h"

// A 433 MHz OOK front end (CC1101 or SX1278). The bit timing always comes from the RMT on pins::RADIO_DATA;
// the radio chip is only tuned, powered and switched in/out of transmit.
class Radio {
 public:
    virtual ~Radio() = default;
    virtual const char *name() const = 0;
    // Probe and configure the chip. Returns false if it is not detected.
    virtual bool init() = 0;
    // Tune + set power. Only called when either changes.
    virtual bool tune(uint32_t freq_hz, int8_t power_dbm) = 0;
    virtual bool begin_tx() = 0;
    virtual void end_tx() = 0;
    virtual int8_t min_power() const = 0;
    virtual int8_t max_power() const = 0;
    // Short human-readable detail for the UI (chip version etc.).
    virtual String detail() const { return String(); }
    // Data-line wiring (radio data pin <-> pins::RADIO_DATA) as checked by init(): "ok", "fault", or "untested".
    // init() may reconfigure pins::RADIO_DATA; call OokSender::begin() afterwards.
    virtual const char *data_line() const { return "untested"; }
    // Listen-before-talk: receive on the tuned frequency and report signal strength. listen_on() is only
    // called after tune(); listen_off() returns the chip to its idle state, ready for begin_tx().
    virtual bool lbt_supported() const { return false; }
    virtual bool listen_on() { return false; }
    virtual int16_t rssi_dbm() { return -127; }
    virtual void listen_off() {}
    // Put the chip in its lowest-power state. init() brings it back.
    virtual void shutdown() {}
};

Radio *create_radio(RadioType type);

// Sends OOK frames on pins::RADIO_DATA via the RMT peripheral (blocking).
class OokSender {
 public:
    bool begin();  // (re)claims pins::RADIO_DATA for the RMT
    // Send `repeats` back-to-back copies of the frame. Returns false on RMT error.
    bool send(uint8_t protocol, const uint8_t *packet, uint8_t repeats);

 private:
    bool ready_{false};
    std::vector<rmt_data_t> symbols_;  // reused between transmissions (keeps its capacity: no per-send allocation)
};
