#pragma once
// In-memory NVS: test_config can save, inspect and reload settings records.
#include <map>
#include <string>
#include <vector>

#include "Arduino.h"

class Preferences {
 public:
    static std::map<std::string, std::vector<uint8_t>> &store() {
        static std::map<std::string, std::vector<uint8_t>> s;
        return s;
    }
    bool begin(const char *ns, bool = false) {
        this->ns_ = ns;
        return true;
    }
    void end() {}
    size_t getBytesLength(const char *k) {
        auto it = store().find(this->ns_ + "/" + k);
        return it == store().end() ? 0 : it->second.size();
    }
    size_t getBytes(const char *k, void *buf, size_t len) {
        auto it = store().find(this->ns_ + "/" + k);
        if (it == store().end())
            return 0;
        size_t n = std::min(len, it->second.size());
        memcpy(buf, it->second.data(), n);
        return n;
    }
    size_t putBytes(const char *k, const void *buf, size_t len) {
        store()[this->ns_ + "/" + k].assign((const uint8_t *) buf, (const uint8_t *) buf + len);
        return len;
    }
    uint8_t getUChar(const char *, uint8_t d = 0) { return d; }
    size_t putUChar(const char *, uint8_t) { return 1; }
    bool clear() {
        store().clear();
        return true;
    }

 private:
    std::string ns_;
};
