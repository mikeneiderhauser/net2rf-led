#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Firmware updates are installed by uploading a .bin to POST /update, either a file picked in the web UI or
// one the browser fetched from the project's release site. The controller never downloads firmware and never
// asks GitHub anything itself: a secure connection costs about 100 KB of heap, which it does not have to spare.
//
// What is left here: the release source setting (AppConfig::update_repo, used by the browser) and the "newer
// version" notice. Nothing in this firmware opens a secure connection.
namespace updater {

static const char *const DEFAULT_REPO = "mikeneiderhauser/net2rf-led";

bool valid_repo(const char *repo);  // "owner/name", GitHub's character set

// The web UI found out which release is the latest and passes it on, so the dashboard and the OLED can say so.
// Kept in memory only (gone at a restart, until a browser reports again). False if `tag` is not a plausible tag.
bool note_latest(const char *tag);
void forget();                     // the release source changed
const char *available_version();   // tag of a release newer than this firmware, or nullptr
void check_json(JsonObject out);   // {available, latest?, reported_ago_s?}

}  // namespace updater
