#include "ota_guard.h"

#include <esp_ota_ops.h>

// Tell the Arduino core not to confirm a freshly updated image at startup (its default); we confirm
// it ourselves once the controller has proven itself.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota_guard {

static const uint32_t HEALTHY_FOR_MS = 30000;   // reachable this long -> keep the update
static const uint32_t GIVE_UP_AFTER_MS = 180000;  // never reachable -> roll back

static bool s_checked = false, s_pending = false;
static uint32_t s_reachable_since = 0;

static void check_state() {
    if (s_checked)
        return;
    s_checked = true;
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    s_pending = esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY;
    if (s_pending)
        log_w("Running a new firmware update: will confirm it once the controller is reachable");
    if (rolled_back())
        log_w("The previous firmware update failed and was rolled back");
}

void loop(bool reachable) {
    check_state();
    if (!s_pending)
        return;
    uint32_t now = millis();
    if (!reachable) {
        s_reachable_since = 0;
        if (now > GIVE_UP_AFTER_MS) {
            log_e("Update never became reachable: rolling back to the previous firmware");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        return;
    }
    if (s_reachable_since == 0)
        s_reachable_since = now;
    if (now - s_reachable_since >= HEALTHY_FOR_MS) {
        esp_ota_mark_app_valid_cancel_rollback();
        s_pending = false;
        log_i("Firmware update confirmed");
    }
}

bool pending() {
    check_state();
    return s_pending;
}

bool rolled_back() { return esp_ota_get_last_invalid_partition() != nullptr; }

}  // namespace ota_guard
