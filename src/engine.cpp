#include "engine.h"
#include "input_parsers.h"

#include <esp_random.h>
#include <freertos/semphr.h>

using namespace bracelet;

Engine g_engine;

static SemaphoreHandle_t s_lock = nullptr;

void state_lock_init() {
    if (s_lock == nullptr)
        s_lock = xSemaphoreCreateRecursiveMutex();
}
StateLock::StateLock() { xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
StateLock::~StateLock() { xSemaphoreGiveRecursive(s_lock); }

const char *radio_state_name(RadioState s) {
    switch (s) {
        case RadioState::READY:
            return "ready";
        case RadioState::NOT_DETECTED:
            return "not_detected";
        case RadioState::OFF:
            return "off";
        default:
            return "initializing";
    }
}

static const uint32_t RADIO_RETRY_MS = 10000;
static const uint8_t MAX_MANUAL_QUEUE = 32;
static const uint32_t TEST_CYCLE_MS = 2000;
// Listen before transmit: sample the channel for longer than the longest gap inside a bracelet frame
// (1.6 ms after a protocol 1 sync pulse), so a frame already on the air can't slip between samples.
static const uint32_t LBT_WINDOW_US = 2500;
static const uint32_t LBT_SETTLE_US = 300;    // RX start-up / RSSI settling after entering RX
static const uint32_t LBT_SAMPLE_US = 100;
static const uint32_t LBT_MAX_WAIT_MS = 250;  // never hold an update longer than this: send anyway

void Engine::begin() {
    this->radio_ = create_radio((RadioType) g_app.radio_type);
    this->sender_.begin();
    // Radio init, scheduling and (blocking) RF transmission all run on this task so the web UI
    // and network stay responsive while the radio is probed or busy.
    xTaskCreatePinnedToCore(&Engine::task_entry_, "engine", 6144, this, 3, nullptr, 1);
}

void Engine::task_entry_(void *arg) { static_cast<Engine *>(arg)->run_(); }

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

void Engine::apply_input_(uint32_t offset, const uint8_t *data, size_t len, bool frame_end, uint32_t src_ip,
                          InputSource source) {
    // Called with StateLock held.
    this->in_.packets++;
    this->packets_in_window_++;
    this->in_.last_src_ip = src_ip;
    this->in_.last_source = source;
    if (source == InputSource::DDP)
        this->in_.ddp_packets++;
    else
        this->in_.e131_packets++;
    if (frame_end) {
        this->in_.frames++;
        this->frames_in_window_++;
    }
    if (offset >= MAX_CHANNELS) {
        this->in_.ignored++;
        this->in_.last_bad_offset = offset;
        this->in_.last_bad_offset_ms = millis() | 1;
        return;
    }
    // Only usable data counts as input: otherwise a stream of out-of-range packets would keep the input
    // timeout from ever blanking the bracelets.
    this->in_.last_rx_ms = millis();
    this->in_.seen = true;
    size_t n = std::min<size_t>(len, MAX_CHANNELS - offset);
    memcpy(&this->channels_[offset], data, n);
    if (this->in_.timed_out)
        log_i("Input resumed");
    this->in_.timed_out = false;
    this->input_dirty_ = true;
}

void Engine::on_ddp(const uint8_t *buf, size_t len, uint32_t src_ip) {
    StateLock lock;
    this->in_.bytes += len;
    InputFrame f;
    switch (parse_ddp(buf, len, f)) {
        case ParseResult::MALFORMED:
            this->in_.malformed++;
            return;
        case ParseResult::IGNORED:
            this->in_.ignored++;
            return;
        case ParseResult::OK:
            this->apply_input_(f.offset, f.data, f.len, f.frame_end, src_ip, InputSource::DDP);
    }
}

void Engine::on_e131(const uint8_t *buf, size_t len, uint32_t src_ip) {
    StateLock lock;
    this->in_.bytes += len;
    InputFrame f;
    switch (parse_e131(buf, len, g_app.e131_universe, f)) {
        case ParseResult::MALFORMED:
            this->in_.malformed++;
            return;
        case ParseResult::IGNORED:
            this->in_.ignored++;
            return;
        case ParseResult::OK:
            this->apply_input_(f.offset, f.data, f.len, f.frame_end, src_ip, InputSource::E131);
    }
}

// ---------------------------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------------------------

void Engine::update_wants_(uint32_t now) {
    uint8_t cycle[3] = {0, 0, 0};
    if (this->test_mode_ == TestMode::CYCLE) {
        static const uint8_t COLORS[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
        memcpy(cycle, COLORS[(now / TEST_CYCLE_MS) % 4], 3);
    }
    uint8_t width = zone_width(g_app);
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        const ZoneConfig &zc = g_app.zones[i];
        ZoneState &zs = this->zones_[i];
        if (!zc.enabled)
            continue;
        uint16_t base = zone_start_channel(g_app, i) - 1;
        if (base + width > MAX_CHANNELS)
            continue;
        zs.gated = false;
        if (this->test_mode_ == TestMode::SOLID) {
            zs.r = this->test_rgb_[0], zs.g = this->test_rgb_[1], zs.b = this->test_rgb_[2], zs.fx = 0;
        } else if (this->test_mode_ == TestMode::CYCLE) {
            zs.r = cycle[0], zs.g = cycle[1], zs.b = cycle[2], zs.fx = 0;
        } else if (g_app.mode == MODE_VENDOR) {
            // Vendor transmitter layout: [boot code][group][R][G][B]. Only transmit while the boot code is 85,
            // so sequences can switch the bracelets' transmitter on and off exactly as with the vendor hardware.
            zs.gated = this->channels_[base] != VENDOR_BOOT_CODE;
            zs.group = this->channels_[base + 1];
            zs.r = this->channels_[base + 2];
            zs.g = this->channels_[base + 3];
            zs.b = this->channels_[base + 4];
            zs.fx = 0;
            if (zs.gated) {
                zs.have_want = false;
                continue;
            }
            uint8_t addr[4] = {zs.group, zc.addr[1], 0, 0};
            build_packet(1, addr, ACTION_COLOR, zs.r, zs.g, zs.b, g_app.off_threshold, zs.want);
            zs.have_want = true;
            continue;
        } else if (g_app.mode == MODE_DMX) {
            zs.r = this->channels_[base];
            zs.g = this->channels_[base + 1];
            zs.b = this->channels_[base + 2];
            zs.fx = this->channels_[base + 3];
        } else {
            extract_rgb(g_app.color_order, &this->channels_[base], &zs.r, &zs.g, &zs.b);
            zs.fx = 0;
        }
        build_packet(g_app.protocol, zc.addr, fx_action(zs.fx), zs.r, zs.g, zs.b, g_app.off_threshold, zs.want);
        zs.have_want = true;
    }

    // Test mode on protocol 1: one broadcast packet per colour change instead of one per zone.
    if (this->test_mode_ != TestMode::OFF && this->can_broadcast_()) {
        const uint8_t *rgb = this->test_mode_ == TestMode::SOLID ? this->test_rgb_ : cycle;
        if (!this->test_sent_ || memcmp(rgb, this->test_last_rgb_, 3) != 0) {
            this->queue_broadcast_(ACTION_COLOR, rgb[0], rgb[1], rgb[2]);
            memcpy(this->test_last_rgb_, rgb, 3);
            this->test_sent_ = true;
        }
        this->mark_zones_sent_(now);  // the broadcast covers every zone
    }
}

void Engine::queue_broadcast_(Action action, uint8_t r, uint8_t g, uint8_t b) {
    // Called with StateLock held. Group 0 = all groups; byte 6 as normally sent (FF).
    static const uint8_t ALL_GROUPS[4] = {0x00, 0xFF, 0x00, 0x00};
    if (!tx_allowed(g_app) || this->manual_.size() >= MAX_MANUAL_QUEUE)
        return;
    Job job{};
    job.protocol = 1;
    job.repeats = g_app.repeats;
    job.manual = false;
    build_packet(1, ALL_GROUPS, action, r, g, b, g_app.off_threshold, job.packet);
    this->manual_.push_back(job);
}

// Treat every zone's current state as delivered (a broadcast already carried it).
void Engine::mark_zones_sent_(uint32_t now) {
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        ZoneState &zs = this->zones_[i];
        if (!g_app.zones[i].enabled || !zs.have_want)
            continue;
        memcpy(zs.sent, zs.want, PACKET_LEN);
        zs.have_sent = true;
        zs.last_tx_ms = now;
    }
}

bool Engine::zone_changed_(uint8_t i) const {
    const ZoneState &zs = this->zones_[i];
    return g_app.zones[i].enabled && zs.have_want && (!zs.have_sent || memcmp(zs.want, zs.sent, PACKET_LEN) != 0);
}

// When zones that reach the same bracelets change together, the broader address (All Zones) must go out first
// so the more specific colour lands last. A changed zone therefore waits while a broader, overlapping zone is
// also waiting to be sent. Zones that don't overlap keep taking turns.
bool Engine::held_back_(uint8_t i) const {
    const uint8_t *mine = this->zones_[i].want;
    uint8_t breadth = address_breadth(g_app.protocol, mine);
    for (uint8_t j = 0; j < g_app.num_zones; j++) {
        if (j == i || !this->zone_changed_(j))
            continue;
        const uint8_t *other = this->zones_[j].want;
        if (address_breadth(g_app.protocol, other) > breadth && addresses_overlap(g_app.protocol, mine, other))
            return true;
    }
    return false;
}

bool Engine::pick_job_(uint32_t now, Job &job) {
    if (!this->manual_.empty()) {
        job = this->manual_.front();
        this->manual_.pop_front();
        return true;
    }
    uint8_t n = g_app.num_zones;
    // Pass 0: zones whose packet changed (newest state wins). Pass 1: periodic refresh.
    for (int pass = 0; pass < 2; pass++) {
        for (uint8_t k = 0; k < n; k++) {
            uint8_t i = (this->rr_ + k) % n;
            ZoneState &zs = this->zones_[i];
            if (!g_app.zones[i].enabled || !zs.have_want)
                continue;
            bool changed = this->zone_changed_(i);
            bool refresh = g_app.refresh_ms > 0 && now - zs.last_tx_ms >= g_app.refresh_ms;
            if (pass == 0 ? !changed || this->held_back_(i) : !refresh)
                continue;
            job.protocol = g_app.protocol;
            memcpy(job.packet, zs.want, PACKET_LEN);
            job.repeats = g_app.repeats;
            job.manual = false;
            memcpy(zs.sent, zs.want, PACKET_LEN);
            zs.have_sent = true;
            zs.last_tx_ms = now;
            zs.tx_count++;
            this->rr_ = (i + 1) % n;
            return true;
        }
    }
    return false;
}

void Engine::try_init_radio_() {
    this->radio_state_ = RadioState::INITIALIZING;
    bool ok = this->radio_->init();
    this->sender_.begin();  // init() may have borrowed the data pin for its wiring check
    StateLock lock;
    this->radio_state_ = ok ? RadioState::READY : RadioState::NOT_DETECTED;
    this->tuned_ = false;
    this->next_init_ms_ = millis() + RADIO_RETRY_MS;
    if (!ok)
        log_w("%s not ready, retrying in %us", this->radio_->name(), RADIO_RETRY_MS / 1000);
}

void Engine::transmit_(const Job &job) {
    uint32_t freq, jitter;
    int8_t power, lbt_threshold;
    bool lbt;
    {
        StateLock lock;
        freq = g_app.freq[job.protocol];
        power = constrain(g_app.tx_power, this->radio_->min_power(), this->radio_->max_power());
        jitter = g_app.tx_jitter_ms;
        lbt = g_app.lbt_enabled && this->radio_->lbt_supported();
        lbt_threshold = g_app.lbt_threshold;
    }
    if (jitter > 0)
        delay(esp_random() % (jitter + 1));

    if (!this->tuned_ || freq != this->tuned_freq_ || power != this->tuned_power_) {
        if (!this->radio_->tune(freq, power)) {
            log_w("%s tune to %lu Hz failed", this->radio_->name(), (unsigned long) freq);
            StateLock lock;
            this->out_.errors++;
            this->radio_state_ = RadioState::NOT_DETECTED;  // re-probe
            this->next_init_ms_ = millis() + 1000;
            return;
        }
        this->tuned_freq_ = freq;
        this->tuned_power_ = power;
        this->tuned_ = true;
        log_d("Tuned %lu Hz, %d dBm", (unsigned long) freq, power);
    }

    if (lbt)
        this->wait_for_clear_channel_(lbt_threshold);

    uint32_t start = micros();
    bool ok = this->radio_->begin_tx() && this->sender_.send(job.protocol, job.packet, job.repeats);
    this->radio_->end_tx();
    uint32_t elapsed = micros() - start;

    StateLock lock;
    this->airtime_us_[this->airtime_slot_] += elapsed;
    if (ok) {
        this->out_.updates++;
        this->out_.frames += job.repeats;
        if (job.manual)
            this->out_.manual++;
    } else {
        this->out_.errors++;
    }
}

bool Engine::wait_for_clear_channel_(int8_t threshold) {
    uint32_t start = millis();
    bool waited = false, clear = false;
    for (;;) {
        if (!this->radio_->listen_on())
            break;  // couldn't enter RX: don't hold the update back
        int16_t peak = -127;
        delayMicroseconds(LBT_SETTLE_US);
        uint32_t t0 = micros();
        while (micros() - t0 < LBT_WINDOW_US) {
            int16_t r = this->radio_->rssi_dbm();
            if (r > peak)
                peak = r;
            delayMicroseconds(LBT_SAMPLE_US);
        }
        this->radio_->listen_off();
        this->lbt_last_rssi_ = peak;
        this->lbt_last_busy_ = peak >= threshold;
        if (peak < threshold) {
            clear = true;
            break;
        }
        waited = true;
        if (millis() - start >= LBT_MAX_WAIT_MS)
            break;
        delay(3 + esp_random() % 13);  // random back-off so two waiting controllers don't retry in lockstep
    }
    StateLock lock;
    this->out_.lbt_checks++;
    if (waited) {
        this->out_.lbt_waits++;
        this->out_.lbt_wait_ms += millis() - start;
    }
    if (!clear && waited)
        this->out_.lbt_forced++;
    return clear || !waited;
}

void Engine::tick_stats_(uint32_t now) {
    if (now - this->stats_window_ms_ < 1000)
        return;
    float secs = (now - this->stats_window_ms_) / 1000.0f;
    // xLights marks the last packet of each frame with PUSH; fall back to packets otherwise.
    uint32_t count = this->frames_in_window_ > 0 ? this->frames_in_window_ : this->packets_in_window_;
    this->in_.fps = count / secs;
    this->frames_in_window_ = this->packets_in_window_ = 0;
    this->stats_window_ms_ = now;

    uint64_t total = 0;
    for (uint32_t v : this->airtime_us_)
        total += v;
    this->out_.airtime_pct = total / 100000.0f;  // of 10 s
    this->airtime_slot_ = (this->airtime_slot_ + 1) % 10;
    this->airtime_us_[this->airtime_slot_] = 0;
}

void Engine::run_() {
    this->try_init_radio_();
    uint32_t last_cycle_step = 0;
    for (;;) {
        uint32_t now = millis();
        bool want_off;
        {
            StateLock lock;
            want_off = g_app.radio_off;
        }
        if (want_off) {
            if (this->radio_state_ != RadioState::OFF) {
                this->radio_->shutdown();
                StateLock lock;
                this->radio_state_ = RadioState::OFF;
                this->tuned_ = false;
                log_i("%s shut down", this->radio_->name());
            }
        } else if (this->radio_state_ == RadioState::OFF ||
                   (this->radio_state_ == RadioState::NOT_DETECTED && (int32_t) (now - this->next_init_ms_) >= 0)) {
            this->try_init_radio_();  // also wakes the chip after a shutdown
        }

        Job job;
        bool have_job = false;
        {
            StateLock lock;
            this->tick_stats_(now);

            uint32_t timeout_ms = (uint32_t) g_app.input_timeout_s * 1000;
            if (timeout_ms > 0 && this->in_.seen && !this->in_.timed_out && now - this->in_.last_rx_ms > timeout_ms) {
                log_i("No DDP for %us: blanking bracelets", g_app.input_timeout_s);
                memset(this->channels_, 0, sizeof(this->channels_));
                if (g_app.mode == MODE_VENDOR) {
                    // Keep each zone's boot code + group so the blank is actually transmitted.
                    for (uint8_t i = 0; i < g_app.num_zones; i++) {
                        uint16_t base = zone_start_channel(g_app, i) - 1;
                        if (base + 5 <= MAX_CHANNELS) {
                            this->channels_[base] = VENDOR_BOOT_CODE;
                            this->channels_[base + 1] = this->zones_[i].group;
                        }
                    }
                }
                this->in_.timed_out = true;
                this->input_dirty_ = true;
                this->blank_broadcast_ = this->can_broadcast_();
            }
            bool cycle_step = this->test_mode_ == TestMode::CYCLE && now / TEST_CYCLE_MS != last_cycle_step;
            if (cycle_step)
                last_cycle_step = now / TEST_CYCLE_MS;
            if (this->config_dirty_) {
                for (auto &zs : this->zones_)
                    zs.have_sent = false;  // push new addressing/protocol out immediately
                this->tuned_ = false;
            }
            if (this->config_dirty_ || this->input_dirty_ || cycle_step)
                this->update_wants_(now);
            this->config_dirty_ = this->input_dirty_ = false;
            if (this->blank_broadcast_) {
                // One broadcast "off" blanks every group, including ones no zone is configured for.
                this->blank_broadcast_ = false;
                if (tx_allowed(g_app)) {
                    this->queue_broadcast_(ACTION_OFF, 0, 0, 0);
                    this->mark_zones_sent_(now);
                }
            }
            if (this->hold_after_update_) {
                // After all_off(): don't re-send the unchanged input colours over the "off".
                this->hold_after_update_ = false;
                this->mark_zones_sent_(now);
            }

            bool enabled = tx_allowed(g_app);
            if (enabled != this->output_was_enabled_) {
                log_i("RF output %s", enabled ? "enabled" : "disabled");
                if (enabled) {
                    for (auto &zs : this->zones_)
                        zs.have_sent = false;  // bring bracelets up to date immediately
                } else {
                    this->manual_.clear();
                }
                this->output_was_enabled_ = enabled;
            }
            if (!enabled) {
                // Consume ("eat") updates without transmitting, so they are counted but not queued.
                for (uint8_t i = 0; i < g_app.num_zones; i++) {
                    ZoneState &zs = this->zones_[i];
                    if (!g_app.zones[i].enabled || !zs.have_want)
                        continue;
                    if (!zs.have_sent || memcmp(zs.want, zs.sent, PACKET_LEN) != 0) {
                        memcpy(zs.sent, zs.want, PACKET_LEN);
                        zs.have_sent = true;
                        this->out_.suppressed++;
                    }
                }
            } else if (this->radio_state_ == RadioState::READY && !this->suspended_) {
                have_job = this->pick_job_(now, job);
            }
        }

        if (have_job)
            this->transmit_(job);
        else
            vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// ---------------------------------------------------------------------------------------------
// Public controls
// ---------------------------------------------------------------------------------------------

bool Engine::send_raw(uint8_t protocol, const uint8_t *packet, uint8_t repeats, bool fix) {
    StateLock lock;
    if (!tx_allowed(g_app) || this->manual_.size() >= MAX_MANUAL_QUEUE)
        return false;
    Job job{};
    job.protocol = protocol ? 1 : 0;
    memcpy(job.packet, packet, PACKET_LEN);
    if (fix)
        fix_checksum(job.protocol, job.packet);
    job.repeats = constrain(repeats, 1, 20);
    job.manual = true;
    this->manual_.push_back(job);
    return true;
}

bool Engine::send_zone(int zone, Action action, uint8_t r, uint8_t g, uint8_t b) {
    StateLock lock;
    if (!tx_allowed(g_app) || this->manual_.size() >= MAX_MANUAL_QUEUE || zone >= (int) g_app.num_zones)
        return false;
    if (zone < 0 && this->can_broadcast_()) {  // "all zones" = one packet to every group
        this->queue_broadcast_(action, r, g, b);
        this->manual_.back().manual = true;
        return true;
    }
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        const ZoneConfig &zc = g_app.zones[i];
        if (zone >= 0 ? zone != i : !zc.enabled)
            continue;
        if (this->manual_.size() >= MAX_MANUAL_QUEUE)
            break;
        Job job{};
        job.protocol = g_app.protocol;
        job.repeats = g_app.repeats;
        job.manual = true;
        build_packet(g_app.protocol, zc.addr, action, r, g, b, g_app.off_threshold, job.packet);
        this->manual_.push_back(job);
    }
    return true;
}

bool Engine::all_off() {
    StateLock lock;
    if (!tx_allowed(g_app))
        return false;
    this->test_mode_ = TestMode::OFF;
    this->test_sent_ = false;
    this->manual_.clear();  // drop anything queued that would land after the "off"
    if (this->can_broadcast_()) {
        this->queue_broadcast_(ACTION_OFF, 0, 0, 0);
        if (!this->manual_.empty())
            this->manual_.back().manual = true;
    } else {
        for (uint8_t i = 0; i < g_app.num_zones; i++) {
            const ZoneConfig &zc = g_app.zones[i];
            if (!zc.enabled)
                continue;
            Job job{};
            job.protocol = g_app.protocol;
            job.repeats = g_app.repeats;
            job.manual = true;
            build_packet(g_app.protocol, zc.addr, ACTION_OFF, 0, 0, 0, g_app.off_threshold, job.packet);
            this->manual_.push_back(job);
        }
    }
    this->input_dirty_ = true;        // recompute the real (input) colours...
    this->hold_after_update_ = true;  // ...but treat them as already sent
    log_i("All off");
    return true;
}

void Engine::set_test(TestMode mode, uint8_t r, uint8_t g, uint8_t b) {
    StateLock lock;
    if (mode == TestMode::OFF && this->test_mode_ != TestMode::OFF) {
        for (auto &zs : this->zones_)
            zs.have_sent = false;  // put the real (input-driven) colours back
    }
    this->test_mode_ = mode;
    this->test_rgb_[0] = r, this->test_rgb_[1] = g, this->test_rgb_[2] = b;
    this->test_sent_ = false;
    this->input_dirty_ = true;
    log_i("Test mode %d", (int) mode);
}

void Engine::reset_stats() {
    StateLock lock;
    bool seen = this->in_.seen, timed_out = this->in_.timed_out;
    uint32_t last = this->in_.last_rx_ms;
    this->in_ = InputStats{};
    this->in_.seen = seen, this->in_.timed_out = timed_out, this->in_.last_rx_ms = last;
    this->out_ = OutputStats{};
    for (auto &zs : this->zones_)
        zs.tx_count = 0;
}

static void hex_into(char *out, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        sprintf(out + i * 2, "%02X", data[i]);
}

void Engine::status_json(JsonObject o) {
    StateLock lock;
    uint32_t now = millis();

    JsonObject radio = o["radio"].to<JsonObject>();
    radio["type"] = radio_type_name(g_app.radio_type);
    radio["name"] = this->radio_->name();
    radio["state"] = radio_state_name(this->radio_state_);
    radio["power"] = !g_app.radio_off;
    radio["detail"] = this->radio_->detail();
    radio["data_line"] = this->radio_->data_line();
    radio["min_power"] = this->radio_->min_power();
    radio["max_power"] = this->radio_->max_power();
    radio["queue"] = (uint32_t) this->manual_.size();
    JsonObject lbt = radio["lbt"].to<JsonObject>();
    lbt["enabled"] = (bool) g_app.lbt_enabled;
    lbt["supported"] = this->radio_->lbt_supported();
    lbt["threshold_dbm"] = g_app.lbt_threshold;
    lbt["last_rssi_dbm"] = (int) this->lbt_last_rssi_;
    lbt["last_busy"] = (bool) this->lbt_last_busy_;

    JsonObject in = o["input"].to<JsonObject>();
    in["packets"] = this->in_.packets;
    in["ddp_packets"] = this->in_.ddp_packets;
    in["e131_packets"] = this->in_.e131_packets;
    in["last_source"] = this->in_.last_source == InputSource::DDP    ? "ddp"
                        : this->in_.last_source == InputSource::E131 ? "e131"
                                                                     : "";
    in["frames"] = this->in_.frames;
    in["malformed"] = this->in_.malformed;
    in["ignored"] = this->in_.ignored;
    // Data aimed past our channels in the last 5 s: almost always xLights "Keep Channel Numbers" (DDP)
    // sending absolute show channel numbers, or a start channel beyond the universe.
    if (this->in_.last_bad_offset_ms && now - this->in_.last_bad_offset_ms < 5000)
        in["out_of_range_channel"] = this->in_.last_bad_offset + 1;
    in["bytes"] = (double) this->in_.bytes;
    in["fps"] = roundf(this->in_.fps * 10) / 10;
    in["seen"] = this->in_.seen;
    in["timed_out"] = this->in_.timed_out;
    in["age_ms"] = this->in_.seen ? (int32_t) (now - this->in_.last_rx_ms) : -1;
    in["source"] = this->in_.last_src_ip ? IPAddress(this->in_.last_src_ip).toString() : String("");
    in["timeout_s"] = g_app.input_timeout_s;

    JsonObject out = o["output"].to<JsonObject>();
    out["enabled"] = (bool) g_app.output_enabled;
    out["suppressed"] = this->out_.suppressed;
    out["updates"] = this->out_.updates;
    out["frames"] = this->out_.frames;
    out["errors"] = this->out_.errors;
    out["manual"] = this->out_.manual;
    out["airtime_pct"] = roundf(this->out_.airtime_pct * 10) / 10;
    out["lbt_checks"] = this->out_.lbt_checks;
    out["lbt_waits"] = this->out_.lbt_waits;
    out["lbt_forced"] = this->out_.lbt_forced;
    out["lbt_wait_ms"] = this->out_.lbt_wait_ms;

    static const char *const TEST_NAMES[] = {"off", "solid", "cycle"};
    JsonObject test = o["test"].to<JsonObject>();
    test["mode"] = TEST_NAMES[(int) this->test_mode_];
    char rgb[7];
    hex_into(rgb, this->test_rgb_, 3);
    test["rgb"] = rgb;

    JsonArray zones = o["zones"].to<JsonArray>();
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        const ZoneState &gs = this->zones_[i];
        JsonObject j = zones.add<JsonObject>();
        uint8_t c[3] = {gs.r, gs.g, gs.b};
        hex_into(rgb, c, 3);
        j["rgb"] = rgb;
        j["fx"] = gs.fx;
        char pkt[PACKET_LEN * 2 + 1] = "";
        if (gs.have_sent)
            hex_into(pkt, gs.sent, PACKET_LEN);
        j["packet"] = pkt;
        j["tx"] = gs.tx_count;
        j["tx_age_ms"] = gs.have_sent ? (int32_t) (now - gs.last_tx_ms) : -1;
        if (g_app.mode == MODE_VENDOR) {
            j["gated"] = gs.gated;
            j["group"] = gs.group;
        }
    }
}

EngineSnapshot Engine::snapshot() {
    StateLock lock;
    EngineSnapshot s{};
    s.radio_state = this->radio_state_;
    s.radio_name = this->radio_->name();
    s.fps = this->in_.fps;
    s.packets = this->in_.packets;
    s.input_seen = this->in_.seen;
    s.timed_out = this->in_.timed_out;
    s.test_active = this->test_mode_ != TestMode::OFF;
    s.output_enabled = g_app.output_enabled;
    s.last_source = this->in_.last_source;
    s.input_age_ms = this->in_.seen ? millis() - this->in_.last_rx_ms : 0;
    s.num_zones = g_app.num_zones;
    s.airtime_pct = this->out_.airtime_pct;
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        s.r[i] = this->zones_[i].r, s.g[i] = this->zones_[i].g, s.b[i] = this->zones_[i].b;
        s.enabled[i] = g_app.zones[i].enabled;
    }
    return s;
}
