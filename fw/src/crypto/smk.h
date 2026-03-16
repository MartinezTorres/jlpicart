#pragma once
// smk.h — HKDF-SHA256 and Storage Master Key (SMK) derivation (Stage 25).
//
// Key hierarchy (see doc/security/key-reference.md):
//   OTP device secret (32 bytes)
//     → SMK = HKDF-SHA256(IKM=otp_secret, info="jlpicart.smk")
//       → namespace key = HKDF-SHA256(IKM=SMK, info="jlpicart.ns." + label)
//
// When otp_secret is absent (unprovisioned unit), all-zeros IKM is used.
// The resulting key is well-known (not secret), giving effectively no hardware-
// rooted encryption. This is intentional: see doc/security/key-reference.md §DIK.
//
// HKDF per RFC 5869.  This header only supports 32-byte output (L=32, one T(1)
// block), which covers all current use cases.

#include <cstdint>
#include <cstddef>

static constexpr size_t SMK_LEN    = 32u;
static constexpr size_t SMK_NS_KEY_LEN = 32u;

// HKDF-SHA256 (RFC 5869), 32-byte output.
// salt: if null or salt_len == 0, treated as 32 zero bytes (RFC 5869 §2.2).
// info: the context/application label.
void hkdf_sha256(const uint8_t* ikm,  size_t ikm_len,
                 const uint8_t* salt, size_t salt_len,
                 const uint8_t* info, size_t info_len,
                 uint8_t        out[32]);

// Derive the 32-byte SMK from the OTP device secret.
// otp_secret_32: 32-byte secret from OTP; null = treat as 32 zero bytes.
void smk_derive(const uint8_t* otp_secret_32_or_null, uint8_t out[32]);

// Derive a 32-byte per-namespace encryption key from the SMK.
// ns_label: e.g. "dik.priv", "sys", "prof", "saves", "guest"
// info = "jlpicart.ns." + ns_label
void smk_derive_ns_key(const uint8_t smk[32], const char* ns_label,
                       uint8_t out[32]);
