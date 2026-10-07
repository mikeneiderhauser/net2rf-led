// Host tests for the engine's scheduler (src/engine.cpp, built against the stubs in stubs/): which packets go on
// air, on which protocol and in what order, for zones on protocol 0, protocol 1 or both.
// Run: pio test -e native
#include <unity.h>

#include <vector>

// The scheduler's steps are private; the tests drive them directly.
#define private public
#include "../../src/engine.cpp"
#undef private

using namespace rfproto;

// ---- what engine.cpp needs from the rest of the firmware ----
AppConfig g_app;
NetConfig g_net;
static uint32_t s_now = 1000;
uint32_t millis() { return s_now; }
uint32_t micros() { return s_now * 1000; }
const char *radio_type_name(uint8_t) { return "cc1101"; }
bool OokSender::begin() { return this->ready_ = true; }
void OokSender::end() { this->ready_ = false; }
bool OokSender::send(uint8_t, const uint8_t *, uint8_t) { return true; }
struct FakeRadio : Radio {
    const char *name() const override { return "fake"; }
    bool init() override { return true; }
    bool tune(uint32_t, int8_t) override { return true; }
    bool begin_tx() override { return true; }
    void end_tx() override {}
    int8_t min_power() const override { return -30; }
    int8_t max_power() const override { return 10; }
};
Radio *create_radio(RadioType) { return new FakeRadio(); }

// ---- setup ----
// Like config_defaults_app(): zone 0 every group, zone k group k, in both protocols' addresses.
static void setup(uint8_t zones, uint8_t protocols, InputMode mode = MODE_PIXEL, bool base_layer = false) {
    memset(&g_app, 0, sizeof(g_app));
    g_app.num_zones = zones;
    g_app.start_channel = 1;
    g_app.mode = mode;
    g_app.repeats = 3;
    g_app.off_threshold = 16;
    g_app.output_enabled = 1;
    g_app.base_layer = base_layer;
    g_app.freq[0] = DEFAULT_FREQ_P0;
    g_app.freq[1] = DEFAULT_FREQ_P1;
    for (uint8_t i = 0; i < MAX_ZONES; i++) {
        ZoneConfig &z = g_app.zones[i];
        z.enabled = 1;
        z.protocols = protocols;
        const uint8_t p0[4] = {0x00, (uint8_t) (i ? (1u << i) & 0xFF : 0xFF), (uint8_t) (i ? (1u << i) >> 8 : 0xFF), 0x0F};
        memcpy(z.addr, p0, 4);
        z.p1_addr[0] = i;  // group code (0 = every group)
        z.p1_addr[1] = 0xFF;
    }
    g_engine = Engine();
    g_engine.radio_ = create_radio(RADIO_CC1101);
    g_engine.radio_state_ = RadioState::READY;
    state_lock_init();
}

// Sends DDP channel data starting at channel 1.
static void ddp(std::vector<uint8_t> channels) {
    std::vector<uint8_t> pkt = {0x41, 0, 1, 1, 0, 0, 0, 0, (uint8_t) (channels.size() >> 8), (uint8_t) channels.size()};
    pkt.insert(pkt.end(), channels.begin(), channels.end());
    g_engine.on_ddp(pkt.data(), pkt.size(), 0);
}

struct Sent {
    uint8_t protocol;
    std::vector<uint8_t> pkt;
};

// One engine step's worth of scheduling: recompute what the zones want, then everything it would transmit.
static std::vector<Sent> drain() {
    std::vector<Sent> out;
    g_engine.update_wants_(s_now);
    g_engine.input_dirty_ = g_engine.config_dirty_ = false;
    if (g_engine.hold_after_update_) {
        g_engine.hold_after_update_ = false;
        g_engine.mark_zones_sent_(s_now, ALL_PROTOCOLS);
    }
    Engine::Job job;
    while (g_engine.pick_job_(s_now, job))
        out.push_back({job.protocol, std::vector<uint8_t>(job.packet, job.packet + PACKET_LEN)});
    return out;
}

static int count(const std::vector<Sent> &v, uint8_t protocol) {
    int n = 0;
    for (const Sent &s : v)
        n += s.protocol == protocol;
    return n;
}

static std::vector<uint8_t> expect(uint8_t protocol, const uint8_t *addr, uint8_t r, uint8_t g, uint8_t b,
                                   Action a = ACTION_COLOR) {
    uint8_t p[PACKET_LEN];
    build_packet(protocol, addr, a, r, g, b, 16, p);
    return std::vector<uint8_t>(p, p + PACKET_LEN);
}

// ---- tests ----

void test_protocol0_only_sends_no_protocol1(void) {
    setup(3, PROTOCOL_BIT[0]);
    ddp({255, 0, 0, 0, 255, 0, 0, 0, 255});
    auto sent = drain();
    TEST_ASSERT_EQUAL(3, sent.size());
    TEST_ASSERT_EQUAL(0, count(sent, 1));
    g_engine.all_off();  // no protocol 1 zone: no protocol 1 broadcast either
    sent = drain();
    TEST_ASSERT_EQUAL(3, sent.size());
    TEST_ASSERT_EQUAL(0, count(sent, 1));
    g_engine.set_test(TestMode::SOLID, 0, 0, 255);
    sent = drain();
    TEST_ASSERT_EQUAL(0, count(sent, 1));
    TEST_ASSERT_FALSE(g_engine.can_broadcast_());
}

void test_protocol1_only_sends_no_protocol0(void) {
    setup(3, PROTOCOL_BIT[1]);
    ddp({255, 0, 0, 0, 255, 0, 0, 0, 255});
    auto sent = drain();
    TEST_ASSERT_EQUAL(3, sent.size());
    TEST_ASSERT_EQUAL(0, count(sent, 0));
    g_engine.all_off();  // one broadcast to every group
    sent = drain();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, sent[0].protocol);
    TEST_ASSERT_EQUAL_HEX8(0x00, sent[0].pkt[1]);  // group 0
}

void test_both_protocols_protocol0_then_protocol1(void) {
    setup(3, ALL_PROTOCOLS);
    ddp({0, 0, 0, 0, 0, 0, 0, 0, 0});
    drain();
    // A change to zone 2 only: two transmissions, protocol 0 first, both for zone 2.
    ddp({0, 0, 0, 0, 0, 0, 0, 0, 255});
    auto sent = drain();
    TEST_ASSERT_EQUAL(2, sent.size());
    TEST_ASSERT_EQUAL_UINT8(0, sent[0].protocol);
    TEST_ASSERT_EQUAL_UINT8(1, sent[1].protocol);
    const ZoneConfig &z2 = g_app.zones[2];
    std::vector<uint8_t> p0 = expect(0, z2.addr, 0, 0, 255), p1 = expect(1, z2.p1_addr, 0, 0, 255);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p0.data(), sent[0].pkt.data(), PACKET_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(p1.data(), sent[1].pkt.data(), PACKET_LEN);
    // Nothing changed: nothing more goes out.
    TEST_ASSERT_EQUAL(0, drain().size());
}

void test_every_zone_pairs_its_protocols(void) {
    // Several zones change at once: each zone's protocol 0 packet is directly followed by its protocol 1 packet.
    setup(4, ALL_PROTOCOLS);
    ddp({10, 0, 0, 0, 20, 0, 0, 0, 30, 40, 40, 0});
    auto sent = drain();
    TEST_ASSERT_EQUAL(8, sent.size());
    for (size_t k = 0; k < sent.size(); k += 2) {
        TEST_ASSERT_EQUAL_UINT8(0, sent[k].protocol);
        TEST_ASSERT_EQUAL_UINT8(1, sent[k + 1].protocol);
    }
    // All Zones (every group) goes before the single groups in both protocols.
    TEST_ASSERT_EQUAL_HEX8(0xFF, sent[0].pkt[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, sent[1].pkt[1]);
}

void test_mixed_zones(void) {
    // Zone 0 protocol 0, zone 1 protocol 1, zone 2 both.
    setup(3, PROTOCOL_BIT[0]);
    g_app.zones[1].protocols = PROTOCOL_BIT[1];
    g_app.zones[2].protocols = ALL_PROTOCOLS;
    ddp({255, 0, 0, 0, 255, 0, 0, 0, 255});
    auto sent = drain();
    TEST_ASSERT_EQUAL(4, sent.size());
    TEST_ASSERT_EQUAL(2, count(sent, 0));  // zones 0 and 2
    TEST_ASSERT_EQUAL(2, count(sent, 1));  // zones 1 and 2
    for (const Sent &s : sent)
        if (s.protocol == 1)
            TEST_ASSERT_NOT_EQUAL(0, s.pkt[1]);  // zone 0 has no protocol 1: nothing to group 0
    g_engine.all_off();  // protocol 0: an "off" per protocol 0 zone; protocol 1: one broadcast
    sent = drain();
    TEST_ASSERT_EQUAL(3, sent.size());
    TEST_ASSERT_EQUAL(2, count(sent, 0));
    TEST_ASSERT_EQUAL(1, count(sent, 1));
}

void test_disabled_zone_and_vendor_mode(void) {
    setup(2, ALL_PROTOCOLS);
    g_app.zones[1].enabled = 0;
    ddp({255, 0, 0, 0, 255, 0});
    auto sent = drain();
    TEST_ASSERT_EQUAL(2, sent.size());  // zone 0 on both; zone 1 off

    // Vendor layout drives protocol 1 only, even for a zone on both.
    setup(1, ALL_PROTOCOLS, MODE_VENDOR);
    ddp({85, 7, 0, 0, 255});
    sent = drain();
    TEST_ASSERT_EQUAL(1, sent.size());
    TEST_ASSERT_EQUAL_UINT8(1, sent[0].protocol);
    TEST_ASSERT_EQUAL_HEX8(7, sent[0].pkt[1]);  // group from the input
}

void test_dmx_effect_on_both(void) {
    // DMX mode, FX channel = fade out (60-79): protocol 0 fades out, protocol 1 (no effects) switches off.
    setup(1, ALL_PROTOCOLS, MODE_DMX);
    ddp({255, 0, 0, 70});
    auto sent = drain();
    TEST_ASSERT_EQUAL(2, sent.size());
    TEST_ASSERT_EQUAL_HEX8(0x06, sent[0].pkt[4]);  // protocol 0: CMD 06 (fade out)
    std::vector<uint8_t> off = expect(1, g_app.zones[0].p1_addr, 0, 0, 0, ACTION_OFF);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(off.data(), sent[1].pkt.data(), PACKET_LEN);
}

void test_base_layer_with_both(void) {
    // All Zones red and zone 2 blue, base layer on: protocol 0 cuts group 2 out of the broadcast; protocol 1 is
    // untouched (plain packets per zone).
    setup(3, ALL_PROTOCOLS, MODE_PIXEL, true);
    ddp({255, 0, 0, 0, 0, 0, 0, 0, 255});
    auto sent = drain();
    bool cut = false;
    for (const Sent &s : sent)
        if (s.protocol == 0 && s.pkt[4] == 0x01 && s.pkt[5] == 0x00)  // red, protocol 0
            cut = s.pkt[1] == 0xFB && s.pkt[2] == 0xFF;
    TEST_ASSERT_TRUE(cut);
    TEST_ASSERT_EQUAL(3, count(sent, 1));  // protocol 1 has no base layer: every zone its own (zone 1: off)
}

void test_status_lists_each_protocol(void) {
    setup(1, ALL_PROTOCOLS);
    ddp({0, 255, 0});
    drain();
    JsonDocument doc;
    g_engine.status_json(doc.to<JsonObject>());
    JsonArray packets = doc["zones"][0]["packets"];
    TEST_ASSERT_EQUAL(2, packets.size());
    TEST_ASSERT_EQUAL(0, packets[0]["p"].as<int>());
    TEST_ASSERT_EQUAL(1, packets[1]["p"].as<int>());
    TEST_ASSERT_EQUAL_STRING(packets[0]["pkt"].as<const char *>(), doc["zones"][0]["packet"].as<const char *>());
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_protocol0_only_sends_no_protocol1);
    RUN_TEST(test_protocol1_only_sends_no_protocol0);
    RUN_TEST(test_both_protocols_protocol0_then_protocol1);
    RUN_TEST(test_every_zone_pairs_its_protocols);
    RUN_TEST(test_mixed_zones);
    RUN_TEST(test_disabled_zone_and_vendor_mode);
    RUN_TEST(test_dmx_effect_on_both);
    RUN_TEST(test_base_layer_with_both);
    RUN_TEST(test_status_lists_each_protocol);
    return UNITY_END();
}
