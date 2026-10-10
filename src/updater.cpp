#include "updater.h"


#include <version.h>

#include "config.h"

namespace updater {

using net2rf::compare_versions;

namespace {

bool valid_name(const char *s, size_t max_len) {
    size_t n = strlen(s);
    if (n == 0 || n > max_len || s[0] == '.')
        return false;
    for (size_t i = 0; i < n; i++)
        if (!isalnum((unsigned char) s[i]) && s[i] != '.' && s[i] != '_' && s[i] != '-')
            return false;
    return true;
}

// The latest release, as last reported by a browser showing the web UI (see updater.h). Text only, written and
// read by the web / loop task.
char s_latest[41] = "";
uint32_t s_latest_ms = 0;

}  // namespace

bool valid_repo(const char *repo) {
    const char *slash = strchr(repo, '/');
    if (!slash || strchr(slash + 1, '/'))
        return false;
    String owner = String(repo).substring(0, slash - repo);
    return valid_name(owner.c_str(), 39) && valid_name(slash + 1, 100);
}

bool note_latest(const char *tag) {
    if (!tag || !valid_name(tag, sizeof(s_latest) - 1))
        return false;
    strlcpy(s_latest, tag, sizeof(s_latest));
    s_latest_ms = millis();
    return true;
}

void forget() { s_latest[0] = 0; }

const char *available_version() {
    return s_latest[0] && compare_versions(s_latest, FW_VERSION) > 0 ? s_latest : nullptr;
}

void check_json(JsonObject out) {
    out["available"] = available_version() != nullptr;
    if (!s_latest[0])
        return;
    out["latest"] = s_latest;
    out["reported_ago_s"] = (millis() - s_latest_ms) / 1000;
}

}  // namespace updater
