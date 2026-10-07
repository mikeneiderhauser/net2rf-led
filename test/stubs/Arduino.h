#pragma once
// Minimal Arduino-ESP32 stand-ins so src/engine.cpp builds on the host (test_engine only).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <cctype>

#define IRAM_ATTR
#define CHANGE 3
#define log_i(...) ((void) 0)
#define log_w(...) ((void) 0)
#define log_e(...) ((void) 0)
#define log_d(...) ((void) 0)

// Arduino's String, close enough for the engine: built on std::string, which ArduinoJson knows.
class String : public std::string {
 public:
    String() = default;
    String(const char *s) : std::string(s ? s : "") {}
    String(const std::string &s) : std::string(s) {}
    String(unsigned long v) : std::string(std::to_string(v)) {}
    String(int v) : std::string(std::to_string(v)) {}
    String operator+(const String &o) const { return String(static_cast<const std::string &>(*this) + o); }
    String operator+(const char *o) const { return String(static_cast<const std::string &>(*this) + o); }
    template<typename T, typename = typename std::enable_if<std::is_integral<T>::value>::type>
    String operator+(T v) const {
        return String(static_cast<const std::string &>(*this) + std::to_string(v));
    }
    String substring(size_t from, size_t to) const { return String(substr(from, to - from)); }
    bool equalsIgnoreCase(const char *o) const {
        if (size() != strlen(o))
            return false;
        for (size_t i = 0; i < size(); i++)
            if (tolower((unsigned char) (*this)[i]) != tolower((unsigned char) o[i]))
                return false;
        return true;
    }
    bool endsWith(const char *o) const {
        size_t n = strlen(o);
        return size() >= n && compare(size() - n, n, o) == 0;
    }
    int indexOf(const char *o) const {
        size_t at = find(o);
        return at == npos ? -1 : (int) at;
    }
    void toLowerCase() {
        for (char &c : *this)
            c = (char) tolower((unsigned char) c);
    }
    void trim() {
        size_t a = find_first_not_of(" \t\r\n"), b = find_last_not_of(" \t\r\n");
        *this = a == npos ? String() : String(substr(a, b - a + 1));
    }
};

class IPAddress {
 public:
    explicit IPAddress(uint32_t v = 0) : v_(v) {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : v_(a | b << 8 | c << 16 | (uint32_t) d << 24) {}
    explicit operator uint32_t() const { return v_; }
    bool fromString(const char *s) {
        unsigned a, b, c, d;
        if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
            return false;
        v_ = a | b << 8 | c << 16 | d << 24;
        return true;
    }
    String toString() const {
        char b[16];
        snprintf(b, sizeof(b), "%u.%u.%u.%u", v_ & 255, v_ >> 8 & 255, v_ >> 16 & 255, v_ >> 24);
        return String(b);
    }

 private:
    uint32_t v_;
};

uint32_t millis();
uint32_t micros();
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}
inline void attachInterrupt(int, void (*)(), int) {}
inline void detachInterrupt(int) {}
template<typename T, typename L, typename H> T constrain(T v, L lo, H hi) { return v < lo ? lo : v > hi ? hi : v; }

struct rmt_data_t {
    uint32_t duration0 : 15, level0 : 1, duration1 : 15, level1 : 1;
};

#if !defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38)
inline size_t strlcpy(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (n) {
        size_t c = l < n - 1 ? l : n - 1;
        memcpy(d, s, c);
        d[c] = 0;
    }
    return l;
}
#endif
