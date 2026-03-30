// test_bundle_sig.cc — Stage 31: bundle ed25519 signature verification tests.
//
// Verifies:
//   1. policy_get_publisher_anchor() — KvStore lookup round-trip
//   2. bundle_sig_verify() — valid ed25519 signature accepted
//   3. bundle_sig_verify() — wrong signature rejected
//   4. bundle_sig_verify() — wrong algorithm rejected
//   5. bundle_sig_verify() — wrong sig_len rejected
//   6. bundle_sig_verify() — wrong public key rejected
//   7. policy_get_publisher_anchor() — absent key returns false
//   8. policy_get_publisher_anchor() — wrong-length key returns false

#include "content/bundle_sig_verify.h"
#include "content/collection_format.h"
#include "storage/kv_store.h"
#include "storage/flash_device.h"
#include "crypto/monocypher/monocypher.h"
#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Flash and KvStore helpers
// ---------------------------------------------------------------------------

static constexpr size_t TEST_KV_SIZE = 8192u;

static FlashDevice make_flash() {
    return FlashDevice(TEST_KV_SIZE);
}

// ---------------------------------------------------------------------------
// Helper: build a BundleSigEnvelope with the given alg and signature
// ---------------------------------------------------------------------------

static BundleSigEnvelope make_env(const char* alg,
                                   const uint8_t sig[64], uint8_t sig_len) {
    BundleSigEnvelope env = {};
    strncpy(env.alg, alg, sizeof(env.alg) - 1u);
    if (sig) memcpy(env.signature, sig, 64u);
    env.sig_len = sig_len;
    return env;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static int failures = 0;
#define ASSERT(cond) do { \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } \
} while (0)

int main() {
    // Generate a test key pair.
    uint8_t secret_key[64];
    uint8_t public_key[32];
    {
        // Seed with a deterministic byte sequence.
        uint8_t seed[32];
        for (int i = 0; i < 32; ++i) seed[i] = (uint8_t)(0x42 + i);
        crypto_eddsa_key_pair(secret_key, public_key, seed);
    }

    // The "manifest sha256" we'll sign.
    uint8_t manifest_hash[32];
    for (int i = 0; i < 32; ++i) manifest_hash[i] = (uint8_t)(0xAB ^ i);

    // Compute a valid ed25519 signature.
    uint8_t valid_sig[64];
    crypto_eddsa_sign(valid_sig, secret_key, manifest_hash, 32u);

    // -----------------------------------------------------------------------
    // 1. policy_get_publisher_anchor() — round-trip via KvStore
    // -----------------------------------------------------------------------
    {
        FlashDevice flash = make_flash();
        KvStore kv;
        kv.init(flash, 0u, TEST_KV_SIZE);

        uint8_t anchor_out[32] = {};
        // Key absent → returns false.
        ASSERT(!policy_get_publisher_anchor(kv, anchor_out));

        // Store the public key.
        kv.put("pub.anchor", public_key, 32u);

        // Now retrieval succeeds and matches.
        ASSERT(policy_get_publisher_anchor(kv, anchor_out));
        ASSERT(memcmp(anchor_out, public_key, 32u) == 0);
    }

    // -----------------------------------------------------------------------
    // 2. Valid ed25519 signature is accepted.
    // -----------------------------------------------------------------------
    {
        BundleSigEnvelope env = make_env("ed25519", valid_sig, 64u);
        ASSERT(bundle_sig_verify(env, public_key, manifest_hash));
    }

    // -----------------------------------------------------------------------
    // 3. Corrupted signature is rejected.
    // -----------------------------------------------------------------------
    {
        uint8_t bad_sig[64];
        memcpy(bad_sig, valid_sig, 64u);
        bad_sig[0] ^= 0xFF;  // flip first byte
        BundleSigEnvelope env = make_env("ed25519", bad_sig, 64u);
        ASSERT(!bundle_sig_verify(env, public_key, manifest_hash));
    }

    // -----------------------------------------------------------------------
    // 4. Unknown algorithm is rejected regardless of sig content.
    // -----------------------------------------------------------------------
    {
        BundleSigEnvelope env = make_env("ecdsa_secp256k1_sha256", valid_sig, 64u);
        ASSERT(!bundle_sig_verify(env, public_key, manifest_hash));
    }

    // -----------------------------------------------------------------------
    // 5. sig_len != 64 is rejected.
    // -----------------------------------------------------------------------
    {
        BundleSigEnvelope env = make_env("ed25519", valid_sig, 63u);
        ASSERT(!bundle_sig_verify(env, public_key, manifest_hash));
    }

    // -----------------------------------------------------------------------
    // 6. Wrong public key is rejected.
    // -----------------------------------------------------------------------
    {
        uint8_t other_pub[32];
        uint8_t other_sec[64];
        uint8_t seed2[32];
        for (int i = 0; i < 32; ++i) seed2[i] = (uint8_t)(0x77 + i);
        crypto_eddsa_key_pair(other_sec, other_pub, seed2);

        BundleSigEnvelope env = make_env("ed25519", valid_sig, 64u);
        // sig was made with secret_key, verifying with other_pub must fail.
        ASSERT(!bundle_sig_verify(env, other_pub, manifest_hash));
    }

    // -----------------------------------------------------------------------
    // 7. policy_get_publisher_anchor() — absent key returns false.
    //    (already tested in case 1 above, confirmed here explicitly)
    // -----------------------------------------------------------------------
    {
        FlashDevice flash = make_flash();
        KvStore kv;
        kv.init(flash, 0u, TEST_KV_SIZE);
        uint8_t out[32] = {};
        ASSERT(!policy_get_publisher_anchor(kv, out));
    }

    // -----------------------------------------------------------------------
    // 8. policy_get_publisher_anchor() — wrong-length value returns false.
    // -----------------------------------------------------------------------
    {
        FlashDevice flash = make_flash();
        KvStore kv;
        kv.init(flash, 0u, TEST_KV_SIZE);
        uint8_t short_key[16] = {};
        kv.put("pub.anchor", short_key, 16u);
        uint8_t out[32] = {};
        ASSERT(!policy_get_publisher_anchor(kv, out));
    }

    if (failures == 0) {
        printf("test_bundle_sig: all tests PASSED\n");
        return 0;
    }
    printf("test_bundle_sig: %d FAILURES\n", failures);
    return 1;
}
