#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>

#include "bracelet_protocol.h"
#include "bracelet_rx.h"
#include "pulse_capture.h"
#include "config.h"
#include "radio.h"

// One recursive mutex guards g_app, g_net and all engine state. Held briefly by the DDP receive
// task, the web server, the display and the engine task (never during RF transmission).
void state_lock_init();
class StateLock {
 public:
    StateLock();
    ~StateLock();
    StateLock(const StateLock &) = delete;
    StateLock &operator=(const StateLock &) = delete;
};

enum class RadioState : uint8_t { INITIALIZING, READY, NOT_DETECTED, OFF };
enum class TestMode : uint8_t { OFF, SOLID, CYCLE };

const char *radio_state_name(RadioState s);

// One zone's packet in one protocol: what the bracelets should show (want) and what was last sent (sent).
struct ZoneLane {
    uint8_t want[bracelet::PACKET_LEN]{};
    uint8_t sent[bracelet::PACKET_LEN]{};
    bool have_want{false};  // the zone is live on this protocol and has a colour
    bool have_sent{false};
    uint32_t last_tx_ms{0};
};

struct ZoneState {
    uint8_t r{0}, g{0}, b{0}, fx{0};
    bool gated{false};  // vendor mode: boot-code channel != 85, so nothing is transmitted
    uint8_t group{0};   // vendor mode: group code taken from the input
    // One lane per protocol. Only the protocols the zone uses ever have a packet; a zone on both sends each change
    // on protocol 0, then protocol 1.
    ZoneLane lane[bracelet::NUM_PROTOCOLS];
    uint32_t tx_count{0};
    bool have_sent() const { return this->lane[0].have_sent || this->lane[1].have_sent; }
    uint32_t last_tx_ms() const {  // the latest transmission on any of its protocols
        if (this->lane[0].have_sent && this->lane[1].have_sent)
            return (int32_t) (this->lane[1].last_tx_ms - this->lane[0].last_tx_ms) > 0 ? this->lane[1].last_tx_ms
                                                                                      : this->lane[0].last_tx_ms;
        return this->lane[0].have_sent ? this->lane[0].last_tx_ms : this->lane[1].last_tx_ms;
    }
    void resend() {  // forget what was sent, so every live lane goes out again
        for (ZoneLane &l : this->lane)
            l.have_sent = false;
    }
};

enum class InputSource : uint8_t { NONE, DDP, E131 };

struct InputStats {
    uint32_t packets{0};     // valid data packets (DDP + E1.31)
    uint32_t ddp_packets{0};
    uint32_t e131_packets{0};
    InputSource last_source{InputSource::NONE};
    uint32_t frames{0};      // packets with the PUSH flag (end of an xLights frame)
    uint32_t malformed{0};   // too short / wrong version
    uint32_t ignored{0};     // queries, other destination ids, out-of-range offsets
    uint32_t last_bad_offset{0};  // most recent out-of-range channel offset (0-based) ...
    uint32_t last_bad_offset_ms{0};  // ... and when (0 = never)
    uint64_t bytes{0};
    uint32_t last_src_ip{0};
    uint32_t last_rx_ms{0};
    bool seen{false};
    bool timed_out{false};
    float fps{0};
};

struct OutputStats {
    uint32_t updates{0};  // transmissions (each = `repeats` frames)
    uint32_t frames{0};
    uint32_t errors{0};
    uint32_t manual{0};
    uint32_t suppressed{0};  // zone updates consumed while output was disabled
    float airtime_pct{0};  // share of the last 10 s spent transmitting
    // listen before transmit
    uint32_t lbt_checks{0};   // transmissions that listened first
    uint32_t lbt_waits{0};    // ... and found the channel busy at least once
    uint32_t lbt_forced{0};   // ... and gave up waiting (sent anyway)
    uint32_t lbt_wait_ms{0};  // total time spent waiting for a clear channel
};

// Receiver mode, for the OLED.
struct ReceiverSnapshot {
    bool enabled;    // receiver mode is selected
    bool active;     // ... and the radio is listening
    bool supported;  // the fitted radio can receive (CC1101)
    uint32_t freq_hz;
    uint32_t frames, bad, updates;
    int16_t rssi_dbm;          // channel level now (average over the last half second)
    int32_t last_age_ms;       // -1 = nothing heard yet
    uint8_t count;
    struct Row {
        uint8_t protocol, group;  // group bracelet::RxTracker::ALL_GROUPS = every group
        uint8_t r, g, b;
        const char *label;
        uint32_t age_ms;
    } rows[bracelet::RxTracker::MAX_ZONES];
};

struct EngineSnapshot {  // for the OLED
    RadioState radio_state;
    const char *radio_name;
    float fps;
    uint32_t packets;
    bool input_seen, timed_out, test_active, output_enabled;
    InputSource last_source;
    uint32_t input_age_ms;
    uint8_t num_zones;
    uint8_t r[MAX_ZONES], g[MAX_ZONES], b[MAX_ZONES], enabled[MAX_ZONES];
    float airtime_pct;
};

class Engine {
 public:
    void begin();

    // Network input (called from the UDP receive tasks).
    void on_ddp(const uint8_t *data, size_t len, uint32_t src_ip);
    void on_e131(const uint8_t *data, size_t len, uint32_t src_ip);


    // Call (with StateLock held) after g_app changes.
    void config_changed() { this->config_dirty_ = true; }

    // Queue one-off transmissions (web test buttons, raw packets, address probe).
    // Both return false when output is disabled or the queue is full.
    bool send_raw(uint8_t protocol, const uint8_t *packet, uint8_t repeats, bool fix_checksum);
    bool send_zone(int zone, bracelet::Action action, uint8_t r, uint8_t g, uint8_t b);  // -1 = all enabled

    void set_test(TestMode mode, uint8_t r, uint8_t g, uint8_t b);
    // Panic button: leave test mode and switch every bracelet off (one broadcast on protocol 1).
    // Bracelets then stay off until the input actually changes a colour. False if output is disabled.
    bool all_off();
    void set_suspended(bool suspended) { this->suspended_ = suspended; }  // e.g. during OTA

    void status_json(JsonObject out);
    // Tools page: the last transmissions (newest first) and the input channels the zones read.
    void tools_json(JsonObject out);
    // Tools page: listen on the bracelet frequency for about 20 ms and report the signal strength. Runs in the
    // engine task between transmissions; false if the radio can't listen (off, not ready, no receiver).
    bool measure_rssi(int16_t &peak_dbm, int16_t &avg_dbm, uint32_t &freq_hz);
    EngineSnapshot snapshot();
    void rx_snapshot(ReceiverSnapshot &out);
    bool receiving() const { return this->rx_active_; }
    // Receiver mode raw capture: one stored burst, as JSON pulses or as rtl_433 pulse data. False if unknown.
    bool rx_capture_json(uint32_t id, JsonObject out);
    bool rx_capture_ook(uint32_t id, String &out);
    uint8_t rx_zone_count() {
        StateLock lock;
        return this->tracker_.count();
    }
    void reset_stats();

 private:
    struct Job {
        uint8_t protocol;
        uint8_t packet[bracelet::PACKET_LEN];
        uint8_t repeats;
        bool manual;
    };

    static void task_entry_(void *arg);
    void run_();
    void try_init_radio_();
    // Protocol 1 has a confirmed "all groups" address (group 0), so fan-out actions to the protocol 1 zones can be
    // one packet. Only when some zone uses protocol 1: a protocol 0-only controller never sends one.
    bool can_broadcast_() const { return protocol_in_use(g_app, 1); }
    void queue_broadcast_(bracelet::Action action, uint8_t r, uint8_t g, uint8_t b);
    // Treat the zones' current packets in these protocols (bracelet::PROTOCOL_BIT set) as delivered.
    void mark_zones_sent_(uint32_t now, uint8_t protocols);
    void apply_input_(uint32_t offset, const uint8_t *data, size_t len, bool frame_end, uint32_t src_ip,
                      InputSource source);
    void update_wants_(uint32_t now);
    bool pick_job_(uint32_t now, Job &job);
    bool zone_changed_(uint8_t zone, uint8_t protocol) const;
    bool held_back_(uint8_t zone, uint8_t protocol) const;  // a broader overlapping zone must be sent first
    void take_lane_(uint8_t zone, uint8_t protocol, uint32_t now);  // record the lane's packet as sent
    void transmit_(const Job &job);
    bool wait_for_clear_channel_(int8_t threshold);  // false if it gave up (busy too long)
    void tick_stats_(uint32_t now);

    Radio *radio_{nullptr};
    OokSender sender_;
    RadioState radio_state_{RadioState::INITIALIZING};
    uint32_t next_init_ms_{0};
    uint32_t tuned_freq_{0};
    int8_t tuned_power_{0};
    bool tuned_{false};

    uint8_t channels_[MAX_CHANNELS]{};
    ZoneState zones_[MAX_ZONES];
    std::deque<Job> manual_;
    uint8_t rr_{0};  // round robin over lanes: zone * NUM_PROTOCOLS + protocol

    struct TxLogEntry {
        uint32_t ms;
        uint8_t protocol, repeats;
        uint8_t packet[bracelet::PACKET_LEN];
        bool manual, ok;
    };
    static const uint8_t TX_LOG_SIZE = 24;
    TxLogEntry tx_log_[TX_LOG_SIZE]{};
    uint8_t tx_log_head_{0}, tx_log_count_{0};
    uint32_t tx_log_total_{0};

    void sample_rssi_();
    volatile bool rssi_request_{false};
    volatile uint32_t rssi_seq_{0};
    bool rssi_ok_{false};
    int16_t rssi_peak_{-127}, rssi_avg_{-127};
    uint32_t rssi_freq_{0};
    // Base layer (protocol 0, AppConfig::base_layer): see p0_apply_base_layer(). Set by update_wants_().
    int8_t base_zone_{-1};     // the all-groups zone, or -1 when layering is not in effect
    uint16_t follows_{0};      // zones currently following the base (bit per zone)
    bool config_dirty_{true};
    bool output_was_enabled_{true};
    bool input_dirty_{false};
    volatile bool suspended_{false};

    TestMode test_mode_{TestMode::OFF};
    uint8_t test_rgb_[3]{};
    uint8_t test_last_rgb_[3]{};
    bool test_sent_{false};        // broadcast test colour already queued (protocol 1)
    bool blank_broadcast_{false};  // input timeout: send one broadcast "off" after update_wants_
    bool hold_after_update_{false};  // all_off(): treat the next computed colours as already sent

    InputStats in_;
    OutputStats out_;
    uint32_t stats_window_ms_{0};
    uint32_t frames_in_window_{0}, packets_in_window_{0};
    uint32_t airtime_us_[10]{};
    uint8_t airtime_slot_{0};
    volatile int16_t lbt_last_rssi_{-127};  // strongest signal seen in the latest listen (dBm)
    volatile bool lbt_last_busy_{false};

    // Receiver mode (AppConfig::receiver): the radio listens and drives pins::RADIO_DATA with the demodulated
    // signal; an edge interrupt times it and the engine task decodes the frames. Nothing is transmitted.
    void start_rx_(uint32_t freq, uint8_t profile);
    void stop_rx_();
    void poll_rx_(uint32_t now);
    void store_burst_();
    void rx_json_(JsonObject o, uint32_t now);
    bool rx_active_{false};
    uint32_t rx_freq_{0};
    uint8_t rx_profile_{0};
    uint32_t rx_retry_ms_{0};
    bool rx_failed_{false};
    bracelet::FrameDecoder decoder_;
    bracelet::RxTracker tracker_;
    // Raw capture: bursts of plausible pulses, kept whether or not they decoded (undecoded ones are kept longest).
    static const uint8_t RX_CAPTURES = 4;
    bracelet::BurstSegmenter segmenter_;
    bracelet::BurstStore<RX_CAPTURES> captures_;
    bool burst_decoded_{false};
    int16_t burst_rssi_{-127};
    uint32_t captures_quiet_{0};  // bursts dropped because the channel was no louder than its noise floor
    uint32_t rx_edges_{0};
    uint32_t rx_storm_win_ms_{0}, rx_storm_edges_{0}, rx_storms_{0};
    uint32_t rx_paused_until_{0};  // edge interrupt detached (noise storm) until then; 0 = attached
    uint32_t rx_last_rssi_ms_{0};
    int32_t rx_rssi_sum_{0};
    int16_t rx_rssi_peak_win_{-127};
    uint16_t rx_rssi_n_{0};
    uint32_t rx_rssi_win_ms_{0};
    int16_t rx_rssi_avg_{-127}, rx_rssi_peak_{-127};
    struct RxLogEntry {
        uint32_t ms;
        uint8_t protocol;
        uint8_t packet[bracelet::PACKET_LEN];
        uint8_t copies;  // frames heard of this transmission
        int16_t rssi_dbm;
    };
    static const uint8_t RX_LOG_SIZE = 24;
    RxLogEntry rx_log_[RX_LOG_SIZE]{};
    uint8_t rx_log_head_{0}, rx_log_count_{0};
};

extern Engine g_engine;
