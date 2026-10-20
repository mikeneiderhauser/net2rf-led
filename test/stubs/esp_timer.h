#pragma once
#include <cstdint>
// A fake microsecond clock: tests set it, and the delay stubs advance it (so waits for a time end).
inline int64_t &esp_timer_fake_us() {
    static int64_t t = 0;
    return t;
}
inline int64_t esp_timer_get_time() { return esp_timer_fake_us(); }
