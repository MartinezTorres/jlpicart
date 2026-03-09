#pragma once
#include <cstddef>
#include <cstdint>

// sha256.h — minimal SHA-256 and HMAC-SHA256 implementation.
// Self-contained, no external dependencies. Works on both firmware and host.
// Used for policy verification and bundle hashing. See spec.md §10.

static constexpr size_t SHA256_DIGEST_SIZE = 32;

struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t  buf[64];
    uint32_t buf_len;
};

void sha256_init  (Sha256Ctx* ctx);
void sha256_update(Sha256Ctx* ctx, const uint8_t* data, size_t len);
void sha256_final (Sha256Ctx* ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

// One-shot hash.
void sha256(const uint8_t* data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);

// HMAC-SHA256.
void hmac_sha256(const uint8_t* key,  size_t key_len,
                 const uint8_t* data, size_t data_len,
                 uint8_t mac[SHA256_DIGEST_SIZE]);
