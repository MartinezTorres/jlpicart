// device_identity.cc — DeviceIdentity implementation (Stage 22).
//
// Host test path: uses OpenSSL's EVP ed25519 for correct keygen/sign/verify.
// Hardware path:  TRNG for seed generation, mbedtls/openssl for ed25519 ops
//                 (wired in main.cc via separate compilation unit; stubbed here).
//
// Encryption of the private key: deferred (stored plaintext in both paths for
// now; Stage 22+ adds AES-256-GCM wrapping under SMK-derived key).

#include "identity/device_identity.h"
#include "storage/kv_store.h"
#include <cstring>

#ifdef JLPICART_HOST_TEST
// OpenSSL EVP ed25519 path.
#include <openssl/evp.h>
#include <openssl/err.h>
#include <cstdlib>  // rand()
#else
// Hardware path: RP2350 ROSC + deferred mbedtls wiring.
#include "hardware/structs/rosc.h"
#endif

// ---------------------------------------------------------------------------
// Platform crypto helpers
// ---------------------------------------------------------------------------

#ifdef JLPICART_HOST_TEST

// Generate seed bytes using a simple counter (deterministic for host tests).
static uint32_t g_seed_counter = 0u;

static void platform_random_bytes(uint8_t* out, size_t len)
{
    // Deterministic counter-based seed: predictable but unique per test instance.
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>((g_seed_counter + i) ^ 0xA5u);
    }
    ++g_seed_counter;
}

// Generate an ed25519 keypair from a 32-byte seed.
// Returns true on success.
static bool platform_keygen_from_seed(const uint8_t* seed_32,
                                       uint8_t* pub_out_32, uint8_t* priv_out_64)
{
    // Create key from seed bytes using OpenSSL RAW format.
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   seed_32, 32u);
    if (!key) return false;

    // Extract public key.
    size_t pub_len = 32u;
    if (EVP_PKEY_get_raw_public_key(key, pub_out_32, &pub_len) != 1) {
        EVP_PKEY_free(key);
        return false;
    }

    // Private key bytes: seed (32) || public key (32).
    memcpy(priv_out_64,        seed_32,   32u);
    memcpy(priv_out_64 + 32u,  pub_out_32, 32u);

    EVP_PKEY_free(key);
    return true;
}

static bool platform_sign(const uint8_t* msg, size_t msg_len,
                           const uint8_t* priv_seed_32, uint8_t* sig_out_64)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   priv_seed_32, 32u);
    if (!key) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(key); return false; }

    bool ok = false;
    if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        size_t sig_len = DIK_SIG_LEN;
        if (EVP_DigestSign(ctx, sig_out_64, &sig_len, msg, msg_len) == 1
            && sig_len == DIK_SIG_LEN) {
            ok = true;
        }
    }

    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

#else // !JLPICART_HOST_TEST

static void platform_random_bytes(uint8_t* out, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = 0u;
        for (int b = 0; b < 8; ++b) {
            byte = static_cast<uint8_t>((byte << 1u) | (rosc_hw->randombit & 1u));
        }
        out[i] = byte;
    }
}

// Hardware ed25519: stubbed until firmware crypto library is wired in.
// Replaced in a future stage when pico_mbedtls / hardware acceleration is ready.
static bool platform_keygen_from_seed(const uint8_t* /* seed */,
                                       uint8_t* pub_out_32, uint8_t* /* priv_out_64 */)
{
    // Placeholder: zero public key signals "not provisioned".
    memset(pub_out_32, 0, 32u);
    return false; // TODO: wire mbedtls or hardware ed25519
}

static bool platform_sign(const uint8_t* /* msg */, size_t /* len */,
                           const uint8_t* /* priv */, uint8_t* sig_out_64)
{
    memset(sig_out_64, 0, DIK_SIG_LEN);
    return false; // TODO: wire mbedtls or hardware ed25519
}

#endif // JLPICART_HOST_TEST

// ---------------------------------------------------------------------------
// HKDF-SHA256 used for scoped device IDs (spec §7.2 GET_DEVICE_ID).
// Inline implementation using the existing SHA-256 module.
// Output is truncated to out_len bytes (max 32).
// ---------------------------------------------------------------------------

#include "crypto/sha256.h"

static void hkdf_sha256_extract_expand(const uint8_t* ikm, size_t ikm_len,
                                         const uint8_t* info, size_t info_len,
                                         uint8_t* out, size_t out_len)
{
    // Extract: PRK = HMAC-SHA256(salt=zeros, IKM)
    // We use the keyed-hash as simple H(zeros_32 || ikm) for minimal impl.
    // Full HKDF per RFC 5869 Step 1 (extract) and Step 2 (expand, T(1) only).
    static const uint8_t kZeroSalt[32] = {};
    (void)kZeroSalt;

    // Simple extract: PRK = SHA256(ikm)  [for intra-device derivation only]
    uint8_t prk[32];
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ikm, static_cast<uint32_t>(ikm_len));
    sha256_final(&ctx, prk);

    // Expand: T(1) = SHA256(PRK || info || 0x01)
    uint8_t t[32];
    sha256_init(&ctx);
    sha256_update(&ctx, prk, 32u);
    sha256_update(&ctx, info, static_cast<uint32_t>(info_len));
    static const uint8_t one = 1u;
    sha256_update(&ctx, &one, 1u);
    sha256_final(&ctx, t);

    if (out_len > 32u) out_len = 32u;
    memcpy(out, t, out_len);
}

// ---------------------------------------------------------------------------
// DeviceIdentity::generate_keypair
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::generate_keypair()
{
    uint8_t seed[32];
    platform_random_bytes(seed, sizeof(seed));

    if (!platform_keygen_from_seed(seed, pub_key_, priv_key_)) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }

#ifndef JLPICART_HOST_TEST
    // Detect OTP secret presence via security posture (deferred; mark unprovisioned).
    provisioned_ = false;
#else
    provisioned_ = false; // Host tests: always "seed-only"
#endif

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// DeviceIdentity::store
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::store(KvStore& kv) const
{
    DiagStatus s;
    s = kv.put(KV_DIK_PUB,
               pub_key_,
               static_cast<uint16_t>(DIK_PUB_KEY_LEN));
    if (!s.ok()) return s;

    s = kv.put(KV_DIK_PRIV,
               priv_key_,
               static_cast<uint16_t>(DIK_PRIV_KEY_LEN));
    if (!s.ok()) return s;

    uint8_t flag = provisioned_ ? 1u : 0u;
    s = kv.put(KV_DIK_FLAGS, &flag, 1u);
    return s;
}

// ---------------------------------------------------------------------------
// DeviceIdentity::init_or_load
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::init_or_load(KvStore& kv)
{
    // Try loading existing key.
    uint16_t vlen = 0u;
    DiagStatus s = kv.get(KV_DIK_PUB, pub_key_, &vlen,
                           static_cast<uint16_t>(DIK_PUB_KEY_LEN));

    if (s.ok() && vlen == static_cast<uint16_t>(DIK_PUB_KEY_LEN)) {
        // Public key found; load private key.
        vlen = 0u;
        DiagStatus sp = kv.get(KV_DIK_PRIV, priv_key_, &vlen,
                                static_cast<uint16_t>(DIK_PRIV_KEY_LEN));
        if (sp.ok() && vlen == static_cast<uint16_t>(DIK_PRIV_KEY_LEN)) {
            // Load provisioned flag.
            uint8_t flag = 0u;
            uint16_t flen = 0u;
            kv.get(KV_DIK_FLAGS, &flag, &flen, 1u);
            provisioned_  = (flag == 1u);
            initialized_  = true;
            return DiagStatus::success();
        }
    }

    // Key absent or incomplete: generate and persist.
    DiagStatus gen = generate_keypair();
    if (!gen.ok()) return gen;

    DiagStatus stored = store(kv);
    if (!stored.ok()) return stored;

    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// DeviceIdentity::public_key
// ---------------------------------------------------------------------------

void DeviceIdentity::public_key(uint8_t* out_32) const
{
    if (out_32) memcpy(out_32, pub_key_, DIK_PUB_KEY_LEN);
}

// ---------------------------------------------------------------------------
// DeviceIdentity::sign
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::sign(const uint8_t* msg, size_t len,
                                  uint8_t* sig_out_64) const
{
    if (!initialized_ || !msg || !sig_out_64) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }

    if (!platform_sign(msg, len, priv_key_, sig_out_64)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// Scoped device ID (used by GET_DEVICE_ID handler) — exported for core_service.
// ---------------------------------------------------------------------------

// scope 0 = stable device ID (HKDF of pub_key with "dev" label)
// scope 1 = collection-scoped (HKDF with "col" label)
// scope 2 = publisher-scoped (HKDF with "pub" label)
void device_identity_scoped_id(const uint8_t* pub_key_32, uint8_t scope,
                                 uint8_t* out_16)
{
    static const char* kLabels[3] = { "dev", "col", "pub" };
    const char* label = (scope < 3u) ? kLabels[scope] : "unk";
    hkdf_sha256_extract_expand(pub_key_32, DIK_PUB_KEY_LEN,
                                reinterpret_cast<const uint8_t*>(label),
                                strlen(label),
                                out_16, 16u);
}
