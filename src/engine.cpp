#include "engine.h"
#include "input_parsers.h"

#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/semphr.h>
#include <hal/gpio_ll.h>

#include "pins.h"

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

// ---------------------------------------------------------------------------------------------
// Receiver mode: edge capture
// ---------------------------------------------------------------------------------------------
// The interrupt runs on the engine task's core and only timestamps edges into a ring; decoding happens in
// the engine task. Each entry is (duration_us << 1) | level, the level being the one that just ended.
namespace {
constexpr uint32_t RX_RING = 1024;  // power of two
volatile uint32_t s_rx_ring[RX_RING];
volatile uint32_t s_rx_head = 0, s_rx_tail = 0, s_rx_dropped = 0;
volatile int64_t s_rx_last_us = 0;

void IRAM_ATTR rx_edge_isr() {
    int64_t now = esp_timer_get_time();
    uint32_t level = gpio_ll_get_level(&GPIO, pins::RADIO_DATA) ? 1 : 0;
    int64_t d = now - s_rx_last_us;
    s_rx_last_us = now;
    uint32_t dur = d > 0x00FFFFFF ? 0x00FFFFFF : (uint32_t) d;
    uint32_t head = s_rx_head;
    if (head - s_rx_tail >= RX_RING) {
        s_rx_dropped = s_rx_dropped + 1;
        return;
    }
    s_rx_ring[head & (RX_RING - 1)] = (dur << 1) | (level ^ 1);
    s_rx_head = head + 1;
}
}  // namespace

static const uint32_t RX_RETRY_MS = 3000;
static const uint32_t RX_RSSI_EVERY_MS = 10;
static const uint32_t RX_RSSI_WINDOW_MS = 500;
// Noise guard: a receiver with no signal can toggle its data line very fast, and an interrupt per edge would
// starve the web server on this core. Bracelet frames need under 5000 edges/s; above this, pause briefly.
static const uint32_t RX_STORM_WINDOW_MS = 100;
static const uint32_t RX_STORM_EDGES = 2500;  // per window (25 000 edges/s)
static const uint32_t RX_STORM_PAUSE_MS = 50;

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
        for (ZoneLane &l : zs.lane)
            l.have_want = false;  // set again below for each protocol the zone drives
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
            // Protocol 1 only (zone_live() leaves protocol 0 out in this mode).
            zs.gated = this->channels_[base] != VENDOR_BOOT_CODE;
            zs.group = this->channels_[base + 1];
            zs.r = this->channels_[base + 2];
            zs.g = this->channels_[base + 3];
            zs.b = this->channels_[base + 4];
            zs.fx = 0;
            if (zs.gated || !zone_live(g_app, i, 1))
                continue;
            uint8_t addr[4] = {zs.group, zc.p1_addr[1], 0, 0};
            build_packet(1, addr, ACTION_COLOR, zs.r, zs.g, zs.b, g_app.off_threshold, zs.lane[1].want);
            zs.lane[1].have_want = true;
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
        for (uint8_t p = 0; p < NUM_PROTOCOLS; p++) {
            if (!zone_live(g_app, i, p))
                continue;  // a protocol the zone doesn't use: no packet, no airtime
            Action action = p == 1 ? p1_action(fx_action(zs.fx)) : fx_action(zs.fx);
            build_packet(p, zone_addr(zc, p), action, zs.r, zs.g, zs.b, g_app.off_threshold, zs.lane[p].want);
            zs.lane[p].have_want = true;
        }
    }

    // Base layer (protocol 0 lanes): zones showing their own colour are cut out of the All Zones address, black
    // zones follow it.
    this->base_zone_ = -1;
    this->follows_ = 0;
    if (g_app.base_layer && protocol_in_use(g_app, 0)) {
        uint8_t wants[MAX_ZONES][PACKET_LEN];
        uint8_t addrs[MAX_ZONES][4];
        bool active[MAX_ZONES];
        for (uint8_t i = 0; i < g_app.num_zones; i++) {
            active[i] = this->zones_[i].lane[0].have_want;
            memcpy(wants[i], this->zones_[i].lane[0].want, PACKET_LEN);
            memcpy(addrs[i], g_app.zones[i].addr, 4);
        }
        uint16_t mask;
        this->base_zone_ = p0_apply_base_layer(wants, addrs, active, g_app.num_zones, &this->follows_, &mask);
        if (this->base_zone_ >= 0) {
            for (uint8_t i = 0; i < g_app.num_zones; i++)
                if (active[i])
                    memcpy(this->zones_[i].lane[0].want, wants[i], PACKET_LEN);
            if (mask == 0) {  // every group has a zone with its own colour: nobody left for the base to address
                ZoneLane &base = this->zones_[this->base_zone_].lane[0];
                memcpy(base.sent, base.want, PACKET_LEN);
                base.have_sent = true;
            }
        }
    }

    // Test mode, protocol 1 zones: one broadcast packet per colour change instead of one per zone. Protocol 0 zones
    // are still sent zone by zone.
    if (this->test_mode_ != TestMode::OFF && this->can_broadcast_()) {
        const uint8_t *rgb = this->test_mode_ == TestMode::SOLID ? this->test_rgb_ : cycle;
        if (!this->test_sent_ || memcmp(rgb, this->test_last_rgb_, 3) != 0) {
            this->queue_broadcast_(ACTION_COLOR, rgb[0], rgb[1], rgb[2]);
            memcpy(this->test_last_rgb_, rgb, 3);
            this->test_sent_ = true;
        }
        this->mark_zones_sent_(now, PROTOCOL_BIT[1]);  // the broadcast covers every protocol 1 zone
    }
}

void Engine::queue_broadcast_(Action action, uint8_t r, uint8_t g, uint8_t b) {
    // Called with StateLock held. Protocol 1, group 0 = all groups; byte 6 as normally sent (FF).
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

void Engine::mark_zones_sent_(uint32_t now, uint8_t protocols) {
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        for (uint8_t p = 0; p < NUM_PROTOCOLS; p++) {
            ZoneLane &l = this->zones_[i].lane[p];
            if (!has_protocol(protocols, p) || !l.have_want)
                continue;
            memcpy(l.sent, l.want, PACKET_LEN);
            l.have_sent = true;
            l.last_tx_ms = now;
        }
    }
}

bool Engine::zone_changed_(uint8_t i, uint8_t p) const {
    const ZoneLane &l = this->zones_[i].lane[p];
    if (!l.have_want)
        return false;
    if (!l.have_sent)
        return true;
    // The base zone's address shrinks and grows as other zones take and release their groups; only a new
    // colour (command bytes) is a reason to transmit it again.
    if (p == 0 && i == this->base_zone_)
        return l.want[0] != l.sent[0] || memcmp(l.want + 4, l.sent + 4, 2) != 0;
    return memcmp(l.want, l.sent, PACKET_LEN) != 0;
}

// When zones that reach the same bracelets change together, the broader address (All Zones) must go out first
// so the more specific colour lands last. A changed zone therefore waits while a broader, overlapping zone of the
// same protocol is also waiting to be sent. Zones that don't overlap keep taking turns.
bool Engine::held_back_(uint8_t i, uint8_t p) const {
    const uint8_t *mine = this->zones_[i].lane[p].want;
    uint8_t breadth = address_breadth(p, mine);
    for (uint8_t j = 0; j < g_app.num_zones; j++) {
        if (j == i || !this->zone_changed_(j, p))
            continue;
        const uint8_t *other = this->zones_[j].lane[p].want;
        if (address_breadth(p, other) > breadth && addresses_overlap(p, mine, other))
            return true;
    }
    return false;
}

void Engine::take_lane_(uint8_t i, uint8_t p, uint32_t now) {
    ZoneLane &l = this->zones_[i].lane[p];
    memcpy(l.sent, l.want, PACKET_LEN);
    l.have_sent = true;
    l.last_tx_ms = now;
    this->zones_[i].tx_count++;
}

bool Engine::pick_job_(uint32_t now, Job &job) {
    if (!this->manual_.empty()) {
        job = this->manual_.front();
        this->manual_.pop_front();
        return true;
    }
    // Lanes in the order zone 0 / protocol 0, zone 0 / protocol 1, zone 1 / protocol 0, ... taking turns.
    uint8_t n = g_app.num_zones, lanes = (uint8_t) (n * NUM_PROTOCOLS);
    // Pass 0: lanes whose packet changed (newest state wins). Pass 1: periodic refresh.
    for (int pass = 0; pass < 2; pass++) {
        for (uint8_t k = 0; k < lanes; k++) {
            uint8_t idx = (uint8_t) ((this->rr_ + k) % lanes), i = idx / NUM_PROTOCOLS, p = idx % NUM_PROTOCOLS;
            // For one change, a zone on both protocols sends protocol 0 first, then protocol 1.
            if (pass == 0 && p == 1 && this->zone_changed_(i, 0) && !this->held_back_(i, 0))
                idx = (uint8_t) (i * NUM_PROTOCOLS), p = 0;
            ZoneLane &l = this->zones_[i].lane[p];
            if (!l.have_want)
                continue;
            bool changed = this->zone_changed_(i, p);
            bool refresh = g_app.refresh_ms > 0 && now - l.last_tx_ms >= g_app.refresh_ms;
            if (pass == 0 ? !changed || this->held_back_(i, p) : !refresh)
                continue;
            job.protocol = p;
            memcpy(job.packet, l.want, PACKET_LEN);
            job.repeats = g_app.repeats;
            job.manual = false;
            this->take_lane_(i, p, now);
            this->rr_ = (uint8_t) ((idx + 1) % lanes);
            if (p == 0 && pass == 0 && g_app.base_layer && i != this->base_zone_) {
                // One packet per colour, not per zone: every other protocol 0 zone waiting to send this same
                // command joins this transmission through the group mask.
                for (uint8_t j = 0; j < n; j++) {
                    const ZoneLane &other = this->zones_[j].lane[0];
                    if (j == i || j == this->base_zone_ || !this->zone_changed_(j, 0) || this->held_back_(j, 0) ||
                        !p0_can_merge(job.packet, other.want))
                        continue;
                    p0_merge(job.packet, other.want);
                    this->take_lane_(j, 0, now);
                }
            }
            if (p == 0 && i == this->base_zone_) {
                // The broadcast also reached every zone that follows the base: they now show its colour.
                for (uint8_t j = 0; j < n; j++) {
                    if (!(this->follows_ >> j & 1))
                        continue;
                    ZoneLane &f = this->zones_[j].lane[0];
                    memcpy(f.sent, f.want, PACKET_LEN);
                    f.have_sent = true;
                    f.last_tx_ms = now;
                }
            }
            return true;
        }
    }
    return false;
}

void Engine::try_init_radio_() {
    this->stop_rx_();
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
    TxLogEntry &entry = this->tx_log_[this->tx_log_head_];
    entry.ms = millis();
    entry.protocol = job.protocol;
    entry.repeats = job.repeats;
    memcpy(entry.packet, job.packet, PACKET_LEN);
    entry.manual = job.manual;
    entry.ok = ok;
    this->tx_log_head_ = (this->tx_log_head_ + 1) % TX_LOG_SIZE;
    if (this->tx_log_count_ < TX_LOG_SIZE)
        this->tx_log_count_++;
    this->tx_log_total_++;
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
                this->stop_rx_();
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

        // Receiver mode: listen instead of transmitting.
        bool want_rx;
        uint32_t rx_freq;
        uint8_t rx_profile;
        {
            StateLock lock;
            want_rx = g_app.receiver;
            rx_profile = g_app.rx_profile;
            rx_freq = (uint32_t) (((uint64_t) g_app.freq[0] + g_app.freq[1]) / 2);  // hears both protocols
        }
        want_rx = want_rx && this->radio_state_ == RadioState::READY && !this->suspended_ && this->radio_->rx_supported();
        if (this->rx_active_ && (!want_rx || rx_freq != this->rx_freq_ || rx_profile != this->rx_profile_))
            this->stop_rx_();
        if (!want_rx)
            this->rx_failed_ = false;
        else if (!this->rx_active_ && (int32_t) (now - this->rx_retry_ms_) >= 0)
            this->start_rx_(rx_freq, rx_profile);
        if (this->rx_active_)
            this->poll_rx_(now);

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
                    zs.resend();  // push new addressing/protocols out immediately
                this->tuned_ = false;
            }
            if (this->config_dirty_ || this->input_dirty_ || cycle_step)
                this->update_wants_(now);
            this->config_dirty_ = this->input_dirty_ = false;
            if (this->blank_broadcast_) {
                // One broadcast "off" blanks every protocol 1 group, including ones no zone is configured for.
                // Protocol 0 zones are blanked zone by zone (their colours have just gone to black).
                this->blank_broadcast_ = false;
                if (tx_allowed(g_app)) {
                    this->queue_broadcast_(ACTION_OFF, 0, 0, 0);
                    this->mark_zones_sent_(now, PROTOCOL_BIT[1]);
                }
            }
            if (this->hold_after_update_) {
                // After all_off(): don't re-send the unchanged input colours over the "off".
                this->hold_after_update_ = false;
                this->mark_zones_sent_(now, ALL_PROTOCOLS);
            }

            bool enabled = tx_allowed(g_app);
            if (enabled != this->output_was_enabled_) {
                log_i("RF output %s", enabled ? "enabled" : "disabled");
                if (enabled) {
                    for (auto &zs : this->zones_)
                        zs.resend();  // bring bracelets up to date immediately
                } else {
                    this->manual_.clear();
                }
                this->output_was_enabled_ = enabled;
            }
            if (!enabled) {
                // Consume ("eat") updates without transmitting, so they are counted but not queued.
                for (uint8_t i = 0; i < g_app.num_zones; i++) {
                    for (ZoneLane &l : this->zones_[i].lane) {
                        if (!l.have_want)
                            continue;
                        if (!l.have_sent || memcmp(l.want, l.sent, PACKET_LEN) != 0) {
                            memcpy(l.sent, l.want, PACKET_LEN);
                            l.have_sent = true;
                            this->out_.suppressed++;
                        }
                    }
                }
            } else if (this->radio_state_ == RadioState::READY && !this->suspended_) {
                have_job = this->pick_job_(now, job);
            }
        }

        if (have_job)
            this->transmit_(job);
        else if (this->rssi_request_)
            this->sample_rssi_();
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
    // Built-in effects exist on protocol 0 only: they aren't sent to a zone's protocol 1 bracelets.
    bool effect = action == ACTION_FX_A || action == ACTION_FX_B || action == ACTION_FX_C;
    // "All zones" on protocol 1: one packet to every group instead of one per zone.
    bool p1_broadcast = zone < 0 && !effect && this->can_broadcast_();
    if (p1_broadcast) {
        this->queue_broadcast_(action, r, g, b);
        if (!this->manual_.empty())
            this->manual_.back().manual = true;
    }
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        const ZoneConfig &zc = g_app.zones[i];
        if (zone >= 0 ? zone != i : !zc.enabled)
            continue;
        for (uint8_t p = 0; p < NUM_PROTOCOLS; p++) {  // protocol 0, then protocol 1
            if (!zone_uses(zc, p) || (p == 0 && g_app.mode == MODE_VENDOR) || (p == 1 && (effect || p1_broadcast)))
                continue;
            if (this->manual_.size() >= MAX_MANUAL_QUEUE)
                return true;
            Job job{};
            job.protocol = p;
            job.repeats = g_app.repeats;
            job.manual = true;
            build_packet(p, zone_addr(zc, p), action, r, g, b, g_app.off_threshold, job.packet);
            this->manual_.push_back(job);
        }
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
    // Each protocol 0 zone gets an "off"; the protocol 1 zones share one broadcast to every group. A protocol that
    // no zone uses gets nothing.
    for (uint8_t i = 0; i < g_app.num_zones; i++) {
        if (!zone_live(g_app, i, 0) || this->manual_.size() >= MAX_MANUAL_QUEUE)
            continue;
        Job job{};
        job.protocol = 0;
        job.repeats = g_app.repeats;
        job.manual = true;
        build_packet(0, g_app.zones[i].addr, ACTION_OFF, 0, 0, 0, g_app.off_threshold, job.packet);
        this->manual_.push_back(job);
    }
    if (this->can_broadcast_()) {
        this->queue_broadcast_(ACTION_OFF, 0, 0, 0);
        if (!this->manual_.empty())
            this->manual_.back().manual = true;
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
            zs.resend();  // put the real (input-driven) colours back
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
    this->tracker_.clear();
    this->decoder_.reset_counts();
    this->captures_.clear();
    this->captures_quiet_ = 0;
    this->rx_edges_ = 0;
    this->rx_storms_ = 0;
    s_rx_dropped = 0;
    this->rx_log_head_ = this->rx_log_count_ = 0;
}

static void hex_into(char *out, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        sprintf(out + i * 2, "%02X", data[i]);
}

// Signal meter (Tools page). Called in the engine task while nothing is being transmitted.
void Engine::sample_rssi_() {
    bool ok = false;
    int16_t peak = -127;
    int32_t sum = 0;
    uint32_t samples = 0, freq = 0;
    if (this->rx_active_) {
        // Already listening (receiver mode): report the running measurement instead of leaving RX.
        StateLock lock;
        this->rssi_ok_ = this->rx_rssi_avg_ > -127;
        this->rssi_peak_ = this->rx_rssi_peak_;
        this->rssi_avg_ = this->rx_rssi_avg_;
        this->rssi_freq_ = this->rx_freq_;
        this->rssi_request_ = false;
        this->rssi_seq_ = this->rssi_seq_ + 1;
        return;
    }
    if (this->radio_state_ == RadioState::READY && this->radio_->lbt_supported()) {
        int8_t power;
        {
            StateLock lock;
            // The channel of the protocol in use (the default protocol's when both or neither are).
            uint8_t p = protocol_in_use(g_app, 0) == protocol_in_use(g_app, 1) ? g_app.protocol & 1
                        : protocol_in_use(g_app, 1)                          ? 1
                                                                             : 0;
            freq = g_app.freq[p];
            power = constrain(g_app.tx_power, this->radio_->min_power(), this->radio_->max_power());
        }
        bool tuned = this->tuned_ && freq == this->tuned_freq_ && power == this->tuned_power_;
        if (!tuned && this->radio_->tune(freq, power)) {
            this->tuned_freq_ = freq;
            this->tuned_power_ = power;
            this->tuned_ = tuned = true;
        }
        if (tuned && this->radio_->listen_on()) {
            delayMicroseconds(LBT_SETTLE_US);
            uint32_t t0 = micros();
            while (micros() - t0 < 20000) {
                int16_t r = this->radio_->rssi_dbm();
                if (r > peak)
                    peak = r;
                sum += r;
                samples++;
                delayMicroseconds(200);
            }
            this->radio_->listen_off();
            ok = samples > 0;
        }
    }
    StateLock lock;
    this->rssi_ok_ = ok;
    this->rssi_peak_ = peak;
    this->rssi_avg_ = samples ? (int16_t) (sum / (int32_t) samples) : -127;
    this->rssi_freq_ = freq;
    this->rssi_request_ = false;
    this->rssi_seq_ = this->rssi_seq_ + 1;
}

bool Engine::measure_rssi(int16_t &peak_dbm, int16_t &avg_dbm, uint32_t &freq_hz) {
    uint32_t seq = this->rssi_seq_;
    this->rssi_request_ = true;
    for (int i = 0; i < 100 && this->rssi_seq_ == seq; i++)  // a transmission in progress finishes first
        delay(5);
    if (this->rssi_seq_ == seq) {
        this->rssi_request_ = false;
        return false;
    }
    StateLock lock;
    peak_dbm = this->rssi_peak_;
    avg_dbm = this->rssi_avg_;
    freq_hz = this->rssi_freq_;
    return this->rssi_ok_;
}

void Engine::tools_json(JsonObject o) {
    StateLock lock;
    uint32_t now = millis();
    o["tx_total"] = this->tx_log_total_;
    JsonArray tx = o["tx"].to<JsonArray>();
    for (uint8_t k = 0; k < this->tx_log_count_; k++) {
        const TxLogEntry &e = this->tx_log_[(this->tx_log_head_ + TX_LOG_SIZE - 1 - k) % TX_LOG_SIZE];
        JsonObject j = tx.add<JsonObject>();
        char hex[PACKET_LEN * 2 + 1];
        hex_into(hex, e.packet, PACKET_LEN);
        j["age_ms"] = now - e.ms;
        j["p"] = e.protocol;
        j["pkt"] = hex;
        j["n"] = e.repeats;
        j["manual"] = e.manual;
        j["ok"] = e.ok;
    }
    JsonObject in = o["input"].to<JsonObject>();
    uint8_t width = zone_width(g_app);
    in["start"] = g_app.start_channel;
    in["width"] = width;
    in["seen"] = this->in_.seen;
    in["age_ms"] = this->in_.seen ? (int32_t) (now - this->in_.last_rx_ms) : -1;
    JsonArray ch = in["channels"].to<JsonArray>();
    uint16_t first = g_app.start_channel - 1, count = (uint16_t) g_app.num_zones * width;
    for (uint16_t i = 0; i < count && first + i < MAX_CHANNELS; i++)
        ch.add(this->channels_[first + i]);
}

void Engine::status_json(JsonObject o) {
    StateLock lock;
    uint32_t now = millis();

    o["role"] = g_app.receiver ? "receiver" : "controller";
    this->rx_json_(o["receiver"].to<JsonObject>(), now);

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
        // The last packet sent on each protocol the zone drives; "packet" is the first of them (older clients).
        char pkt[PACKET_LEN * 2 + 1] = "";
        JsonArray packets = j["packets"].to<JsonArray>();
        for (uint8_t p = 0; p < NUM_PROTOCOLS; p++) {
            const ZoneLane &l = gs.lane[p];
            if (!l.have_sent || !zone_live(g_app, i, p))
                continue;
            hex_into(pkt, l.sent, PACKET_LEN);
            JsonObject e = packets.add<JsonObject>();
            e["p"] = p;
            e["pkt"] = pkt;
            if (j["packet"].isNull())
                j["packet"] = pkt;
        }
        if (j["packet"].isNull())
            j["packet"] = "";
        j["tx"] = gs.tx_count;
        j["tx_age_ms"] = gs.have_sent() ? (int32_t) (now - gs.last_tx_ms()) : -1;
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
    s.output_enabled = g_app.output_enabled && !g_app.receiver;  // a receiver never transmits
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

// ---------------------------------------------------------------------------------------------
// Receiver mode
// ---------------------------------------------------------------------------------------------

void Engine::start_rx_(uint32_t freq, uint8_t profile) {
    this->sender_.end();  // the radio drives the data line from here
    bool ok = this->radio_->tune(freq, this->radio_->min_power()) && this->radio_->rx_data_on(profile);
    this->tuned_ = false;  // the next transmission re-tunes
    if (!ok) {
        log_w("%s could not start receiving", this->radio_->name());
        this->radio_->rx_data_off();
        this->sender_.begin();
        this->rx_retry_ms_ = millis() + RX_RETRY_MS;
        this->rx_failed_ = true;
        return;
    }
    this->decoder_.reset();
    this->segmenter_.reset();  // (not by assignment: a temporary would put 2 KB on this task's stack)
    this->burst_decoded_ = false;
    this->burst_rssi_ = -127;
    s_rx_tail = s_rx_head;
    s_rx_last_us = esp_timer_get_time();
    attachInterrupt(pins::RADIO_DATA, rx_edge_isr, CHANGE);
    this->rx_active_ = true;
    this->rx_failed_ = false;
    this->rx_paused_until_ = 0;
    this->rx_storm_win_ms_ = millis();
    this->rx_storm_edges_ = 0;
    this->rx_freq_ = freq;
    this->rx_profile_ = profile;
    this->rx_rssi_win_ms_ = millis();
    log_i("Receiver listening on %lu Hz, %s profile (%lu kHz)", (unsigned long) freq,
          RX_PROFILE_NAMES[profile < NUM_RX_PROFILES ? profile : 0],
          (unsigned long) (this->radio_->rx_bandwidth(profile) / 1000));
}

void Engine::stop_rx_() {
    if (!this->rx_active_)
        return;
    if (!this->rx_paused_until_)
        detachInterrupt(pins::RADIO_DATA);
    this->rx_paused_until_ = 0;
    this->radio_->rx_data_off();
    this->sender_.begin();
    this->rx_active_ = false;
    this->tuned_ = false;
    StateLock lock;
    this->rx_rssi_avg_ = this->rx_rssi_peak_ = -127;
    log_i("Receiver stopped");
}

void Engine::poll_rx_(uint32_t now) {
    uint32_t tail = s_rx_tail, head = s_rx_head;
    this->rx_edges_ += head - tail;
    this->rx_storm_edges_ += head - tail;
    if (this->rx_paused_until_) {
        if ((int32_t) (now - this->rx_paused_until_) >= 0) {
            this->rx_paused_until_ = 0;
            this->decoder_.reset();
            s_rx_last_us = esp_timer_get_time();
            attachInterrupt(pins::RADIO_DATA, rx_edge_isr, CHANGE);
        }
    } else if (now - this->rx_storm_win_ms_ >= RX_STORM_WINDOW_MS) {
        if (this->rx_storm_edges_ > RX_STORM_EDGES) {
            detachInterrupt(pins::RADIO_DATA);
            this->rx_paused_until_ = (now + RX_STORM_PAUSE_MS) | 1;
            this->rx_storms_++;
        }
        this->rx_storm_win_ms_ = now;
        this->rx_storm_edges_ = 0;
    }
    bracelet::RxFrame f;
    while (tail != head) {
        uint32_t v = s_rx_ring[tail & (RX_RING - 1)];
        tail++;
        if (this->segmenter_.push(v & 1, v >> 1))
            this->store_burst_();
        if (!this->decoder_.push(v & 1, v >> 1, f))
            continue;
        this->burst_decoded_ = true;
        // The transmitter usually sends the frame several times back to back, so the carrier is likely still
        // up: a fair reading of how strong this transmitter is here.
        int16_t rssi = this->radio_->rssi_dbm();
        uint32_t ms = millis();
        StateLock lock;
        bool fresh = this->tracker_.apply(f, ms, rssi);
        RxLogEntry *e = this->rx_log_count_ ? &this->rx_log_[(this->rx_log_head_ + RX_LOG_SIZE - 1) % RX_LOG_SIZE]
                                            : nullptr;
        if (!fresh && e) {
            e->copies++;
            e->ms = ms;
        } else {
            e = &this->rx_log_[this->rx_log_head_];
            e->ms = ms;
            e->protocol = f.protocol;
            memcpy(e->packet, f.pkt, PACKET_LEN);
            e->copies = 1;
            e->rssi_dbm = rssi;
            this->rx_log_head_ = (this->rx_log_head_ + 1) % RX_LOG_SIZE;
            if (this->rx_log_count_ < RX_LOG_SIZE)
                this->rx_log_count_++;
        }
    }
    s_rx_tail = tail;

    if (this->segmenter_.open()) {
        // A burst is coming in: track its strength, and end it if the line has gone quiet.
        int16_t r = this->radio_->rssi_dbm();
        if (r > this->burst_rssi_)
            this->burst_rssi_ = r;
        int64_t quiet = esp_timer_get_time() - s_rx_last_us;
        if (s_rx_tail == s_rx_head && this->segmenter_.idle(quiet > 0x7FFFFFFF ? 0x7FFFFFFF : (uint32_t) quiet))
            this->store_burst_();
    }

    if (now - this->rx_last_rssi_ms_ >= RX_RSSI_EVERY_MS) {
        this->rx_last_rssi_ms_ = now;
        int16_t r = this->radio_->rssi_dbm();
        this->rx_rssi_sum_ += r;
        this->rx_rssi_n_++;
        if (r > this->rx_rssi_peak_win_)
            this->rx_rssi_peak_win_ = r;
    }
    if (now - this->rx_rssi_win_ms_ >= RX_RSSI_WINDOW_MS && this->rx_rssi_n_) {
        StateLock lock;
        this->rx_rssi_avg_ = (int16_t) (this->rx_rssi_sum_ / this->rx_rssi_n_);
        this->rx_rssi_peak_ = this->rx_rssi_peak_win_;
        this->rx_rssi_sum_ = 0;
        this->rx_rssi_n_ = 0;
        this->rx_rssi_peak_win_ = -127;
        this->rx_rssi_win_ms_ = now;
    }
}

void Engine::store_burst_() {
    // A burst that never rose above the channel's noise floor is the receiver chattering on noise.
    bool quiet = this->rx_rssi_avg_ > -127 && this->burst_rssi_ < this->rx_rssi_avg_ + 4;
    {
        StateLock lock;
        if (quiet)
            this->captures_quiet_++;
        else
            this->captures_.add(this->segmenter_, millis(), this->burst_decoded_, this->burst_rssi_);
    }
    this->burst_decoded_ = false;
    this->burst_rssi_ = -127;
}

bool Engine::rx_capture_json(uint32_t id, JsonObject o) {
    StateLock lock;
    const bracelet::RawBurst *b = this->captures_.find(id);
    if (!b)
        return false;
    o["id"] = b->id;
    o["age_ms"] = millis() - b->end_ms;
    o["freq"] = this->rx_freq_;
    o["decoded"] = b->decoded;
    o["truncated"] = b->truncated;
    o["rssi_dbm"] = b->rssi_dbm;
    o["us"] = b->total_us;
    JsonArray p = o["pulses"].to<JsonArray>();  // + mark, - space, microseconds
    for (uint16_t i = 0; i < b->count; i++)
        p.add(b->pulses[i]);
    return true;
}

bool Engine::rx_capture_ook(uint32_t id, String &out) {
    StateLock lock;
    const bracelet::RawBurst *b = this->captures_.find(id);
    if (!b)
        return false;
    out.reserve(160 + b->count * 6);
    bracelet::export_ook(*b, this->rx_freq_, [&](const char *t) { out += t; });
    return true;
}

static String group_name(uint8_t group) {
    if (group == bracelet::RxTracker::ALL_GROUPS)
        return "all";
    return String(group);
}

void Engine::rx_json_(JsonObject o, uint32_t now) {
    // Called with StateLock held.
    o["enabled"] = (bool) g_app.receiver;
    o["active"] = this->rx_active_;
    o["supported"] = this->radio_->rx_supported();
    o["error"] = this->rx_failed_;
    o["freq"] = this->rx_active_ ? this->rx_freq_ : (uint32_t) (((uint64_t) g_app.freq[0] + g_app.freq[1]) / 2);
    o["profile"] = RX_PROFILE_NAMES[g_app.rx_profile < NUM_RX_PROFILES ? g_app.rx_profile : 0];
    o["bandwidth"] = this->radio_->rx_bandwidth(g_app.rx_profile);
    o["frames"] = this->tracker_.frames();
    o["bad"] = this->decoder_.bad();
    o["updates"] = this->tracker_.updates();
    o["edges"] = this->rx_edges_;
    o["dropped"] = (uint32_t) s_rx_dropped;
    o["noise_pauses"] = this->rx_storms_;  // edge storms (no usable signal) that paused listening briefly
    o["rssi_dbm"] = this->rx_rssi_avg_;
    o["rssi_peak_dbm"] = this->rx_rssi_peak_;
    o["last_age_ms"] = this->tracker_.have_last() ? (int32_t) (now - this->tracker_.last_ms()) : -1;
    char hex[PACKET_LEN * 2 + 1], rgb[7];
    JsonArray zones = o["zones"].to<JsonArray>();
    for (uint8_t i = 0; i < this->tracker_.count(); i++) {
        const bracelet::RxZone &z = this->tracker_.zone(i);
        JsonObject j = zones.add<JsonObject>();
        j["p"] = z.protocol;
        j["group"] = group_name(z.group);
        uint8_t c[3] = {z.r, z.g, z.b};
        hex_into(rgb, c, 3);
        j["rgb"] = rgb;
        if (z.label)
            j["label"] = z.label;
        hex_into(hex, z.pkt, PACKET_LEN);
        j["pkt"] = hex;
        j["updates"] = z.updates;
        j["age_ms"] = now - z.last_ms;
        j["rssi_dbm"] = z.rssi_dbm;
    }
    o["captures_quiet"] = this->captures_quiet_;
    JsonArray caps = o["captures"].to<JsonArray>();  // newest first; pulses via /api/rx/capture?id=
    for (uint8_t k = 0; k < this->captures_.size(); k++) {
        const bracelet::RawBurst *newest = nullptr;
        for (uint8_t i = 0; i < this->captures_.size(); i++) {
            const bracelet::RawBurst &b = this->captures_.at(i);
            uint32_t seen = k ? caps[k - 1]["id"].as<uint32_t>() : UINT32_MAX;
            if (b.id < seen && (!newest || b.id > newest->id))
                newest = &b;
        }
        if (!newest)
            break;
        JsonObject j = caps.add<JsonObject>();
        j["id"] = newest->id;
        j["age_ms"] = now - newest->end_ms;
        j["pulses"] = newest->count;
        j["us"] = newest->total_us;
        j["decoded"] = newest->decoded;
        j["truncated"] = newest->truncated;
        j["rssi_dbm"] = newest->rssi_dbm;
    }
    JsonArray log = o["log"].to<JsonArray>();
    for (uint8_t k = 0; k < this->rx_log_count_; k++) {
        const RxLogEntry &e = this->rx_log_[(this->rx_log_head_ + RX_LOG_SIZE - 1 - k) % RX_LOG_SIZE];
        JsonObject j = log.add<JsonObject>();
        hex_into(hex, e.packet, PACKET_LEN);
        j["age_ms"] = now - e.ms;
        j["p"] = e.protocol;
        j["pkt"] = hex;
        j["n"] = e.copies;
        j["rssi_dbm"] = e.rssi_dbm;
        if (e.protocol == 1)
            j["checksum"] = bracelet::p1_checksum_kind(e.packet) == bracelet::P1_CK_LEGACY ? "legacy" : "vendor";
    }
}

void Engine::rx_snapshot(ReceiverSnapshot &s) {
    StateLock lock;
    uint32_t now = millis();
    s.enabled = g_app.receiver;
    s.active = this->rx_active_;
    s.supported = this->radio_->rx_supported();
    s.freq_hz = this->rx_freq_;
    s.frames = this->tracker_.frames();
    s.bad = this->decoder_.bad();
    s.updates = this->tracker_.updates();
    s.rssi_dbm = this->rx_rssi_avg_;
    s.last_age_ms = this->tracker_.have_last() ? (int32_t) (now - this->tracker_.last_ms()) : -1;
    s.count = this->tracker_.count();
    for (uint8_t i = 0; i < s.count; i++) {
        const bracelet::RxZone &z = this->tracker_.zone(i);
        s.rows[i] = {z.protocol, z.group, z.r, z.g, z.b, z.label, now - z.last_ms};
    }
}
