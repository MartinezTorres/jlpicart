// device_identity.cc — DeviceIdentity implementation (Stage 22/24).
//
// Host test path: uses OpenSSL's EVP ed25519 for correct keygen/sign/verify.
//   platform_sign() receives priv_key_[64] = seed[32] || pub[32]; uses seed only.
//
// Hardware path (Stage 24): uses pico_rand (RP2350 hardware TRNG) for entropy
//   and Monocypher ed25519 for keygen/sign.  Monocypher secret_key[64] is an
//   expanded/clamped form of the seed — NOT seed||pub.  priv_key_[64] stores
//   Monocypher secret_key directly.
//
// Encryption of the private key: deferred to Stage 25 (AES-256-GCM under SMK).

#include "identity/device_identity.h"
#include "storage/kv_store.h"
#include "crypto/smk.h"
#include <cstring>

// Monocypher is used in both paths for dik.priv AEAD wrapping (Stage 25).
// It is also used for ed25519 keygen/sign in the hardware (non-host) path.
#include "crypto/monocypher/monocypher.h"

#ifdef JLPICART_HOST_TEST
// OpenSSL EVP ed25519 for keygen/sign in host tests.
#include <openssl/evp.h>
#include <openssl/err.h>
#else
// pico_rand for hardware TRNG entropy.
#include "pico/rand.h"
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

// priv_key points to priv_key_[64] = seed[32] || pub[32]; OpenSSL needs seed only.
static bool platform_sign(const uint8_t* msg, size_t msg_len,
                           const uint8_t* priv_key, uint8_t* sig_out_64)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   priv_key, 32u);
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

// Fill out[0..len-1] with cryptographically random bytes from the RP2350 TRNG.
static void platform_random_bytes(uint8_t* out, size_t len)
{
    size_t i = 0u;
    while (i + 8u <= len) {
        const uint64_t r = get_rand_64();
        memcpy(out + i, &r, 8u);
        i += 8u;
    }
    if (i < len) {
        const uint64_t r = get_rand_64();
        memcpy(out + i, &r, len - i);
    }
}

// Generate an ed25519 keypair from a 32-byte seed using Monocypher.
// priv_out_64 receives Monocypher's 64-byte secret_key (expanded form of seed).
// pub_out_32 receives the 32-byte public key.
static bool platform_keygen_from_seed(const uint8_t* seed_32,
                                       uint8_t* pub_out_32, uint8_t* priv_out_64)
{
    // Monocypher takes a non-const seed (it wipes it after use); copy first.
    uint8_t seed_copy[32u];
    memcpy(seed_copy, seed_32, 32u);
    crypto_eddsa_key_pair(priv_out_64, pub_out_32, seed_copy);
    crypto_wipe(seed_copy, sizeof(seed_copy));
    return true;
}

// Sign msg using Monocypher ed25519.
// priv_key points to priv_key_[64] — Monocypher's 64-byte secret_key.
static bool platform_sign(const uint8_t* msg, size_t msg_len,
                           const uint8_t* priv_key, uint8_t* sig_out_64)
{
    crypto_eddsa_sign(sig_out_64, priv_key, msg, msg_len);
    return true;
}

#endif // JLPICART_HOST_TEST

// (Scoped device ID derivation uses hkdf_sha256 from crypto/smk.h.)

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
// DeviceIdentity::store — persist keypair; dik.priv encrypted with wrap_key
// ---------------------------------------------------------------------------
//
// Stored format for dik.priv (Stage 25):
//   nonce[24] || mac[16] || ciphertext[64] = 104 bytes (XChacha20-Poly1305)
//
// If wrap_key_32 is null, a zero-derived key is used (unprovisioned units).

static constexpr uint16_t DIK_PRIV_STORED_LEN =
    24u + 16u + static_cast<uint16_t>(DIK_PRIV_KEY_LEN); // 104 bytes

DiagStatus DeviceIdentity::store(KvStore& kv, const uint8_t* wrap_key_32) const
{
    DiagStatus s;

    // Public key is not secret — store plaintext.
    s = kv.put(KV_DIK_PUB, pub_key_, static_cast<uint16_t>(DIK_PUB_KEY_LEN));
    if (!s.ok()) return s;

    // Derive effective wrapping key.
    uint8_t eff_key[32];
    if (wrap_key_32) {
        memcpy(eff_key, wrap_key_32, 32u);
    } else {
        // Zero-derived key: deterministic, not secret (unprovisioned behaviour).
        uint8_t smk[32];
        smk_derive(nullptr, smk);
        smk_derive_ns_key(smk, "dik.priv", eff_key);
        crypto_wipe(smk, sizeof(smk));
    }

    // Generate a fresh nonce for each store.
    uint8_t nonce[24];
    platform_random_bytes(nonce, sizeof(nonce));

    // Encrypt: nonce[24] || mac[16] || ct[64]
    uint8_t stored[DIK_PRIV_STORED_LEN];
    uint8_t* mac = stored + 24u;
    uint8_t* ct  = stored + 24u + 16u;
    memcpy(stored, nonce, 24u);
    crypto_aead_lock(ct, mac, eff_key, nonce,
                     nullptr, 0u,
                     priv_key_, DIK_PRIV_KEY_LEN);

    crypto_wipe(eff_key, sizeof(eff_key));
    crypto_wipe(nonce,   sizeof(nonce));

    s = kv.put(KV_DIK_PRIV, stored, DIK_PRIV_STORED_LEN);
    if (!s.ok()) return s;

    uint8_t flag = provisioned_ ? 1u : 0u;
    s = kv.put(KV_DIK_FLAGS, &flag, 1u);
    return s;
}

// ---------------------------------------------------------------------------
// DeviceIdentity::init_or_load
// ---------------------------------------------------------------------------
//
// Load or generate the DIK.  Private key migration:
//   64-byte value  → Stage 22 plaintext format; load directly, re-encrypt.
//   104-byte value → Stage 25 encrypted format; decrypt with wrap_key.
//   Other / error  → regenerate.
//
// If decryption fails (wrong key or corruption), regenerates a fresh pair.

DiagStatus DeviceIdentity::init_or_load(KvStore& kv, const uint8_t* wrap_key_32)
{
    // 1 — Try loading the public key.
    uint16_t vlen = 0u;
    DiagStatus s = kv.get(KV_DIK_PUB, pub_key_, &vlen,
                           static_cast<uint16_t>(DIK_PUB_KEY_LEN));
    if (s.ok() && vlen == static_cast<uint16_t>(DIK_PUB_KEY_LEN)) {
        // 2 — Public key present; try loading private key.
        uint8_t stored[DIK_PRIV_STORED_LEN];
        uint16_t plen = 0u;
        DiagStatus sp = kv.get(KV_DIK_PRIV, stored, &plen, DIK_PRIV_STORED_LEN);

        if (sp.ok() && plen == static_cast<uint16_t>(DIK_PRIV_KEY_LEN)) {
            // Stage 22 plaintext format: copy directly and re-encrypt.
            memcpy(priv_key_, stored, DIK_PRIV_KEY_LEN);
            uint8_t flag = 0u; uint16_t flen = 0u;
            kv.get(KV_DIK_FLAGS, &flag, &flen, 1u);
            provisioned_ = (flag == 1u);
            initialized_ = true;
            // Migrate: re-store in encrypted format.
            (void)store(kv, wrap_key_32);
            return DiagStatus::success();
        }

        if (sp.ok() && plen == DIK_PRIV_STORED_LEN) {
            // Stage 25 encrypted format: decrypt.
            const uint8_t* nonce = stored;
            const uint8_t* mac   = stored + 24u;
            const uint8_t* ct    = stored + 24u + 16u;

            // Derive effective wrapping key.
            uint8_t eff_key[32];
            if (wrap_key_32) {
                memcpy(eff_key, wrap_key_32, 32u);
            } else {
                uint8_t smk[32];
                smk_derive(nullptr, smk);
                smk_derive_ns_key(smk, "dik.priv", eff_key);
                crypto_wipe(smk, sizeof(smk));
            }

            int ok = crypto_aead_unlock(priv_key_, mac, eff_key, nonce,
                                         nullptr, 0u,
                                         ct, DIK_PRIV_KEY_LEN);
            crypto_wipe(eff_key, sizeof(eff_key));

            if (ok == 0) {
                // Decryption succeeded.
                uint8_t flag = 0u; uint16_t flen = 0u;
                kv.get(KV_DIK_FLAGS, &flag, &flen, 1u);
                provisioned_ = (flag == 1u);
                initialized_ = true;
                return DiagStatus::success();
            }
            // Decryption failed (wrong key or corruption): fall through to regenerate.
            crypto_wipe(priv_key_, sizeof(priv_key_));
        }
    }

    // 3 — Key absent, incomplete, or decryption failed: generate a fresh pair.
    DiagStatus gen = generate_keypair();
    if (!gen.ok()) return gen;

    DiagStatus stored = store(kv, wrap_key_32);
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

    uint8_t tmp[32];
    hkdf_sha256(pub_key_32, DIK_PUB_KEY_LEN,
                nullptr, 0u,
                reinterpret_cast<const uint8_t*>(label), strlen(label),
                tmp);
    memcpy(out_16, tmp, 16u);
    crypto_wipe(tmp, sizeof(tmp));
}
