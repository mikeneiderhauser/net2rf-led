#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Firmware update from a GitHub release: the controller downloads the release's .bin over HTTPS and flashes
// it, then reboots (the usual rollback guard applies). The web UI finds the latest release and asks for it by
// tag and file name; the repository comes from the settings (AppConfig::update_repo).
namespace updater {

static const char *const DEFAULT_REPO = "mikeneiderhauser/net2rf-led";

bool valid_repo(const char *repo);  // "owner/name", GitHub's character set

// Start downloading https://github.com/<repo>/releases/download/<tag>/<asset> in the background.
// False (with `err`) if the names are invalid or an update is already running.
bool start(const String &repo, const String &tag, const String &asset, String &err);

bool busy();          // a download is running
bool reboot_due();    // the new firmware is written: reboot to run it
void status_json(JsonObject out);

}  // namespace updater
