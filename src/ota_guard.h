#pragma once

#include <Arduino.h>

// Firmware-update rollback. After an OTA update the new image boots in "pending verify" state.
// It is only marked good once the controller is actually reachable (web UI up, network or setup AP
// active) and has stayed up for a while. If it crashes or reboots first, the bootloader returns to
// the previous firmware on the next boot; if it hangs without ever getting healthy, we roll back.
namespace ota_guard {

void loop(bool reachable);  // call regularly; `reachable` = web server running and an IP to reach it
bool pending();             // running an update that has not been confirmed yet
int32_t confirm_left_ms();  // while pending: ms until it's confirmed, or -1 while not yet reachable
bool just_confirmed(uint32_t within_ms);  // confirmed during this run, less than within_ms ago
bool rolled_back();         // the last update failed and was rolled back

}  // namespace ota_guard
