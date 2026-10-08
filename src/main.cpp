// Net2RF LED
// xLights --DDP / E1.31--> WT32-ETH01 --> CC1101 or SX1278 (Ra-02) --433 MHz--> LED wristbands
#include <Arduino.h>
#include <AsyncUDP.h>

#include "config.h"
#include "engine.h"
#include "net.h"
#include "ota_guard.h"
#include "panel.h"
#include "peers.h"
#include "updater.h"
#include "store.h"
#include "web.h"

static AsyncUDP s_ddp;
static uint16_t s_ddp_port = 0;  // 0 = not listening

static AsyncUDP s_e131_unicast, s_e131_multicast;
static const uint16_t E131_PORT = 5568;
static bool s_e131_on = false, s_e131_mcast_on = false;
static uint16_t s_e131_universe = 0;

// (Re)bind the input sockets to match the current settings. Called once a second from loop().
static void bind_inputs() {
    bool ddp_enabled, e131_enabled, e131_multicast;
    uint16_t ddp_port, universe;
    {
        StateLock lock;
        ddp_enabled = g_app.ddp_enabled;
        ddp_port = g_app.ddp_port;
        e131_enabled = g_app.e131_enabled;
        e131_multicast = g_app.e131_multicast;
        universe = g_app.e131_universe;
    }

    uint16_t want_ddp = ddp_enabled ? ddp_port : 0;
    if (want_ddp != s_ddp_port) {
        s_ddp.close();
        s_ddp_port = 0;
        if (want_ddp && s_ddp.listen(want_ddp)) {
            s_ddp.onPacket([](AsyncUDPPacket &packet) {
                g_engine.on_ddp(packet.data(), packet.length(), (uint32_t) packet.remoteIP());
            });
            s_ddp_port = want_ddp;
            log_i("Listening for DDP on UDP %u", want_ddp);
        } else if (want_ddp) {
            log_e("Could not bind DDP port %u", want_ddp);
        }
    }

    auto on_e131 = [](AsyncUDPPacket &packet) {
        g_engine.on_e131(packet.data(), packet.length(), (uint32_t) packet.remoteIP());
    };
    if (e131_enabled != s_e131_on) {
        s_e131_unicast.close();
        s_e131_on = false;
        if (e131_enabled && s_e131_unicast.listen(E131_PORT)) {
            s_e131_unicast.onPacket(on_e131);
            s_e131_on = true;
            log_i("Listening for E1.31 on UDP %u", E131_PORT);
        }
    }
    bool want_mcast = e131_enabled && e131_multicast;
    if (want_mcast != s_e131_mcast_on || (want_mcast && universe != s_e131_universe)) {
        s_e131_multicast.close();
        s_e131_mcast_on = false;
        if (want_mcast) {
            IPAddress group(239, 255, universe >> 8, universe & 0xFF);  // sACN multicast address
            if (s_e131_multicast.listenMulticast(group, E131_PORT)) {
                s_e131_multicast.onPacket(on_e131);
                s_e131_mcast_on = true;
                log_i("Joined E1.31 multicast %s", group.toString().c_str());
            }
        }
    }
    s_e131_universe = universe;
}

void setup() {
    Serial.begin(115200);
    log_i("Net2RF LED v%s", FW_VERSION);

    state_lock_init();
    config_load();
    store::begin();  // boot log first: a crash later in setup() is then already on record
    panel::begin();
    peers::begin();
    net::begin();  // network and web UI come up first...
    web::begin();
    g_engine.begin();  // ...the radio is probed in the background by the engine task
}

void loop() {
    net::loop();
    web::loop();
    panel::loop();
    peers::loop();
    updater::loop();
    store::loop();
    ota_guard::loop(net::connected() || net::ap_active());  // web UI is up from setup()
    static uint32_t last_bind_check = 0;
    if (millis() - last_bind_check > 1000) {
        last_bind_check = millis();
        bind_inputs();
    }
    delay(1);
}
