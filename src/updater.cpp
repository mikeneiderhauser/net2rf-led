#include "updater.h"

#include <Update.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include <version.h>

#include "config.h"
#include "engine.h"
#include "net.h"

extern Engine g_engine;

namespace updater {

using net2rf::compare_versions;
using net2rf::tag_from_release_url;

namespace {
enum class State : uint8_t { IDLE, DOWNLOADING, DONE, FAILED };

volatile State s_state = State::IDLE;
volatile bool s_checking = false;  // the release check (below) has a connection open
volatile uint8_t s_progress = 0;  // percent
char s_url[256];
char s_tag[41];
char s_error[96];

const char *state_name(State s) {
    switch (s) {
        case State::DOWNLOADING:
            return "downloading";
        case State::DONE:
            return "done";
        case State::FAILED:
            return "failed";
        default:
            return "idle";
    }
}

// Letters, digits, '.', '_' and '-' only: nothing that could change the URL's host or path.
bool valid_name(const char *s, size_t max_len) {
    size_t n = strlen(s);
    if (n == 0 || n > max_len || s[0] == '.')
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char) s[i]) && s[i] != '.' && s[i] != '_' && s[i] != '-')
            return false;
    return true;
}

void fail(const String &msg) {
    strlcpy(s_error, msg.c_str(), sizeof(s_error));
    log_e("Update from GitHub failed: %s", s_error);
    g_engine.set_suspended(false);  // resume RF
    s_state = State::FAILED;
}

// Returns an empty string on success, otherwise why it failed. `retry` is set when nothing was written yet and
// trying again is worthwhile (the connection could not be made).
String download(bool &retry) {
    retry = false;
    esp_http_client_config_t cfg = {};
    cfg.url = s_url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;  // verify GitHub's certificate against the built-in CA bundle
    cfg.timeout_ms = 15000;
    cfg.buffer_size = 8192;     // GitHub's response headers are long
    cfg.buffer_size_tx = 2048;  // ... and so is the signed URL it redirects to
    cfg.user_agent = "net2rf-led/" FW_VERSION;
    cfg.keep_alive_enable = false;
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http)
        return "out of memory";

    // GitHub answers with a redirect to its file servers: follow it (a few hops at most).
    int code = 0;
    int64_t len = 0;
    esp_err_t e = ESP_OK;
    for (int hop = 0; hop < 4; hop++) {
        e = esp_http_client_open(http, 0);
        if (e != ESP_OK)
            break;
        len = esp_http_client_fetch_headers(http);
        code = esp_http_client_get_status_code(http);
        if (code != 301 && code != 302 && code != 307 && code != 308)
            break;
        esp_http_client_set_redirection(http);
        esp_http_client_close(http);
    }
    String err;
    if (e != ESP_OK || len < 0) {
        err = String("could not reach GitHub (") + esp_err_to_name(e != ESP_OK ? e : ESP_FAIL) + ")";
        retry = true;
    }
    else if (code == 404)
        err = "release file not found";
    else if (code != 200)
        err = "GitHub answered HTTP " + String(code);
    else if (len <= 0)
        err = "unknown download size";
    else if (!Update.begin((size_t) len, U_FLASH))
        err = Update.errorString();
    if (err.isEmpty()) {
        uint8_t *buf = (uint8_t *) malloc(4096);
        int64_t done = 0;
        while (buf && done < len) {
            int n = esp_http_client_read(http, (char *) buf, 4096);
            if (n <= 0)
                break;
            if (Update.write(buf, n) != (size_t) n) {
                err = Update.errorString();
                break;
            }
            done += n;
            s_progress = (uint8_t) (done * 100 / len);
        }
        free(buf);
        if (err.isEmpty() && done != len)
            err = buf ? "download incomplete (" + String((uint32_t) done) + " of " + String((uint32_t) len) + " bytes)"
                      : String("out of memory");
        if (err.isEmpty() && !Update.end(true))
            err = Update.errorString();
        if (!err.isEmpty())
            Update.abort();
    }
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    if (err.isEmpty())
        log_i("Update %s written (%u bytes)", s_tag, (unsigned) len);
    return err;
}

void task(void *) {
    g_engine.set_suspended(true);  // no RF while flashing
    // One secure connection at a time: there isn't the memory for two, and the web UI's "Check for updates"
    // starts a release check right before "Install" can be pressed.
    for (int i = 0; i < 200 && s_checking; i++)
        delay(100);
    log_i("Updating from %s", s_url);
    String err;
    for (int attempt = 1; attempt <= 3; attempt++) {
        bool retry;
        err = download(retry);
        if (err.isEmpty() || !retry)
            break;
        log_w("Update attempt %d: %s", attempt, err.c_str());
        if (attempt < 3)
            delay(2000);
    }
    if (err.isEmpty()) {
        s_progress = 100;
        s_state = State::DONE;
    } else {
        fail(err);
    }
    vTaskDelete(nullptr);
}
// ---- looking for a newer release -----------------------------------------------------------------

const uint32_t FIRST_CHECK_MS = 30000;        // after boot, once the network is up
const uint32_t RETRY_CHECK_MS = 60000;        // after a check that could not reach GitHub; doubles each time ...
const uint32_t RETRY_CHECK_MAX_MS = 15 * 60000;  // ... up to this

volatile bool s_check_now = false;
volatile bool s_check_failed = false;
bool s_checked = false;
uint32_t s_checked_ms = 0;
uint32_t s_next_check_ms = FIRST_CHECK_MS;
char s_check_repo[sizeof(AppConfig::update_repo)];
char s_latest[41] = "";       // tag of the latest release, once known
char s_check_error[64] = "";

struct Redirect {
    char location[256];
};

esp_err_t on_check_event(esp_http_client_event_t *e) {
    if (e->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(e->header_key, "Location") == 0)
        strlcpy(((Redirect *) e->user_data)->location, e->header_value, sizeof(Redirect::location));
    return ESP_OK;
}

// github.com/<repo>/releases/latest answers with a redirect to the newest release's tag page: read the tag
// from that redirect. No API call, no JSON, no rate limit.
void check_task(void *) {
    static Redirect redirect;
    redirect.location[0] = 0;
    char url[160];
    snprintf(url, sizeof(url), "https://github.com/%s/releases/latest", s_check_repo);
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_HEAD;
    cfg.disable_auto_redirect = true;
    cfg.event_handler = on_check_event;
    cfg.user_data = &redirect;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 10000;
    cfg.buffer_size = 8192;
    cfg.user_agent = "net2rf-led/" FW_VERSION;
    cfg.keep_alive_enable = false;
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    esp_err_t e = http ? esp_http_client_perform(http) : ESP_ERR_NO_MEM;
    int code = http ? esp_http_client_get_status_code(http) : 0;
    if (http)
        esp_http_client_cleanup(http);

    char tag[sizeof(s_latest)];
    bool failed = false;
    if (e != ESP_OK && !redirect.location[0]) {
        snprintf(s_check_error, sizeof(s_check_error), "could not reach GitHub (%s)", esp_err_to_name(e));
        failed = true;
    } else if (tag_from_release_url(redirect.location, tag, sizeof(tag))) {
        strlcpy(s_latest, tag, sizeof(s_latest));
        s_check_error[0] = 0;
        log_i("Latest release of %s: %s (running %s)", s_check_repo, s_latest, FW_VERSION);
    } else {
        snprintf(s_check_error, sizeof(s_check_error), code == 404 ? "repository not found" : "no releases found");
        s_latest[0] = 0;
    }
    if (failed)
        log_w("Update check: %s", s_check_error);
    s_check_failed = failed;
    s_checked_ms = millis();
    s_checked = true;
    s_checking = false;
    vTaskDelete(nullptr);
}
}  // namespace

bool valid_repo(const char *repo) {
    const char *slash = strchr(repo, '/');
    if (!slash || strchr(slash + 1, '/'))
        return false;
    String owner = String(repo).substring(0, slash - repo);
    return valid_name(owner.c_str(), 39) && valid_name(slash + 1, 100);
}

bool start(const String &repo, const String &tag, const String &asset, String &err) {
    if (s_state == State::DOWNLOADING || s_state == State::DONE) {
        err = "an update is already running";
        return false;
    }
    if (!valid_repo(repo.c_str())) {
        err = "invalid release source";
        return false;
    }
    if (!valid_name(tag.c_str(), sizeof(s_tag) - 1) || !valid_name(asset.c_str(), 80) || !asset.endsWith(".bin") ||
        asset.indexOf("factory") >= 0) {
        err = "invalid release tag or file name";
        return false;
    }
    int n = snprintf(s_url, sizeof(s_url), "https://github.com/%s/releases/download/%s/%s", repo.c_str(), tag.c_str(),
                     asset.c_str());
    if (n < 0 || n >= (int) sizeof(s_url)) {
        err = "release URL too long";
        return false;
    }
    strlcpy(s_tag, tag.c_str(), sizeof(s_tag));
    s_error[0] = 0;
    s_progress = 0;
    s_state = State::DOWNLOADING;
    if (xTaskCreate(task, "gh_update", 12288, nullptr, 1, nullptr) != pdPASS) {
        s_state = State::FAILED;
        strlcpy(s_error, "out of memory", sizeof(s_error));
        err = s_error;
        return false;
    }
    return true;
}

void check_now() { s_check_now = true; }

void forget() {
    if (s_checking)
        return;
    s_latest[0] = 0;
    s_check_error[0] = 0;
    s_checked = false;
}

void loop() {
    static uint32_t last_ms = 0;
    uint32_t now = millis();
    if (now - last_ms < 1000)
        return;
    last_ms = now;
    if (s_checking || s_state == State::DOWNLOADING || s_state == State::DONE || !net::connected())
        return;
    bool enabled;
    uint16_t hours;
    char repo[sizeof(s_check_repo)];
    {
        StateLock lock;
        enabled = !g_app.update_check_off;
        hours = g_app.update_check_hours;
        strlcpy(repo, g_app.update_repo, sizeof(repo));
    }
    static uint8_t failures = 0;
    if (s_check_failed) {  // GitHub was unreachable: try again sooner than the full period
        s_check_failed = false;
        uint32_t wait = RETRY_CHECK_MS << (failures < 4 ? failures : 4);
        s_next_check_ms = now + (wait < RETRY_CHECK_MAX_MS ? wait : RETRY_CHECK_MAX_MS);
        if (failures < 255)
            failures++;
    } else if (s_checked && !s_check_error[0]) {
        failures = 0;
    }
    if (!s_check_now && !(enabled && (int32_t) (now - s_next_check_ms) >= 0))
        return;
    s_check_now = false;
    s_next_check_ms = now + (uint32_t) hours * 3600000UL;
    if (strcmp(repo, s_check_repo) != 0)
        s_latest[0] = 0;  // a different source: forget what the old one offered
    strlcpy(s_check_repo, repo, sizeof(s_check_repo));
    s_checking = true;
    if (xTaskCreate(check_task, "gh_check", 8192, nullptr, 1, nullptr) != pdPASS)
        s_checking = false;
}

void check_json(JsonObject out) {
    {
        StateLock lock;
        out["auto_check"] = !g_app.update_check_off;
        out["check_hours"] = g_app.update_check_hours;
    }
    out["checking"] = (bool) s_checking;
    if (s_checking)
        return;  // the task is writing the fields below
    if (s_latest[0]) {
        out["latest"] = s_latest;
        out["available"] = compare_versions(s_latest, FW_VERSION) > 0;
    } else {
        out["available"] = false;
    }
    if (s_checked) {
        out["checked_ago_s"] = (millis() - s_checked_ms) / 1000;
        if (s_check_error[0])
            out["error"] = s_check_error;
    }
}

const char *available_version() {
    return !s_checking && s_latest[0] && compare_versions(s_latest, FW_VERSION) > 0 ? s_latest : nullptr;
}

bool busy() { return s_state == State::DOWNLOADING; }
bool reboot_due() { return s_state == State::DONE; }

void status_json(JsonObject out) {
    State s = s_state;
    out["state"] = state_name(s);
    if (s == State::IDLE)
        return;
    out["tag"] = s_tag;
    out["progress"] = s_progress;
    if (s == State::FAILED)
        out["error"] = s_error;
}

}  // namespace updater
