// Host tests for lib/net2rf_group: the coverage group's wire format, clock sync, roster, slot plans and the
// remote's queue. Run: pio test -e native
#include <unity.h>

#include <cstdlib>

#include "group_sync.h"

using namespace n2rgroup;

static const uint8_t MASTER[6] = {0xA8, 0x03, 0x2A, 0x11, 0x00, 0x50};
static const uint8_t R1[6] = {0xA8, 0x03, 0x2A, 0x11, 0x00, 0x10};
static const uint8_t R2[6] = {0xA8, 0x03, 0x2A, 0x11, 0x00, 0x90};
static const uint8_t R3[6] = {0xA8, 0x03, 0x2A, 0x11, 0x00, 0x30};
static const uint32_t FRAME = 46600;

void test_wire_roundtrip(void) {
    uint8_t buf[128];
    SyncMsg s{};
    memcpy(s.mac, R1, 6);
    s.seq = 7;
    s.t1 = -123456789012345LL;
    s.error_us = 850;
    s.on_time = 10, s.late = 2, s.missed = 1;
    strcpy(s.name, "Side yard");
    size_t n = encode(s, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n < 80);  // small: one per remote per second
    SyncMsg s2{};
    TEST_ASSERT_TRUE(decode(buf, n, s2));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R1, s2.mac, 6);
    TEST_ASSERT_TRUE(s2.t1 == s.t1);
    TEST_ASSERT_EQUAL_UINT32(850, s2.error_us);
    TEST_ASSERT_EQUAL_STRING("Side yard", s2.name);
    TEST_ASSERT_FALSE(decode(buf, n - 1, s2));  // truncated

    TxMsg t{};
    memcpy(t.mac, MASTER, 6);
    t.seq = 99;
    t.start_us = 5000000123LL;
    t.copies = 2;
    t.protocol = 1;
    t.freq_hz = 433920000;
    const uint8_t pkt[7] = {0x55, 0x01, 0x00, 0x7F, 0xFF, 0xD4, 0xFF};
    memcpy(t.packet, pkt, 7);
    n = encode(t, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0 && n <= 40);  // one per remote per change
    TxMsg t2{};
    TEST_ASSERT_TRUE(decode(buf, n, t2));
    TEST_ASSERT_TRUE(t2.start_us == t.start_us);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pkt, t2.packet, 7);
    SyncAck a2{};
    TEST_ASSERT_FALSE(decode(buf, n, a2));  // wrong type

    buf[4] = VERSION + 1;  // another version: ignored
    TEST_ASSERT_EQUAL_UINT8(0, message_type(buf, n));
    const uint8_t junk[] = "{\"p\":\"net2rf\"}";
    TEST_ASSERT_EQUAL_UINT8(0, message_type(junk, sizeof(junk)));
}

// Simulated exchange: remote clock = master clock - `offset`; one-way delays `up` and `down` (us).
static void exchange(ClockFilter &f, int64_t master_now, int64_t offset, int64_t up, int64_t down) {
    int64_t t1 = master_now - offset;
    int64_t t2 = master_now + up;
    int64_t t3 = t2 + 50;
    int64_t t4 = t3 + down - offset;
    f.add(t1, t2, t3, t4);
}

void test_clock_filter(void) {
    ClockFilter f;
    TEST_ASSERT_FALSE(f.valid());
    const int64_t OFFSET = 123456789;  // master is ahead by ~2 minutes
    srand(5);
    int64_t t = 1000000;
    for (int i = 0; i < 20; i++) {  // Wi-Fi-like: 1-20 ms each way, often lopsided
        exchange(f, t, OFFSET, 1000 + rand() % 19000, 1000 + rand() % 19000);
        t += 1000000;
    }
    exchange(f, t, OFFSET, 400, 450);  // one quick, nearly symmetric exchange: it wins
    TEST_ASSERT_TRUE(f.valid());
    TEST_ASSERT_INT64_WITHIN(100, OFFSET, f.offset());
    TEST_ASSERT_TRUE(f.error_us(t - OFFSET) < 600);
    // master time -> local time
    TEST_ASSERT_INT64_WITHIN(100, 5000000 - OFFSET, f.to_local(5000000));
    // the error grows with the sample's age (drift)
    TEST_ASSERT_TRUE(f.error_us(t - OFFSET + 8000000) > f.error_us(t - OFFSET));
    // nonsense samples are ignored
    f.add(10, 5, 6, 0);
    TEST_ASSERT_INT64_WITHIN(100, OFFSET, f.offset());
}

void test_clock_filter_ethernet_accuracy(void) {
    // Wired: 200-600 us each way. The offset lands within a few hundred microseconds.
    ClockFilter f;
    srand(11);
    for (int i = 0; i < 8; i++)
        exchange(f, 1000000 + i * 1000000LL, -42000, 200 + rand() % 400, 200 + rand() % 400);
    TEST_ASSERT_INT64_WITHIN(300, -42000, f.offset());
    TEST_ASSERT_TRUE(f.error_us(9000000 + 42000) < 1000);
}

static SyncMsg sync_from(const uint8_t *mac, uint32_t err, const char *name) {
    SyncMsg s{};
    memcpy(s.mac, mac, 6);
    s.error_us = err;
    strcpy(s.name, name);
    return s;
}

void test_roster(void) {
    Roster r;
    r.on_heartbeat(R2, 0x0A00A8C0, "Back", 1000);
    TEST_ASSERT_EQUAL_UINT8(1, r.size());
    TEST_ASSERT_TRUE(r.state(r.at(0), 1000) == MemberState::JOINING);
    r.on_sync(sync_from(R2, 900, "Back"), 0x0A00A8C0, 1500);
    r.on_sync(sync_from(R1, 600, "Side"), 0x0B00A8C0, 1500);
    r.on_sync(sync_from(R3, 25000, "Garage"), 0x0C00A8C0, 1500);  // sync too poor for a slot
    uint8_t live[MAX_REMOTES];
    uint8_t n = r.live(2000, live);
    TEST_ASSERT_EQUAL_UINT8(2, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R1, r.at(live[0]).mac, 6);  // sorted by MAC
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R2, r.at(live[1]).mac, 6);
    TEST_ASSERT_TRUE(r.state(r.at(2), 2000) == MemberState::POOR);
    // R2 stops syncing: lost after MEMBER_TIMEOUT_MS, the others stay
    r.on_sync(sync_from(R1, 600, "Side"), 0x0B00A8C0, 4000);
    n = r.live(4600, live);
    TEST_ASSERT_EQUAL_UINT8(1, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R1, r.at(live[0]).mac, 6);
    // sync switched off on R1: removed at once
    r.remove(R1);
    TEST_ASSERT_EQUAL_UINT8(0, r.live(4600, live));
    // forgotten after a long silence
    r.expire(100000);
    TEST_ASSERT_EQUAL_UINT8(0, r.size());
}

void test_roster_full(void) {
    Roster r;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 0};
    for (int i = 0; i < 12; i++) {
        mac[5] = (uint8_t) i;
        r.on_sync(sync_from(mac, 500, "x"), (uint32_t) i, 1000);
    }
    TEST_ASSERT_EQUAL_UINT8(MAX_REMOTES, r.size());
}

void test_plan(void) {
    Roster r;
    r.on_sync(sync_from(R1, 500, "A"), 1, 1000);
    r.on_sync(sync_from(R2, 2000, "B"), 2, 1000);
    uint8_t live[MAX_REMOTES];
    uint8_t n = r.live(1000, live);
    // Order by MAC: R1 (..10), master (..50), R2 (..90)
    Plan p = make_plan(1000000, MASTER, r, live, n, 0, FRAME, 1, 2000, 4000);
    TEST_ASSERT_EQUAL_UINT8(3, p.count);
    TEST_ASSERT_EQUAL_INT8(live[0], p.slots[0].member);
    TEST_ASSERT_EQUAL_INT8(-1, p.slots[1].member);
    TEST_ASSERT_EQUAL_INT8(live[1], p.slots[2].member);
    TEST_ASSERT_EQUAL_UINT32(GUARD_BASE_US + 4000, p.guard_us);
    TEST_ASSERT_EQUAL_UINT32(LEAD_MIN_US + 8000, p.lead_us);
    TEST_ASSERT_TRUE(p.slots[0].start_us == 1000000 + (int64_t) p.lead_us);
    // back to back, no overlap: each slot = copies x frame + guard
    for (int k = 1; k < 3; k++)
        TEST_ASSERT_TRUE(p.slots[k].start_us == p.slots[k - 1].start_us + FRAME + p.guard_us);
    TEST_ASSERT_TRUE(p.end_us == p.slots[2].start_us + FRAME + p.guard_us);
    // Rotation: the first slot moves round, one member per plan
    Plan q = make_plan(1000000, MASTER, r, live, n, 1, FRAME, 1, 2000, 4000);
    TEST_ASSERT_EQUAL_INT8(-1, q.slots[0].member);
    TEST_ASSERT_EQUAL_INT8(live[1], q.slots[1].member);
    TEST_ASSERT_EQUAL_INT8(live[0], q.slots[2].member);
    Plan z = make_plan(1000000, MASTER, r, live, n, 3, FRAME, 1, 2000, 4000);  // wraps
    TEST_ASSERT_EQUAL_INT8(live[0], z.slots[0].member);
    // Two copies each: slots twice as long
    Plan c = make_plan(0, MASTER, r, live, n, 0, FRAME, 2, 0, 0);
    TEST_ASSERT_TRUE(c.slots[1].start_us - c.slots[0].start_us == 2 * FRAME + GUARD_BASE_US);
    // Alone: just the master
    Plan a = make_plan(0, MASTER, r, live, 0, 5, FRAME, 1, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(1, a.count);
    TEST_ASSERT_EQUAL_INT8(-1, a.slots[0].member);
    // Guard and lead are capped
    Plan w = make_plan(0, MASTER, r, live, n, 0, FRAME, 1, 1000000, 1000000);
    TEST_ASSERT_EQUAL_UINT32(GUARD_MAX_US, w.guard_us);
    TEST_ASSERT_EQUAL_UINT32(LEAD_MAX_US, w.lead_us);
}

void test_master_tracker(void) {
    MasterTracker t;
    TEST_ASSERT_NULL(t.choose(nullptr, 0));
    t.on_master_beat(MASTER, 50, "Front", 1000);
    const MasterTracker::Seen *m = t.choose(nullptr, 2000);
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_STRING("Front", m->name);
    TEST_ASSERT_EQUAL_UINT8(1, t.alive(2000));
    // A second master appears with a lower MAC: reported, but a remote keeps the one it follows
    t.on_master_beat(R1, 10, "Oops", 3000);
    TEST_ASSERT_EQUAL_UINT8(2, t.alive(3000));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MASTER, t.choose(MASTER, 3000)->mac, 6);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R1, t.choose(nullptr, 3000)->mac, 6);  // a new remote: lowest MAC
    // The followed master goes quiet: switch
    t.on_master_beat(R1, 10, "Oops", 17000);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(R1, t.choose(MASTER, 17000)->mac, 6);
    TEST_ASSERT_EQUAL_UINT8(1, t.alive(17000));
    t.not_master(R1);
    TEST_ASSERT_NULL(t.choose(nullptr, 17000));
}

static TxMsg tx(uint32_t seq) {
    TxMsg m{};
    m.seq = seq;
    m.copies = 1;
    return m;
}

void test_remote_queue(void) {
    RemoteQueue q;
    TEST_ASSERT_TRUE(q.add(tx(1), 50000, 10000));
    TEST_ASSERT_TRUE(q.add(tx(2), 30000, 10000));
    TEST_ASSERT_EQUAL_INT64(30000, q.next()->local_us);  // earliest first
    q.pop(true);
    TEST_ASSERT_EQUAL_INT64(50000, q.next()->local_us);
    q.pop(true);
    TEST_ASSERT_EQUAL_UINT32(2, q.on_time);
    TEST_ASSERT_FALSE(q.add(tx(3), 10000, 20000));  // its slot is long gone
    TEST_ASSERT_EQUAL_UINT32(1, q.late);
    TEST_ASSERT_TRUE(q.add(tx(6), 40000, 20000));  // plans 4 and 5 never arrived
    TEST_ASSERT_EQUAL_UINT32(2, q.missed);
    TEST_ASSERT_TRUE(q.add(tx(5), 45000, 20000));  // out of order: not a gap
    TEST_ASSERT_EQUAL_UINT32(2, q.missed);
    q.pop(false);
    TEST_ASSERT_EQUAL_UINT32(2, q.late);
}

void test_mac_hex(void) {
    char s[13];
    mac_to_hex(MASTER, s);
    TEST_ASSERT_EQUAL_STRING("A8032A110050", s);
    uint8_t m[6];
    TEST_ASSERT_TRUE(mac_from_hex("a8032a110050", m));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(MASTER, m, 6);
    TEST_ASSERT_FALSE(mac_from_hex("A8032A11005", m));
    TEST_ASSERT_FALSE(mac_from_hex("A8032A11005G", m));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_wire_roundtrip);
    RUN_TEST(test_clock_filter);
    RUN_TEST(test_clock_filter_ethernet_accuracy);
    RUN_TEST(test_roster);
    RUN_TEST(test_roster_full);
    RUN_TEST(test_plan);
    RUN_TEST(test_master_tracker);
    RUN_TEST(test_remote_queue);
    RUN_TEST(test_mac_hex);
    return UNITY_END();
}
