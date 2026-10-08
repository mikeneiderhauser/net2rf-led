#pragma once

// Persistent board records (no Arduino deps, host-testable): a log of boots and crashes, a table of the
// transmitters heard in receiver mode, and a few saved raw captures. Each is a small JSON file written
// through the Storage interface below; on the controller that is LittleFS on the 128 KB data partition
// (src/store.cpp), in the tests a map in memory. Being plain JSON, every file can be downloaded and read as is.
//
// A file that is missing, not JSON or of another version reads as "empty": nothing here ever fails a boot.

#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace boardstore {

struct Storage {
    virtual ~Storage() = default;
    virtual bool read(const char *name, std::string &out) = 0;
    virtual bool write(const char *name, const std::string &data) = 0;
    virtual bool remove(const char *name) = 0;
};

static const int FORMAT = 1;  // "v" in every file

inline bool load_doc(Storage &s, const char *name, JsonDocument &doc) {
    std::string text;
    doc.clear();
    if (!s.read(name, text) || deserializeJson(doc, text) != DeserializationError::Ok || doc["v"] != FORMAT) {
        doc.clear();
        return false;
    }
    return true;
}

inline bool save_doc(Storage &s, const char *name, JsonDocument &doc) {
    std::string text;
    doc["v"] = FORMAT;
    serializeJson(doc, text);
    return s.write(name, text);
}

inline void hex32(uint32_t v, char out[11]) { snprintf(out, 11, "0x%08lx", (unsigned long) v); }

// ---------------------------------------------------------------------------------------------
// Boot log (/boots.json): one entry per boot, newest kept. Says why the controller restarted and on which
// firmware, so a run of crashes or brownouts is visible after the fact.
// ---------------------------------------------------------------------------------------------

inline bool boot_abnormal(const char *reason) {
    return !strcmp(reason, "crash") || !strcmp(reason, "watchdog") || !strcmp(reason, "brownout");
}

struct BootEntry {
    uint32_t boot_no{0};
    std::string reason;         // "power on", "software restart", "crash", "watchdog", "brownout", ...
    std::string fw;             // firmware version that booted
    uint32_t prev_uptime_s{0};  // how long the run before this boot lasted (0 = unknown)
    uint32_t pc{0};             // crash address from the core dump, when there is one
    uint32_t skipped{0};        // abnormal restarts just before this one that were not logged (boot loop: see src/store.cpp)
};

class BootLog {
 public:
    static const uint8_t CAPACITY = 32;
    static constexpr const char *PATH = "/boots.json";

    void load(Storage &s) {
        this->entries_.clear();
        this->boots_ = 0;
        JsonDocument doc;
        if (!load_doc(s, PATH, doc))
            return;
        this->boots_ = doc["boots"] | 0u;
        for (JsonObject o : doc["entries"].as<JsonArray>()) {
            if (this->entries_.size() >= CAPACITY)
                break;
            BootEntry e;
            e.boot_no = o["n"] | 0u;
            e.reason = o["reason"] | "other";
            e.fw = o["fw"] | "";
            e.prev_uptime_s = o["prev_uptime_s"] | 0u;
            e.pc = (uint32_t) strtoul(o["pc"] | "0", nullptr, 16);
            e.skipped = o["skipped"] | 0u;
            this->entries_.push_back(e);
        }
    }

    // Records this boot (the oldest entry drops out when full) and writes the file. Returns the boot number.
    // `skipped` restarts that were not logged still count as boots.
    uint32_t record(Storage &s, const char *reason, const char *fw, uint32_t prev_uptime_s, uint32_t pc,
                    uint32_t skipped = 0) {
        BootEntry e;
        this->boots_ += skipped;
        e.skipped = skipped;
        e.boot_no = ++this->boots_;
        e.reason = reason ? reason : "other";
        e.fw = fw ? fw : "";
        e.prev_uptime_s = prev_uptime_s;
        e.pc = pc;
        if (this->entries_.size() >= CAPACITY)
            this->entries_.erase(this->entries_.begin());
        this->entries_.push_back(e);
        this->save(s);
        return e.boot_no;
    }

    // The same object the file holds: {boots, abnormal_streak, entries: [{n, reason, fw, prev_uptime_s, pc?}]}.
    void to_json(JsonObject o) const {
        o["boots"] = this->boots_;
        o["abnormal_streak"] = this->abnormal_streak();
        JsonArray a = o["entries"].to<JsonArray>();
        for (const BootEntry &e : this->entries_) {
            JsonObject j = a.add<JsonObject>();
            j["n"] = e.boot_no;
            j["reason"] = e.reason;
            j["fw"] = e.fw;
            j["prev_uptime_s"] = e.prev_uptime_s;
            if (e.pc) {
                char hex[11];
                hex32(e.pc, hex);
                j["pc"] = hex;
            }
            if (e.skipped)
                j["skipped"] = e.skipped;
        }
    }

    bool save(Storage &s) const {
        JsonDocument doc;
        this->to_json(doc.to<JsonObject>());
        return save_doc(s, PATH, doc);
    }

    void clear(Storage &s) {  // the boot counter carries on
        this->entries_.clear();
        this->save(s);
    }

    void reset(Storage &s) {  // the counter too
        this->entries_.clear();
        this->boots_ = 0;
        s.remove(PATH);
    }

    const std::vector<BootEntry> &entries() const { return this->entries_; }  // oldest first
    uint32_t boots() const { return this->boots_; }
    // How many of the newest entries in a row were abnormal: 3 or more is a boot loop.
    uint8_t abnormal_streak() const {
        uint8_t n = 0;
        for (size_t i = this->entries_.size(); i-- > 0 && boot_abnormal(this->entries_[i].reason.c_str());)
            n++;
        return n;
    }

 private:
    std::vector<BootEntry> entries_;
    uint32_t boots_{0};
};

// ---------------------------------------------------------------------------------------------
// Seen on air (/seen.json): which transmitters receiver mode has decoded, by protocol and address. Kept in
// memory and written when asked (the caller rate-limits that: flash wears).
// ---------------------------------------------------------------------------------------------

struct SeenEntry {
    uint8_t protocol{0};
    uint8_t addr[4]{};  // protocol 0: packet bytes 0-3; protocol 1: group code, then zeros
    uint32_t count{0};  // transmissions heard
    uint32_t first_boot{0}, last_boot{0};
    int16_t best_rssi{-127};
};

class SeenTable {
 public:
    static const uint8_t CAPACITY = 64;
    static constexpr const char *PATH = "/seen.json";

    // The address part of a 7-byte device packet, as stored.
    static void address_of(uint8_t protocol, const uint8_t *packet, uint8_t out[4]) {
        memset(out, 0, 4);
        if (protocol == 0)
            memcpy(out, packet, 4);
        else
            out[0] = packet[1];
    }

    void load(Storage &s) {
        this->entries_.clear();
        this->dirty_ = false;
        JsonDocument doc;
        if (!load_doc(s, PATH, doc))
            return;
        for (JsonObject o : doc["entries"].as<JsonArray>()) {
            if (this->entries_.size() >= CAPACITY)
                break;
            SeenEntry e;
            e.protocol = o["protocol"] | 0;
            uint32_t a = (uint32_t) strtoul(o["address"] | "0", nullptr, 16);
            for (int i = 0; i < 4; i++)
                e.addr[i] = (uint8_t) (a >> (24 - 8 * i));
            e.count = o["count"] | 0u;
            e.first_boot = o["first_boot"] | 0u;
            e.last_boot = o["last_boot"] | 0u;
            e.best_rssi = o["best_rssi_dbm"] | -127;
            this->entries_.push_back(e);
        }
    }

    // One transmission heard. Returns true when it is a transmitter not in the table yet.
    bool note(uint8_t protocol, const uint8_t *packet, int16_t rssi_dbm, uint32_t boot_no) {
        uint8_t a[4];
        address_of(protocol, packet, a);
        this->dirty_ = true;
        for (SeenEntry &e : this->entries_) {
            if (e.protocol == protocol && memcmp(e.addr, a, 4) == 0) {
                if (e.count != UINT32_MAX)
                    e.count++;
                e.last_boot = boot_no;
                if (rssi_dbm > e.best_rssi)
                    e.best_rssi = rssi_dbm;
                return false;
            }
        }
        SeenEntry *slot;
        if (this->entries_.size() < CAPACITY) {
            this->entries_.emplace_back();
            slot = &this->entries_.back();
        } else {  // full: the one heard longest ago makes room, the rarest of those first
            slot = &this->entries_[0];
            for (SeenEntry &e : this->entries_)
                if (e.last_boot < slot->last_boot || (e.last_boot == slot->last_boot && e.count < slot->count))
                    slot = &e;
        }
        slot->protocol = protocol;
        memcpy(slot->addr, a, 4);
        slot->count = 1;
        slot->first_boot = slot->last_boot = boot_no;
        slot->best_rssi = rssi_dbm;
        return true;
    }

    // {entries: [{protocol, address (8 hex digits), group? (protocol 1), count, first_boot, last_boot, best_rssi_dbm}]}
    void to_json(JsonObject o) const {
        JsonArray arr = o["entries"].to<JsonArray>();
        for (const SeenEntry &e : this->entries_) {
            JsonObject j = arr.add<JsonObject>();
            char hex[9];
            snprintf(hex, sizeof(hex), "%02X%02X%02X%02X", e.addr[0], e.addr[1], e.addr[2], e.addr[3]);
            j["protocol"] = e.protocol;
            j["address"] = hex;
            if (e.protocol != 0)
                j["group"] = e.addr[0];
            j["count"] = e.count;
            j["first_boot"] = e.first_boot;
            j["last_boot"] = e.last_boot;
            j["best_rssi_dbm"] = e.best_rssi;
        }
    }

    bool dirty() const { return this->dirty_; }
    void mark_clean() { this->dirty_ = false; }  // for a caller that serialises to_json() and writes the file itself
    bool save(Storage &s) {
        JsonDocument doc;
        this->to_json(doc.to<JsonObject>());
        bool ok = save_doc(s, PATH, doc);
        if (ok)
            this->dirty_ = false;
        return ok;
    }
    void clear(Storage &s) {
        this->entries_.clear();
        this->dirty_ = false;
        s.remove(PATH);
    }
    const std::vector<SeenEntry> &entries() const { return this->entries_; }

 private:
    std::vector<SeenEntry> entries_;
    bool dirty_{false};
};

// ---------------------------------------------------------------------------------------------
// Saved captures (/cap0.json ... /cap5.json): raw bursts the user chose to keep (receiver mode only holds the
// last few in RAM). One file each, in numbered slots; the same shape as GET /api/rx/capture.
// ---------------------------------------------------------------------------------------------

struct CaptureInfo {
    uint32_t freq_hz{0};
    uint32_t total_us{0};
    uint32_t boot_no{0};
    uint16_t count{0};  // pulses
    int16_t rssi_dbm{-127};
    bool decoded{false}, truncated{false};
    std::string note;
};

class CaptureShelf {
 public:
    static const uint8_t SLOTS = 6;  // a full 1024-pulse burst is ~6 KB of JSON
    static const uint16_t MAX_PULSES = 1024;
    static const size_t MAX_NOTE = 40;

    static void file_name(uint8_t slot, char out[16]) { snprintf(out, 16, "/cap%u.json", (unsigned) slot); }

    static bool used(Storage &s, uint8_t slot) {
        JsonDocument doc;
        return read(s, slot, doc);
    }

    // Saves into the first free slot. Returns the slot, or -1 when all are taken or the write failed.
    static int save(Storage &s, const CaptureInfo &info, const int16_t *pulses) {
        if (info.count == 0 || info.count > MAX_PULSES)
            return -1;
        for (uint8_t slot = 0; slot < SLOTS; slot++) {
            if (used(s, slot))
                continue;
            JsonDocument doc;
            doc["slot"] = slot;
            doc["freq"] = info.freq_hz;
            doc["us"] = info.total_us;
            doc["boot"] = info.boot_no;
            doc["decoded"] = info.decoded;
            doc["truncated"] = info.truncated;
            doc["rssi_dbm"] = info.rssi_dbm;
            doc["note"] = info.note.substr(0, MAX_NOTE);
            JsonArray p = doc["pulses"].to<JsonArray>();  // + mark, - space, microseconds
            for (uint16_t i = 0; i < info.count; i++)
                p.add(pulses[i]);
            char name[16];
            file_name(slot, name);
            return save_doc(s, name, doc) ? slot : -1;
        }
        return -1;
    }

    // The stored document of one slot. False if the slot is empty.
    static bool read(Storage &s, uint8_t slot, JsonDocument &doc) {
        if (slot >= SLOTS)
            return false;
        char name[16];
        file_name(slot, name);
        return load_doc(s, name, doc) && doc["pulses"].is<JsonArray>();
    }

    static bool remove(Storage &s, uint8_t slot) {
        if (slot >= SLOTS)
            return false;
        char name[16];
        file_name(slot, name);
        return s.remove(name);
    }
};

}  // namespace boardstore
