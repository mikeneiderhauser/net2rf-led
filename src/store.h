#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Board records kept as JSON files on the 128 KB data partition (LittleFS, partition label "spiffs"): the boot
// log, the transmitters receiver mode has heard, and saved raw captures. Formats: lib/net2rf_store.
// Everything degrades to "nothing stored" if the partition is missing or will not mount.
namespace store {

void begin();  // mounts (formatting an empty partition), records this boot
void loop();   // writes what changed, at most once a minute and never during a firmware update

bool mounted();
uint32_t boot_no();

// Receiver mode decoded a transmission (first frame of it). Safe from any task; memory only.
void note_seen(uint8_t protocol, const uint8_t *packet, int16_t rssi_dbm);

// Whether the transmitters receiver mode hears are written to flash (off by default: it wears the flash, slowly).
// Off, the list is still kept in memory and shown, and is gone at the next restart.
bool record_seen();
bool set_record_seen(bool on);

// {mounted, total_bytes, used_bytes, boot_no, writes, record_seen, boot_log_paused, files: [{name, bytes}]}
void status_json(JsonObject out);
void boots_json(JsonObject out);
void seen_json(JsonObject out);
// One entry per used slot, without the pulses: [{slot, freq, us, boot, decoded, truncated, rssi_dbm, note, pulses_n}]
void captures_json(JsonArray out);
bool capture_json(uint8_t slot, JsonDocument &doc);  // the whole stored document

// Keeps the receiver's capture `id` (see GET /api/rx/capture). Returns the slot, -1 if there is no such capture,
// -2 if every slot is taken or the write failed.
int save_capture(uint32_t id, const char *note);
bool delete_capture(uint8_t slot);
// what: "boots", "seen", "captures" or "all". False for anything else.
bool clear(const char *what);

// A stored file by name ("boots.json"), for downloading. False if there is no such file.
bool read_file(const String &name, String &out);
// Deletes one, and what is held in memory with it: boots.json restarts the count (this boot is logged again),
// seen.json empties the heard list, store.json puts recording back to off. False if there is no such file.
bool delete_file(const String &name);

}  // namespace store
