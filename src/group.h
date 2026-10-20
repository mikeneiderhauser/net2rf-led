#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "group_sync.h"
#include "heartbeat.h"

// Coverage group on the network: the master / remote side of lib/net2rf_group over UDP (n2rgroup::GROUP_PORT, unicast
// only). Settings: AppConfig::sync_enabled / sync_master / sync_copies.
//
//  * Discovery rides on the controller heartbeat (peers.cpp): the master lists every controller whose beat says
//    sync on / master off; a remote follows the master whose beat says sync on / master on.
//  * A remote sends the master a clock-sync request every second; the master answers. That keeps the remote's
//    clock offset, and is how the master knows the remote is alive and how good its sync is.
//  * For each transmission the master plans the slots (n2rgroup::make_plan) and sends each remote its slot; the
//    engine transmits the master's own. A remote's engine transmits what is queued here, at the slot's time.
namespace group {

void begin();
void loop();  // Arduino loop: listens / stops with the setting, sends clock-sync requests, expires members

// A heartbeat from another controller (peers.cpp), with the address it came from.
void on_heartbeat(const net2rf::Heartbeat &h, uint32_t src_ip);

// ---- engine task ----
// Master: plan one transmission and send every live remote its slot. False when no remote is live (the engine then
// transmits on its own, as a standalone controller). Times are esp_timer microseconds.
bool plan(uint8_t protocol, uint32_t freq_hz, const uint8_t *packet, uint8_t copies, uint32_t frame_us, int64_t now_us,
          int64_t &own_start_us, uint8_t &own_copies, int64_t &plan_end_us);
// Remote: following a master with a synced clock. While true, the engine transmits only what the master schedules.
bool following();
// Remote: the earliest scheduled transmission (own clock), if any; then done() once it went out or was skipped.
bool next_due(n2rgroup::Pending &out);
void done(bool sent);

void status_json(JsonObject out);  // /api/status "group"
// One short line for the OLED ("Master: 2 remotes", "Remote: Garage", ...); empty when the group is off.
String summary();
void reset_stats();

}  // namespace group
