#pragma once

// Raw pulse capture for receiver mode (no Arduino deps, host-testable). Cuts the demodulated OOK edge stream
// into bursts: runs of pulses with plausible remote-control timings, separated by long gaps or by the very short
// glitches a receiver produces on an empty channel. A burst is kept as a list of signed durations (+ mark,
// - space, microseconds), so a signal neither device protocol decodes can still be looked at, compared with
// rtl_433's pulse analyser (export_ook()), or added as a new protocol.

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rfproto {

class BurstSegmenter {
 public:
    static const uint16_t MAX_PULSES = 1024;  // ~ 7 device frames; longer bursts are truncated
    static const uint32_t MIN_US = 60;        // shorter: a receiver glitch, not a remote's pulse
    static const uint32_t GAP_US = 8000;      // longer: the burst is over
    static const uint16_t MIN_PULSES = 32;    // fewer: noise that happened to look plausible

    // Feed every level change, as for FrameDecoder::push(). Returns true when a burst has just ended: it is then
    // readable through pulses() / count() / truncated() until the next push().
    bool push(uint8_t level, uint32_t us) {
        if (this->done_) {
            this->clear_();
        }
        if (us < MIN_US || us > GAP_US) {
            bool ended = this->count_ >= MIN_PULSES;
            if (ended)
                this->done_ = true;
            else
                this->clear_();
            return ended;
        }
        if (this->count_ == 0 && !level)
            return false;  // a burst starts with a mark
        if (this->count_ < MAX_PULSES) {
            uint32_t v = us > 32767 ? 32767 : us;
            this->pulses_[this->count_++] = level ? (int16_t) v : (int16_t) -(int32_t) v;
        } else {
            this->truncated_ = true;
        }
        this->total_us_ += us;
        return false;
    }

    // Call when no edge has arrived for `quiet_us`: ends a burst whose last level has lasted too long.
    bool idle(uint32_t quiet_us) {
        if (this->done_ || quiet_us <= GAP_US || this->count_ == 0)
            return false;
        if (this->count_ >= MIN_PULSES) {
            this->done_ = true;
            return true;
        }
        this->clear_();
        return false;
    }

    bool open() const { return !this->done_ && this->count_ > 0; }
    void reset() { this->clear_(); }
    const int16_t *pulses() const { return this->pulses_; }
    uint16_t count() const { return this->count_; }
    bool truncated() const { return this->truncated_; }
    uint32_t total_us() const { return this->total_us_; }

 private:
    void clear_() {
        this->count_ = 0;
        this->truncated_ = false;
        this->done_ = false;
        this->total_us_ = 0;
    }

    int16_t pulses_[MAX_PULSES]{};
    uint16_t count_{0};
    bool truncated_{false};
    bool done_{false};
    uint32_t total_us_{0};
};

struct RawBurst {
    uint32_t id;
    uint32_t end_ms;
    uint32_t total_us;
    uint16_t count;
    bool truncated;
    bool decoded;      // at least one device frame was decoded from it
    int16_t rssi_dbm;  // strongest reading while it lasted
    int16_t pulses[BurstSegmenter::MAX_PULSES];
};

// The last few bursts. When full, a burst that decoded is dropped before one that didn't: those are the
// interesting ones.
template<uint8_t N> class BurstStore {
 public:
    RawBurst &add(const BurstSegmenter &s, uint32_t now_ms, bool decoded, int16_t rssi_dbm) {
        RawBurst *slot = nullptr;
        if (this->size_ < N) {
            slot = &this->slots_[this->size_++];
        } else {
            for (uint8_t pass = 0; pass < 2 && !slot; pass++) {  // oldest decoded first, then the oldest of all
                for (uint8_t i = 0; i < N; i++) {
                    RawBurst &b = this->slots_[i];
                    if ((pass == 1 || b.decoded) && (!slot || b.id < slot->id))
                        slot = &b;
                }
            }
        }
        slot->id = ++this->next_id_;
        slot->end_ms = now_ms;
        slot->total_us = s.total_us();
        slot->count = s.count();
        slot->truncated = s.truncated();
        slot->decoded = decoded;
        slot->rssi_dbm = rssi_dbm;
        memcpy(slot->pulses, s.pulses(), s.count() * sizeof(int16_t));
        return *slot;
    }
    uint8_t size() const { return this->size_; }
    const RawBurst &at(uint8_t i) const { return this->slots_[i]; }
    const RawBurst *find(uint32_t id) const {
        for (uint8_t i = 0; i < this->size_; i++)
            if (this->slots_[i].id == id)
                return &this->slots_[i];
        return nullptr;
    }
    void clear() { this->size_ = 0; }

 private:
    RawBurst slots_[N]{};
    uint8_t size_{0};
    uint32_t next_id_{0};
};

// rtl_433's OOK pulse-data text format (read with `rtl_433 -r capture.ook -A` to run its pulse analyser).
// Writes "mark space" pairs; a burst ending on a mark gets a 10 ms closing gap. `emit` is called per chunk of
// text. Returns the number of pulses written.
template<typename Emit> uint16_t export_ook(const RawBurst &b, uint32_t freq_hz, Emit emit) {
    char line[64];
    uint16_t pairs = 0;
    for (uint16_t i = 0; i < b.count; i++)
        pairs += b.pulses[i] > 0;
    emit(";pulse data\n;version 1\n;timescale 1us\n");
    snprintf(line, sizeof(line), ";freq1 %lu\n;ook %u pulses\n", (unsigned long) freq_hz, (unsigned) pairs);
    emit(line);
    for (uint16_t i = 0; i < b.count; i++) {
        if (b.pulses[i] <= 0)
            continue;  // a space without a mark before it (cannot happen: bursts start with a mark)
        int32_t space = (i + 1 < b.count && b.pulses[i + 1] < 0) ? -b.pulses[i + 1] : 10000;
        snprintf(line, sizeof(line), "%d %ld\n", b.pulses[i], (long) space);
        emit(line);
    }
    emit(";end\n");
    return pairs;
}

}  // namespace rfproto
