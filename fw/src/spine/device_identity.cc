// device_identity.cc — DeviceIdentity backed by FAT file.
//
// Host test path: OpenSSL EVP ed25519.
// Hardware path: pico_rand + Monocypher ed25519.

#include "spine/device_identity.h"
#include "filesystem/fat_util.h"
#include "crypto/smk.h"
#include <cstring>

#include "crypto/monocypher/monocypher.h"

#ifdef JLPICART_HOST_TEST
#include <openssl/evp.h>
#include <openssl/err.h>
#else
#include "pico/rand.h"
#endif

// ---------------------------------------------------------------------------
// Platform crypto helpers (unchanged from original)
// ---------------------------------------------------------------------------

#ifdef JLPICART_HOST_TEST

static uint32_t g_seed_counter = 0u;

static void platform_random_bytes(uint8_t* out, size_t len)
{
    for (size_t i = 0; i < len; ++i)
        out[i] = static_cast<uint8_t>((g_seed_counter + i) ^ 0xA5u);
    ++g_seed_counter;
}

static bool platform_keygen_from_seed(const uint8_t* seed_32,
                                       uint8_t* pub_out_32, uint8_t* priv_out_64)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   seed_32, 32u);
    if (!key) return false;
    size_t pub_len = 32u;
    if (EVP_PKEY_get_raw_public_key(key, pub_out_32, &pub_len) != 1) {
        EVP_PKEY_free(key); return false;
    }
    memcpy(priv_out_64,       seed_32,    32u);
    memcpy(priv_out_64 + 32u, pub_out_32, 32u);
    EVP_PKEY_free(key);
    return true;
}

static bool platform_sign(const uint8_t* msg, size_t msg_len,
                           const uint8_t* priv_key, uint8_t* sig_out_64)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                                   priv_key, 32u);
    if (!key) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    bool ok = false;
    if (ctx && EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, key) == 1) {
        size_t sig_len = DIK_SIG_LEN;
        if (EVP_DigestSign(ctx, sig_out_64, &sig_len, msg, msg_len) == 1
            && sig_len == DIK_SIG_LEN) {
            ok = true;
        }
    }
    if (ctx) EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

#else // !JLPICART_HOST_TEST

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

static bool platform_keygen_from_seed(const uint8_t* seed_32,
                                       uint8_t* pub_out_32, uint8_t* priv_out_64)
{
    uint8_t seed_copy[32u];
    memcpy(seed_copy, seed_32, 32u);
    crypto_eddsa_key_pair(priv_out_64, pub_out_32, seed_copy);
    crypto_wipe(seed_copy, sizeof(seed_copy));
    return true;
}

static bool platform_sign(const uint8_t* msg, size_t msg_len,
                           const uint8_t* priv_key, uint8_t* sig_out_64)
{
    crypto_eddsa_sign(sig_out_64, priv_key, msg, msg_len);
    return true;
}

#endif // JLPICART_HOST_TEST

// ---------------------------------------------------------------------------
// device_identity_scoped_id
// ---------------------------------------------------------------------------

#include "crypto/sha256.h"

void device_identity_scoped_id(const uint8_t* pub_key_32, uint8_t scope,
                                uint8_t* out_16)
{
    // HKDF-SHA256 label: "dev"|"col"|"pub" depending on scope.
    static const char* labels[] = { "dev", "col", "pub" };
    const char* label = (scope < 3u) ? labels[scope] : "dev";

    // Simple derivation: SHA-256(pub_key || label || 0x00) → first 16 bytes.
    uint8_t input[32u + 4u];
    memcpy(input, pub_key_32, 32u);
    size_t label_len = strlen(label);
    memcpy(input + 32u, label, label_len < 4u ? label_len : 3u);
    input[32u + 3u] = 0u;

    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256(input, sizeof(input), digest);
    memcpy(out_16, digest, 16u);
}

// ---------------------------------------------------------------------------
// generate_keypair
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::generate_keypair()
{
    uint8_t seed[32];
    platform_random_bytes(seed, sizeof(seed));
    if (!platform_keygen_from_seed(seed, pub_key_, priv_key_))
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    provisioned_ = false;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// store — write 137-byte file: pub[32] | nonce[24] | mac[16] | priv_ct[64] | flags[1]
// ---------------------------------------------------------------------------

static constexpr size_t DIK_PRIV_STORED_LEN = 24u + 16u + DIK_PRIV_KEY_LEN; // 104

DiagStatus DeviceIdentity::store(const uint8_t* wrap_key_32) const
{
    // Derive effective wrap key.
    uint8_t eff_key[32];
    if (wrap_key_32) {
        memcpy(eff_key, wrap_key_32, 32u);
    } else {
        uint8_t smk[32];
        smk_derive(nullptr, smk);
        smk_derive_ns_key(smk, "dik.priv", eff_key);
        crypto_wipe(smk, sizeof(smk));
    }

    uint8_t nonce[24];
    platform_random_bytes(nonce, sizeof(nonce));

    uint8_t stored_priv[DIK_PRIV_STORED_LEN];
    uint8_t* mac = stored_priv + 24u;
    uint8_t* ct  = stored_priv + 24u + 16u;
    memcpy(stored_priv, nonce, 24u);
    crypto_aead_lock(ct, mac, eff_key, nonce,
                     nullptr, 0u, priv_key_, DIK_PRIV_KEY_LEN);

    crypto_wipe(eff_key, sizeof(eff_key));
    crypto_wipe(nonce,   sizeof(nonce));

    // Compose 137-byte file.
    uint8_t file_buf[32u + DIK_PRIV_STORED_LEN + 1u];
    memcpy(file_buf,             pub_key_,    32u);
    memcpy(file_buf + 32u,       stored_priv, DIK_PRIV_STORED_LEN);
    file_buf[32u + DIK_PRIV_STORED_LEN] = provisioned_ ? 1u : 0u;

    fat_ensure_dir("1:/system");
    if (!fat_write_file(PATH, file_buf, sizeof(file_buf)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// init_or_load
// ---------------------------------------------------------------------------

DiagStatus DeviceIdentity::init_or_load(const uint8_t* wrap_key_32)
{
    static constexpr size_t FILE_LEN = 32u + DIK_PRIV_STORED_LEN + 1u; // 137

    uint8_t file_buf[FILE_LEN];
    size_t  actual = 0;
    if (fat_read_file(PATH, file_buf, sizeof(file_buf), &actual)
        && actual == FILE_LEN) {

        // Try to decrypt private key.
        const uint8_t* pub_raw   = file_buf;
        const uint8_t* nonce     = file_buf + 32u;
        const uint8_t* mac       = file_buf + 32u + 24u;
        const uint8_t* ct        = file_buf + 32u + 24u + 16u;
        uint8_t        flags     = file_buf[32u + DIK_PRIV_STORED_LEN];

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
                                     nullptr, 0u, ct, DIK_PRIV_KEY_LEN);
        crypto_wipe(eff_key, sizeof(eff_key));

        if (ok == 0) {
            memcpy(pub_key_, pub_raw, 32u);
            provisioned_ = (flags == 1u);
            initialized_ = true;
            return DiagStatus::success();
        }
        // Decryption failed — fall through to regenerate.
        crypto_wipe(priv_key_, sizeof(priv_key_));
    }

    // Generate a fresh keypair and persist it.
    DiagStatus gen = generate_keypair();
    if (!gen.ok()) return gen;

    DiagStatus s = store(wrap_key_32);
    if (!s.ok()) return s;

    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// public_key / sign
// ---------------------------------------------------------------------------

void DeviceIdentity::public_key(uint8_t* out_32) const
{
    if (out_32) memcpy(out_32, pub_key_, DIK_PUB_KEY_LEN);
}

DiagStatus DeviceIdentity::sign(const uint8_t* msg, size_t len,
                                  uint8_t* sig_out_64) const
{
    if (!initialized_ || !msg || !sig_out_64)
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (!platform_sign(msg, len, priv_key_, sig_out_64))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}
