#include "store.h"

#include <LittleFS.h>
#include <Update.h>
#include <esp_core_dump.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <board_store.h>
#include <pulse_capture.h>

#include "engine.h"

namespace store {

static const char *const MOUNT = "/store";
static const char *const PARTITION = "spiffs";  // the data partition of min_spiffs.csv
static const char *const PREFS_PATH = "/store.json";

// Flash wear. The partition is 32 blocks of ~100,000 erases each and a small file costs about two, so there is
// room for roughly a million writes. Everything below is paced to stay far inside that for years:
//   boot log     one write per boot; none at all during a boot loop (BOOT_LOOP_AFTER)
//   seen table   only if the user switched recording on; a new transmitter within a minute, counts every 15
static const uint32_t SEEN_NEW_MS = 60000;
static const uint32_t SEEN_COUNTS_MS = 15 * 60000;
static const uint8_t BOOT_LOOP_AFTER = 5;      // abnormal restarts in a row before the log stops being written
static const uint32_t BOOT_STABLE_MS = 60000;  // ...until a boot has stayed up this long

static uint32_t s_writes = 0;  // file writes over the partition's life (stamped into every file as "w")

static bool s_mounted = false;  // the partition is usable (it is only actually mounted inside a Session)

// LittleFS costs a few KB of heap while mounted, and this firmware's secure downloads need every one of them.
// So the file system is mounted for the length of an operation and released again. Sessions nest; take one
// BEFORE the Guard below, never after (two tasks use both locks).
static SemaphoreHandle_t s_fs_mutex = nullptr;
static int s_fs_depth = 0;
struct Session {
    bool ok = false;
    bool held = false;
    Session() {
        if (!s_mounted || !s_fs_mutex)
            return;
        xSemaphoreTakeRecursive(s_fs_mutex, portMAX_DELAY);
        this->held = true;
        if (s_fs_depth == 0 && !LittleFS.begin(false, "/store", 4, "spiffs"))
            return;
        s_fs_depth++;
        this->ok = true;
    }
    ~Session() {
        if (!this->held)
            return;
        if (this->ok && --s_fs_depth == 0)
            LittleFS.end();
        xSemaphoreGiveRecursive(s_fs_mutex);
    }
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
};

// LittleFS behind the lib's Storage interface. A file is replaced by writing a temporary one and renaming it,
// so a power cut leaves either the old or the new contents.
class Flash : public boardstore::Storage {
 public:
    bool read(const char *name, std::string &out) override {
        out.clear();
        Session fs;
        if (!fs.ok)
            return false;
        File f = LittleFS.open(name, "r");
        if (!f || f.isDirectory())
            return false;
        size_t n = f.size();
        out.resize(n);
        size_t got = n ? f.read((uint8_t *) &out[0], n) : 0;
        f.close();
        return got == n;
    }
    bool write(const char *name, const std::string &data) override {
        Session fs;
        if (!fs.ok)
            return false;
        // Every file is a JSON object: stamp the running write count in as its first member.
        std::string text = data;
        if (!text.empty() && text[0] == '{')
            text.insert(1, "\"w\":" + std::to_string(s_writes + 1) + (text.size() > 2 ? "," : ""));
        String tmp = String(name) + ".tmp";
        File f = LittleFS.open(tmp, "w");
        if (!f)
            return false;
        size_t put = f.write((const uint8_t *) text.data(), text.size());
        f.close();
        if (put != text.size()) {
            LittleFS.remove(tmp);
            return false;
        }
        LittleFS.remove(name);
        if (!LittleFS.rename(tmp, name))
            return false;
        s_writes++;
        return true;
    }
    bool remove(const char *name) override {
        Session fs;
        return fs.ok && LittleFS.remove(name);
    }
};

static Flash s_flash;
static uint32_t s_boot_no = 0;
static boardstore::BootLog s_boots;
static boardstore::SeenTable s_seen;
static SemaphoreHandle_t s_mutex = nullptr;  // guards s_boots and s_seen; never held across a flash write
static bool s_record_seen = false;           // user setting, kept in PREFS_PATH
static bool s_seen_new = false;              // a transmitter not in the file yet
static uint32_t s_seen_written_ms = 0;
static bool s_boot_deferred = false;  // boot loop: this boot is logged once it has proved stable
static const char *s_boot_reason = "other";
static uint32_t s_boot_prev_uptime = 0, s_boot_pc = 0;

struct Guard {
    Guard() { xSemaphoreTake(s_mutex, portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(s_mutex); }
};

// RTC memory survives a crash, a watchdog and a software restart, but not a power cut (it then reads as
// garbage). Kept there: how long this run has lasted, and how many boot-loop restarts have gone unlogged.
// One block with a magic that names its layout and a check over every field, so neither garbage nor what
// another firmware version left there can pass for a count.
struct RtcState {
    uint32_t magic, uptime_s, skipped, check;
};
RTC_NOINIT_ATTR static RtcState s_rtc;
static const uint32_t RTC_MAGIC = 0x4E325332u;  // "N2S2": bump when RtcState changes
static uint32_t rtc_sum(const RtcState &r) { return (r.magic * 31u + r.uptime_s) * 31u + r.skipped + 0x9E3779B9u; }
static void rtc_seal() {
    s_rtc.magic = RTC_MAGIC;
    s_rtc.check = rtc_sum(s_rtc);
}

static const char *reset_reason_name() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "power on";
        case ESP_RST_SW:
            return "software restart";
        case ESP_RST_PANIC:
            return "crash";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:
            return "watchdog";
        case ESP_RST_BROWNOUT:
            return "brownout";
        case ESP_RST_EXT:
            return "reset button";
        default:
            return "other";
    }
}

// Where the last crash happened, from the core dump (0 if there is none). The dump stays until the next crash,
// so this is only asked for when this boot followed one.
static uint32_t crash_pc() {
    uint32_t pc = 0;
    esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *) malloc(sizeof(esp_core_dump_summary_t));
    if (sum && esp_core_dump_get_summary(sum) == ESP_OK)
        pc = sum->exc_pc;
    free(sum);
    return pc;
}

static uint32_t stamped_writes(const char *path) {
    std::string text;
    JsonDocument doc;
    if (!s_flash.read(path, text) || deserializeJson(doc, text) != DeserializationError::Ok)
        return 0;
    return doc["w"] | 0u;
}

static void rx_heard(uint8_t protocol, const uint8_t *packet, int16_t rssi_dbm) {
    note_seen(protocol, packet, rssi_dbm);
}

void begin() {
    s_mutex = xSemaphoreCreateMutex();
    // A power-on leaves nothing to trust there, whatever the bits happen to say.
    bool rtc_ok = s_rtc.magic == RTC_MAGIC && s_rtc.check == rtc_sum(s_rtc) && esp_reset_reason() != ESP_RST_POWERON;
    s_boot_prev_uptime = rtc_ok ? s_rtc.uptime_s : 0;
    uint32_t skipped = rtc_ok ? s_rtc.skipped : 0;
    s_rtc.uptime_s = 0;
    s_rtc.skipped = skipped;
    rtc_seal();

    // formatOnFail: a never-used (or SPIFFS-formatted) partition is formatted once, which takes a few seconds.
    s_fs_mutex = xSemaphoreCreateRecursiveMutex();
    s_mounted = LittleFS.begin(true, MOUNT, 4, PARTITION);
    if (s_mounted)
        LittleFS.end();
    Session fs;
    if (!s_mounted || !fs.ok) {
        s_mounted = false;
        log_w("Board store: no usable '%s' partition, nothing will be kept", PARTITION);
        return;
    }
    for (const char *path : {boardstore::BootLog::PATH, boardstore::SeenTable::PATH, PREFS_PATH})
        s_writes = max(s_writes, stamped_writes(path));
    JsonDocument prefs;
    if (boardstore::load_doc(s_flash, PREFS_PATH, prefs))
        s_record_seen = prefs["record_seen"] | false;
    {
        Guard g;
        s_boots.load(s_flash);
        s_seen.load(s_flash);
    }
    s_boot_reason = reset_reason_name();
    bool abnormal = boardstore::boot_abnormal(s_boot_reason);
    s_boot_pc = abnormal && strcmp(s_boot_reason, "brownout") != 0 ? crash_pc() : 0;
    if (abnormal && s_boots.abnormal_streak() >= BOOT_LOOP_AFTER) {
        // A boot loop would otherwise wear the flash out in months. Count this restart in RTC memory instead,
        // and log it (with how many were skipped) once a boot has stayed up for a minute: see loop().
        s_boot_deferred = true;
        s_boot_no = s_boots.boots() + skipped + 1;
        s_rtc.skipped = skipped + 1;  // this one included, in case it does not last either
        rtc_seal();
        log_w("Board store: restart loop (%u unlogged), not writing the boot log yet", (unsigned) skipped);
    } else {
        s_boot_no = s_boots.record(s_flash, s_boot_reason, FW_VERSION, s_boot_prev_uptime, s_boot_pc, skipped);
        s_rtc.skipped = 0;
        rtc_seal();
    }
    g_rx_heard_hook = rx_heard;
    log_i("Board store: boot %u (%s), %u of %u bytes used, %u writes so far", (unsigned) s_boot_no, s_boot_reason,
          (unsigned) LittleFS.usedBytes(), (unsigned) LittleFS.totalBytes(), (unsigned) s_writes);
}

bool mounted() { return s_mounted; }
uint32_t boot_no() { return s_boot_no; }
bool record_seen() { return s_record_seen; }

bool set_record_seen(bool on) {
    Session fs;
    if (!fs.ok)
        return false;
    if (on == s_record_seen)
        return true;
    JsonDocument prefs;
    prefs["record_seen"] = on;
    if (!boardstore::save_doc(s_flash, PREFS_PATH, prefs))
        return false;
    s_record_seen = on;
    s_seen_written_ms = millis();  // switched on: what has been heard so far is written at the next interval
    return true;
}

void note_seen(uint8_t protocol, const uint8_t *packet, int16_t rssi_dbm) {
    if (!s_mounted)
        return;
    Guard g;
    if (s_seen.note(protocol, packet, rssi_dbm, s_boot_no))
        s_seen_new = true;
}

// The seen table as file text, taken under the lock; written by the caller without it.
static bool seen_snapshot(std::string &text) {
    Guard g;
    if (!s_seen.dirty())
        return false;
    JsonDocument doc;
    s_seen.to_json(doc.to<JsonObject>());
    doc["v"] = boardstore::FORMAT;
    serializeJson(doc, text);
    s_seen.mark_clean();
    s_seen_new = false;
    return true;
}

static void flush_seen() {
    std::string text;
    if (seen_snapshot(text))
        s_flash.write(boardstore::SeenTable::PATH, text);
    s_seen_written_ms = millis();
}

void loop() {
    static uint32_t last_tick = 0;
    uint32_t now = millis();
    if (now - last_tick < 1000)
        return;
    last_tick = now;
    s_rtc.uptime_s = now / 1000;
    rtc_seal();
    if (!s_mounted || Update.isRunning())
        return;  // an upload is writing the new firmware
    if (s_boot_deferred && now >= BOOT_STABLE_MS) {
        s_boot_deferred = false;
        Session fs;
        Guard g;  // a ~2 KB file: short enough to write under the lock
        s_boot_no = s_boots.record(s_flash, s_boot_reason, FW_VERSION, s_boot_prev_uptime, s_boot_pc, s_rtc.skipped - 1);
        s_rtc.skipped = 0;
        rtc_seal();
    }
    if (s_record_seen && now - s_seen_written_ms >= (s_seen_new ? SEEN_NEW_MS : SEEN_COUNTS_MS))
        flush_seen();
}

void status_json(JsonObject out) {
    out["mounted"] = s_mounted;
    out["partition"] = PARTITION;
    Session fs;
    if (!fs.ok)
        return;
    out["total_bytes"] = (uint32_t) LittleFS.totalBytes();
    out["used_bytes"] = (uint32_t) LittleFS.usedBytes();
    out["boot_no"] = s_boot_no;
    out["writes"] = s_writes;  // file writes over the partition's life; room for roughly a million
    out["record_seen"] = s_record_seen;
    out["boot_log_paused"] = s_boot_deferred;  // restart loop: this boot is logged once it has stayed up a minute
    JsonArray files = out["files"].to<JsonArray>();
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (f.isDirectory() || String(f.name()).endsWith(".tmp"))
            continue;
        JsonObject o = files.add<JsonObject>();
        o["name"] = f.name();
        o["bytes"] = (uint32_t) f.size();
    }
}

void boots_json(JsonObject out) {
    Guard g;
    s_boots.to_json(out);
    out["boot_no"] = s_boot_no;
}

void seen_json(JsonObject out) {
    Guard g;
    s_seen.to_json(out);
    out["recording"] = s_record_seen;    // false: this list is in memory only and is gone at the next restart
    out["saved"] = !s_seen.dirty();      // false: heard since the last write
}

void captures_json(JsonArray out) {
    Session fs;
    if (!fs.ok)
        return;
    for (uint8_t slot = 0; slot < boardstore::CaptureShelf::SLOTS; slot++) {
        JsonDocument doc;
        if (!boardstore::CaptureShelf::read(s_flash, slot, doc))
            continue;
        JsonObject o = out.add<JsonObject>();
        o["slot"] = slot;
        for (const char *k : {"freq", "us", "boot", "decoded", "truncated", "rssi_dbm", "note"})
            o[k] = doc[k];
        o["pulses_n"] = doc["pulses"].size();
    }
}

bool capture_json(uint8_t slot, JsonDocument &doc) {
    return s_mounted && boardstore::CaptureShelf::read(s_flash, slot, doc);
}

int save_capture(uint32_t id, const char *note) {
    if (!s_mounted)
        return -2;
    // A burst is 2 KB: too big for the web task's stack.
    rfproto::RawBurst *b = (rfproto::RawBurst *) malloc(sizeof(rfproto::RawBurst));
    if (!b)
        return -2;
    uint32_t freq = 0;
    int slot = -1;
    if (g_engine.rx_capture_copy(id, *b, freq)) {
        boardstore::CaptureInfo info;
        info.freq_hz = freq;
        info.total_us = b->total_us;
        info.boot_no = s_boot_no;
        info.count = b->count;
        info.rssi_dbm = b->rssi_dbm;
        info.decoded = b->decoded;
        info.truncated = b->truncated;
        info.note = note ? note : "";
        slot = boardstore::CaptureShelf::save(s_flash, info, b->pulses);
        if (slot < 0)
            slot = -2;
    }
    free(b);
    return slot;
}

bool delete_capture(uint8_t slot) { return s_mounted && boardstore::CaptureShelf::remove(s_flash, slot); }

// Forgets every restart and the count; this boot is then logged afresh, as boot 1.
static bool clear_boots_all() {
    Session fs;
    Guard g;
    s_boots.reset(s_flash);
    s_boot_no = s_boots.record(s_flash, s_boot_reason, FW_VERSION, s_boot_prev_uptime, s_boot_pc);
    s_boot_deferred = false;
    s_rtc.skipped = 0;
    rtc_seal();
    return true;
}

bool clear(const char *what) {
    Session fs;
    if (!fs.ok)
        return false;
    bool all = !strcmp(what, "all");
    bool any = false;
    if (all) {  // everything, the restart counter included
        clear_boots_all();
    } else if (!strcmp(what, "boots")) {
        Guard g;
        s_boots.clear(s_flash);  // a 100-byte file: short enough to write under the lock
        any = true;
    }
    if (all || !strcmp(what, "seen")) {
        Guard g;
        s_seen.clear(s_flash);
        s_seen_new = false;
        any = true;
    }
    if (all || !strcmp(what, "captures")) {
        for (uint8_t slot = 0; slot < boardstore::CaptureShelf::SLOTS; slot++)
            boardstore::CaptureShelf::remove(s_flash, slot);
        any = true;
    }
    return any;
}

// Names come from the request: letters, digits, dot, dash and underscore only, so no paths.
static bool safe_name(const String &name) {
    if (!name.length() || name.length() > 24)
        return false;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (!isalnum((unsigned char) c) && c != '.' && c != '-' && c != '_')
            return false;
    }
    return true;
}

bool delete_file(const String &name) {
    Session fs;
    if (!fs.ok || !safe_name(name) || !LittleFS.exists("/" + name))
        return false;
    if (name == "boots.json")
        return clear_boots_all();
    if (name == "seen.json")
        return clear("seen");
    if (name == "store.json")
        s_record_seen = false;
    return LittleFS.remove("/" + name);
}

bool read_file(const String &name, String &out) {
    Session fs;
    if (!fs.ok || !safe_name(name))
        return false;
    File f = LittleFS.open("/" + name, "r");
    if (!f || f.isDirectory())
        return false;
    out = f.readString();
    f.close();
    return true;
}

}  // namespace store
