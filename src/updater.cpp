#include "updater.h"

#include <Update.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>

#include "engine.h"

extern Engine g_engine;

namespace updater {

namespace {
enum class State : uint8_t { IDLE, DOWNLOADING, DONE, FAILED };

volatile State s_state = State::IDLE;
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

void download() {
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
        return fail("out of memory");

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
    if (e != ESP_OK || len < 0)
        err = String("could not reach GitHub (") + esp_err_to_name(e != ESP_OK ? e : ESP_FAIL) + ")";
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
    if (!err.isEmpty())
        return fail(err);
    log_i("Update %s written (%u bytes)", s_tag, (unsigned) len);
    s_progress = 100;
    s_state = State::DONE;
}

void task(void *) {
    g_engine.set_suspended(true);  // no RF while flashing
    log_i("Updating from %s", s_url);
    download();
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
