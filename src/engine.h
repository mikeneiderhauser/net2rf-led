#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <deque>

#include "bracelet_protocol.h"
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

enum class RadioState : uint8_t { INITIALIZING, READY, NOT_DETECTED };
enum class TestMode : uint8_t { OFF, SOLID, CYCLE };

const char *radio_state_name(RadioState s);

struct ZoneState {
    uint8_t r{0}, g{0}, b{0}, fx{0};
    bool gated{false};  // vendor mode: boot-code channel != 85, so nothing is transmitted
    uint8_t group{0};   // vendor mode: group code taken from the input
    uint8_t want[bracelet::PACKET_LEN]{};
    uint8_t sent[bracelet::PACKET_LEN]{};
    bool have_want{false};
    bool have_sent{false};
    uint32_t last_tx_ms{0};
    uint32_t tx_count{0};
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
    EngineSnapshot snapshot();
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
    // Protocol 1 has a confirmed "all groups" address (group 0), so fan-out actions can be one packet.
    bool can_broadcast_() const { return g_app.protocol == 1; }
    void queue_broadcast_(bracelet::Action action, uint8_t r, uint8_t g, uint8_t b);
    void mark_zones_sent_(uint32_t now);
    void apply_input_(uint32_t offset, const uint8_t *data, size_t len, bool frame_end, uint32_t src_ip,
                      InputSource source);
    void update_wants_(uint32_t now);
    bool pick_job_(uint32_t now, Job &job);
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
    uint8_t rr_{0};
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
};

extern Engine g_engine;
