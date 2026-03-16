// smk.cc — HKDF-SHA256 and SMK / namespace key derivation (Stage 25).
//
// Implements RFC 5869 HKDF-SHA256 using the HMAC-SHA256 from sha256.h.
// Output is always 32 bytes (one T(1) expand block).

#include "crypto/smk.h"
#include "crypto/sha256.h"
#include <cstring>

// Non-optimisable zero wipe for sensitive stack buffers.
static void wipe(void* p, size_t n)
{
    volatile uint8_t* vp = static_cast<volatile uint8_t*>(p);
    for (size_t i = 0u; i < n; ++i) vp[i] = 0u;
}

// ---------------------------------------------------------------------------
// hkdf_sha256 — RFC 5869 HKDF-SHA256, 32-byte output
// ---------------------------------------------------------------------------

void hkdf_sha256(const uint8_t* ikm,  size_t ikm_len,
                 const uint8_t* salt, size_t salt_len,
                 const uint8_t* info, size_t info_len,
                 uint8_t        out[32])
{
    // RFC 5869 §2.2: if salt not provided, set to HashLen zero bytes.
    static const uint8_t kZeroSalt[SHA256_DIGEST_SIZE] = {};
    const uint8_t* act_salt = (salt != nullptr && salt_len > 0u)
                                ? salt : kZeroSalt;
    const size_t   act_slen = (salt != nullptr && salt_len > 0u)
                                ? salt_len : SHA256_DIGEST_SIZE;

    // Step 1 — Extract: PRK = HMAC-SHA256(salt, IKM).
    uint8_t prk[SHA256_DIGEST_SIZE];
    hmac_sha256(act_salt, act_slen, ikm, ikm_len, prk);

    // Step 2 — Expand T(1): HMAC-SHA256(PRK, info || 0x01).
    // Single block suffices for 32 bytes of output.
    // Max info_len in practice: ~24 bytes ("jlpicart.ns.dik.priv").
    // Buffer holds info (up to 219 bytes) + counter byte.
    uint8_t data[220];
    if (info_len > 219u) info_len = 219u;
    if (info != nullptr && info_len > 0u) memcpy(data, info, info_len);
    data[info_len] = 0x01u;

    hmac_sha256(prk, SHA256_DIGEST_SIZE, data, info_len + 1u, out);

    wipe(prk,  sizeof(prk));
    wipe(data, sizeof(data));
}

// ---------------------------------------------------------------------------
// smk_derive — OTP device secret → 32-byte SMK
// ---------------------------------------------------------------------------

void smk_derive(const uint8_t* otp_secret_32_or_null, uint8_t out[32])
{
    static const uint8_t kZeroIkm[32] = {};
    static const char    kInfo[]      = "jlpicart.smk";

    const uint8_t* ikm = otp_secret_32_or_null ? otp_secret_32_or_null : kZeroIkm;

    hkdf_sha256(ikm, 32u,
                nullptr, 0u,
                reinterpret_cast<const uint8_t*>(kInfo), sizeof(kInfo) - 1u,
                out);
}

// ---------------------------------------------------------------------------
// smk_derive_ns_key — SMK → 32-byte per-namespace encryption key
// ---------------------------------------------------------------------------

void smk_derive_ns_key(const uint8_t smk[32], const char* ns_label,
                       uint8_t out[32])
{
    // info = "jlpicart.ns." + ns_label
    static const char kPrefix[]  = "jlpicart.ns.";
    constexpr size_t  kPrefixLen = sizeof(kPrefix) - 1u; // 12 bytes

    const size_t label_len = strlen(ns_label);
    const size_t info_len  = kPrefixLen + label_len;

    // Longest namespace label is "dik.priv" (8 chars) → 20 bytes total.
    // 64-byte buffer is ample; truncate silently on pathological input.
    uint8_t info[64] = {};
    if (info_len <= sizeof(info)) {
        memcpy(info, kPrefix, kPrefixLen);
        memcpy(info + kPrefixLen, ns_label, label_len);
    }

    hkdf_sha256(smk, 32u,
                nullptr, 0u,
                info, info_len <= sizeof(info) ? info_len : 0u,
                out);

    wipe(info, sizeof(info));
}
