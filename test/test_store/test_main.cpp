// Host tests for board_store.h: the boot log, the seen-on-air table and saved captures round-trip through
// their JSON files, stay within their limits, and shrug off missing or damaged files.
// Run: pio test -e native
#include <unity.h>

#include <map>
#include <string>

#include "board_store.h"

using namespace boardstore;

struct MemStorage : Storage {
    std::map<std::string, std::string> files;
    int writes{0};
    bool fail_writes{false};
    bool read(const char *name, std::string &out) override {
        auto it = this->files.find(name);
        if (it == this->files.end())
            return false;
        out = it->second;
        return true;
    }
    bool write(const char *name, const std::string &data) override {
        if (this->fail_writes)
            return false;
        this->writes++;
        this->files[name] = data;
        return true;
    }
    bool remove(const char *name) override { return this->files.erase(name) > 0; }
};

void test_boot_log_roundtrip() {
    MemStorage s;
    BootLog log;
    log.load(s);  // no file yet
    TEST_ASSERT_EQUAL_UINT32(0, log.boots());
    TEST_ASSERT_EQUAL_UINT32(1, log.record(s, "power on", "0.0.7", 0, 0));
    TEST_ASSERT_EQUAL_UINT32(2, log.record(s, "crash", "0.0.7", 3600, 0x400D1234));

    BootLog again;
    again.load(s);
    TEST_ASSERT_EQUAL_UINT32(2, again.boots());
    TEST_ASSERT_EQUAL(2, (int) again.entries().size());
    TEST_ASSERT_EQUAL_STRING("crash", again.entries()[1].reason.c_str());
    TEST_ASSERT_EQUAL_STRING("0.0.7", again.entries()[1].fw.c_str());
    TEST_ASSERT_EQUAL_UINT32(3600, again.entries()[1].prev_uptime_s);
    TEST_ASSERT_EQUAL_HEX32(0x400D1234, again.entries()[1].pc);

    // The file is plain JSON a person can read.
    JsonDocument doc;
    TEST_ASSERT_TRUE(deserializeJson(doc, s.files[BootLog::PATH]) == DeserializationError::Ok);
    TEST_ASSERT_EQUAL(1, doc["v"].as<int>());
    TEST_ASSERT_EQUAL_STRING("0x400d1234", doc["entries"][1]["pc"].as<const char *>());
    TEST_ASSERT_TRUE(doc["entries"][0]["pc"].isNull());
}

void test_boot_log_ring_and_streak() {
    MemStorage s;
    BootLog log;
    for (int i = 0; i < BootLog::CAPACITY + 5; i++)
        log.record(s, i < BootLog::CAPACITY + 2 ? "software restart" : "watchdog", "1.0", 0, 0);
    TEST_ASSERT_EQUAL(BootLog::CAPACITY, (int) log.entries().size());
    TEST_ASSERT_EQUAL_UINT32(6, log.entries().front().boot_no);  // the first five dropped out
    TEST_ASSERT_EQUAL_UINT32(BootLog::CAPACITY + 5, log.boots());
    TEST_ASSERT_EQUAL_UINT8(3, log.abnormal_streak());
    log.record(s, "power on", "1.0", 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, log.abnormal_streak());

    log.clear(s);
    BootLog again;
    again.load(s);
    TEST_ASSERT_EQUAL(0, (int) again.entries().size());
    TEST_ASSERT_EQUAL_UINT32(BootLog::CAPACITY + 6, again.boots());  // the counter carries on
}

void test_boot_log_skipped_restarts() {
    MemStorage s;
    BootLog log;
    log.record(s, "crash", "1.0", 0, 0);
    // 40 crash-loop restarts went unlogged; the one that finally stayed up reports them.
    TEST_ASSERT_EQUAL_UINT32(42, log.record(s, "crash", "1.0", 0, 0, 40));
    BootLog again;
    again.load(s);
    TEST_ASSERT_EQUAL_UINT32(42, again.boots());
    TEST_ASSERT_EQUAL_UINT32(40, again.entries()[1].skipped);
    TEST_ASSERT_EQUAL_UINT32(0, again.entries()[0].skipped);
}

void test_damaged_files_read_as_empty() {
    MemStorage s;
    s.files[BootLog::PATH] = "{\"v\":1,\"boots\":4,\"entries\":[{\"n\":4,\"reas";  // cut off mid-write
    BootLog log;
    log.load(s);
    TEST_ASSERT_EQUAL_UINT32(0, log.boots());
    s.files[BootLog::PATH] = "{\"v\":99,\"boots\":4,\"entries\":[]}";  // a future format
    log.load(s);
    TEST_ASSERT_EQUAL_UINT32(0, log.boots());
    s.files[SeenTable::PATH] = "not json at all";
    SeenTable seen;
    seen.load(s);
    TEST_ASSERT_EQUAL(0, (int) seen.entries().size());
    TEST_ASSERT_FALSE(CaptureShelf::used(s, 0));
}

void test_seen_table() {
    MemStorage s;
    SeenTable t;
    const uint8_t p1_group5[7] = {0x55, 5, 0xFF, 0x00, 0xFF, 0, 0xFF};
    const uint8_t p1_group5_other_colour[7] = {0x55, 5, 0x00, 0x00, 0xFF, 0, 0xFF};
    const uint8_t p0[7] = {0x00, 0x08, 0x00, 0x0F, 0x01, 0x02, 0};
    TEST_ASSERT_TRUE(t.note(1, p1_group5, -70, 3));
    TEST_ASSERT_FALSE(t.note(1, p1_group5_other_colour, -55, 4));  // same transmitter group, another colour
    TEST_ASSERT_TRUE(t.note(0, p0, -80, 4));
    TEST_ASSERT_TRUE(t.dirty());
    TEST_ASSERT_EQUAL(0, s.writes);  // nothing is written until asked
    TEST_ASSERT_TRUE(t.save(s));
    TEST_ASSERT_FALSE(t.dirty());

    SeenTable again;
    again.load(s);
    TEST_ASSERT_EQUAL(2, (int) again.entries().size());
    const SeenEntry &a = again.entries()[0];
    TEST_ASSERT_EQUAL_UINT8(1, a.protocol);
    TEST_ASSERT_EQUAL_UINT8(5, a.addr[0]);
    TEST_ASSERT_EQUAL_UINT32(2, a.count);
    TEST_ASSERT_EQUAL_UINT32(3, a.first_boot);
    TEST_ASSERT_EQUAL_UINT32(4, a.last_boot);
    TEST_ASSERT_EQUAL_INT16(-55, a.best_rssi);
    const SeenEntry &b = again.entries()[1];
    TEST_ASSERT_EQUAL_UINT8(0, b.protocol);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p0, b.addr, 4);

    JsonDocument doc;
    again.to_json(doc.to<JsonObject>());
    TEST_ASSERT_EQUAL_STRING("05000000", doc["entries"][0]["address"].as<const char *>());
    TEST_ASSERT_EQUAL(5, doc["entries"][0]["group"].as<int>());
    TEST_ASSERT_EQUAL_STRING("0008000F", doc["entries"][1]["address"].as<const char *>());
    TEST_ASSERT_TRUE(doc["entries"][1]["group"].isNull());
}

void test_seen_table_full_evicts_oldest() {
    SeenTable t;
    uint8_t pkt[7] = {0x55, 0, 0, 0, 0, 0, 0xFF};
    for (int g = 0; g < SeenTable::CAPACITY; g++) {
        pkt[1] = (uint8_t) g;
        t.note(1, pkt, -60, g == 7 ? 1 : 2);  // group 7 was last heard longest ago
    }
    pkt[1] = 200;
    TEST_ASSERT_TRUE(t.note(1, pkt, -60, 3));
    TEST_ASSERT_EQUAL(SeenTable::CAPACITY, (int) t.entries().size());
    bool has7 = false, has200 = false;
    for (const SeenEntry &e : t.entries()) {
        has7 |= e.addr[0] == 7;
        has200 |= e.addr[0] == 200;
    }
    TEST_ASSERT_FALSE(has7);
    TEST_ASSERT_TRUE(has200);
}

void test_seen_failed_write_stays_dirty() {
    MemStorage s;
    SeenTable t;
    const uint8_t pkt[7] = {0x55, 1, 0, 0, 0, 0, 0xFF};
    t.note(1, pkt, -60, 1);
    s.fail_writes = true;
    TEST_ASSERT_FALSE(t.save(s));
    TEST_ASSERT_TRUE(t.dirty());  // tried again at the next flush
}

void test_captures() {
    MemStorage s;
    int16_t pulses[CaptureShelf::MAX_PULSES];
    for (int i = 0; i < CaptureShelf::MAX_PULSES; i++)
        pulses[i] = (int16_t) (i % 2 ? -600 - i : 200 + i);
    CaptureInfo info;
    info.freq_hz = 433920000;
    info.total_us = 95000;
    info.boot_no = 12;
    info.count = 116;
    info.rssi_dbm = -48;
    info.decoded = true;
    info.note = "vendor transmitter, group 5";
    TEST_ASSERT_EQUAL(0, CaptureShelf::save(s, info, pulses));
    TEST_ASSERT_EQUAL(1, CaptureShelf::save(s, info, pulses));

    JsonDocument doc;
    TEST_ASSERT_TRUE(CaptureShelf::read(s, 1, doc));
    TEST_ASSERT_EQUAL_UINT32(433920000, doc["freq"].as<uint32_t>());
    TEST_ASSERT_EQUAL(116, (int) doc["pulses"].size());
    TEST_ASSERT_EQUAL(200, doc["pulses"][0].as<int>());
    TEST_ASSERT_EQUAL(-601, doc["pulses"][1].as<int>());
    TEST_ASSERT_TRUE(doc["decoded"].as<bool>());
    TEST_ASSERT_EQUAL_INT(-48, doc["rssi_dbm"].as<int>());
    TEST_ASSERT_EQUAL_STRING("vendor transmitter, group 5", doc["note"].as<const char *>());

    // A freed slot is the next one used; a full shelf refuses.
    TEST_ASSERT_TRUE(CaptureShelf::remove(s, 0));
    TEST_ASSERT_FALSE(CaptureShelf::read(s, 0, doc));
    info.count = CaptureShelf::MAX_PULSES;
    for (int i = 0; i < CaptureShelf::SLOTS - 1; i++)
        TEST_ASSERT_TRUE(CaptureShelf::save(s, info, pulses) >= 0);
    TEST_ASSERT_EQUAL(-1, CaptureShelf::save(s, info, pulses));
    // The largest capture stays well inside a 128 KB partition even with every slot full.
    size_t total = 0;
    for (auto &f : s.files)
        total += f.second.size();
    TEST_ASSERT_TRUE(total < 64 * 1024);

    info.count = 0;
    TEST_ASSERT_EQUAL(-1, CaptureShelf::save(s, info, pulses));
    TEST_ASSERT_FALSE(CaptureShelf::remove(s, CaptureShelf::SLOTS));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_log_roundtrip);
    RUN_TEST(test_boot_log_ring_and_streak);
    RUN_TEST(test_boot_log_skipped_restarts);
    RUN_TEST(test_damaged_files_read_as_empty);
    RUN_TEST(test_seen_table);
    RUN_TEST(test_seen_table_full_evicts_oldest);
    RUN_TEST(test_seen_failed_write_stays_dirty);
    RUN_TEST(test_captures);
    return UNITY_END();
}
