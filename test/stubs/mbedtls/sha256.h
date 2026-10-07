#pragma once
// Not a real SHA-256: a stand-in so config.cpp links (test_config doesn't test passwords).
#include <cstddef>
#include <cstdint>
#include <cstring>
struct mbedtls_sha256_context {
    uint8_t acc[32];
    size_t n;
};
inline void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof(*c)); }
inline int mbedtls_sha256_starts(mbedtls_sha256_context *, int) { return 0; }
inline int mbedtls_sha256_update(mbedtls_sha256_context *c, const uint8_t *d, size_t l) {
    for (size_t i = 0; i < l; i++)
        c->acc[c->n++ % 32] ^= d[i];
    return 0;
}
inline int mbedtls_sha256_finish(mbedtls_sha256_context *c, uint8_t *out) {
    memcpy(out, c->acc, 32);
    return 0;
}
inline void mbedtls_sha256_free(mbedtls_sha256_context *) {}
