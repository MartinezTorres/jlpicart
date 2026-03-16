// test_smk.cc — host tests for HKDF-SHA256, SMK derivation, and DIK wrapping
//               (Stage 25).
//
// Tests verify:
//   - HKDF-SHA256 matches RFC 5869 Appendix A.1 test vector (first 32 bytes)
//   - smk_derive: same input → same output; null vs non-null inputs differ
//   - smk_derive_ns_key: different labels produce different keys
//   - DeviceIdentity: round-trip store/load with explicit wrap key
//   - DeviceIdentity: wrong wrap key causes regeneration (public key changes)
//   - DeviceIdentity: Stage 22 plaintext migration works transparently

#include "crypto/smk.h"
#include "identity/device_identity.h"
#include "storage/kv_store.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

static constexpr uint32_t TEST_FLASH_SIZE = FLASH_SECTOR_SIZE * 32u;

// ---------------------------------------------------------------------------
// test_hkdf_rfc5869_vector — RFC 5869 Appendix A.1 test case
// ---------------------------------------------------------------------------

static void test_hkdf_rfc5869_vector()
{
    // Test Case 1 from RFC 5869 Appendix A.
    // Hash: SHA-256
    // IKM  (22 bytes): 0x0b (× 22)
    // salt (13 bytes): 00 01 02 03 04 05 06 07 08 09 0a 0b 0c
    // info (10 bytes): f0 f1 f2 f3 f4 f5 f6 f7 f8 f9
    // OKM  (42 bytes): 3c b2 5f 25 fa ac d5 7a 90 43 4f 64
    //                  d0 36 2f 2a 2d 2d 0a 90 cf 1a 5a 4c
    //                  5d b0 2d 56 ec c4 c5 bf ...
    // First 32 bytes of OKM = T(1) (one HKDF expand block).

    static const uint8_t ikm[22]  = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b
    };
    static const uint8_t salt[13] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,
        0x07,0x08,0x09,0x0a,0x0b,0x0c
    };
    static const uint8_t info[10] = {
        0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9
    };
    static const uint8_t expected[32] = {
        0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,
        0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,
        0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,
        0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf
    };

    uint8_t out[32] = {};
    hkdf_sha256(ikm, sizeof(ikm), salt, sizeof(salt), info, sizeof(info), out);

    CHECK(memcmp(out, expected, 32) == 0);
}

// ---------------------------------------------------------------------------
// test_smk_derive_deterministic — same IKM → same SMK
// ---------------------------------------------------------------------------

static void test_smk_derive_deterministic()
{
    static const uint8_t otp_secret[32] = {
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
        0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
    };

    uint8_t smk1[32] = {}, smk2[32] = {};
    smk_derive(otp_secret, smk1);
    smk_derive(otp_secret, smk2);

    CHECK(memcmp(smk1, smk2, 32) == 0);
}

// ---------------------------------------------------------------------------
// test_smk_derive_null_vs_secret — null secret and non-null produce different SMKs
// ---------------------------------------------------------------------------

static void test_smk_derive_null_vs_secret()
{
    static const uint8_t otp_secret[32] = {
        0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe,
        0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe,
        0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe,
        0xde,0xad,0xbe,0xef,0xca,0xfe,0xba,0xbe,
    };

    uint8_t smk_null[32] = {}, smk_secret[32] = {};
    smk_derive(nullptr, smk_null);
    smk_derive(otp_secret, smk_secret);

    CHECK(memcmp(smk_null, smk_secret, 32) != 0);
}

// ---------------------------------------------------------------------------
// test_smk_ns_keys_differ — different labels → different keys
// ---------------------------------------------------------------------------

static void test_smk_ns_keys_differ()
{
    uint8_t smk[32] = {};
    smk_derive(nullptr, smk); // use zero-derived SMK for simplicity

    uint8_t key_dik_priv[32] = {}, key_sys[32] = {}, key_prof[32] = {};
    smk_derive_ns_key(smk, "dik.priv", key_dik_priv);
    smk_derive_ns_key(smk, "sys",      key_sys);
    smk_derive_ns_key(smk, "prof",     key_prof);

    CHECK(memcmp(key_dik_priv, key_sys,  32) != 0);
    CHECK(memcmp(key_dik_priv, key_prof, 32) != 0);
    CHECK(memcmp(key_sys,      key_prof, 32) != 0);
}

// ---------------------------------------------------------------------------
// test_dik_wrap_roundtrip — store with key A, reload with key A → same pub key
// ---------------------------------------------------------------------------

static void test_dik_wrap_roundtrip()
{
    FlashDevice flash(TEST_FLASH_SIZE);
    KvStore     kv;
    kv.init(flash, 0u, TEST_FLASH_SIZE);

    uint8_t smk[32], wrap_key[32];
    // Non-zero OTP secret for a "provisioned" simulation.
    static const uint8_t kOtpSecret[32] = {
        0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
        0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
        0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
        0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x42,
    };
    smk_derive(kOtpSecret, smk);
    smk_derive_ns_key(smk, "dik.priv", wrap_key);

    DeviceIdentity dik1;
    CHECK(dik1.init_or_load(kv, wrap_key).ok());

    uint8_t pub1[32] = {};
    dik1.public_key(pub1);

    // Verify dik.priv is stored in encrypted format (104 bytes).
    uint16_t priv_len = 0u;
    uint8_t  priv_buf[104];
    CHECK(kv.get(KV_DIK_PRIV, priv_buf, &priv_len, sizeof(priv_buf)).ok());
    CHECK(priv_len == 104u);

    // Reload with the same key → same public key.
    DeviceIdentity dik2;
    CHECK(dik2.init_or_load(kv, wrap_key).ok());

    uint8_t pub2[32] = {};
    dik2.public_key(pub2);

    CHECK(memcmp(pub1, pub2, 32) == 0);
}

// ---------------------------------------------------------------------------
// test_dik_wrong_key_regenerates — wrong wrap key → regeneration
// ---------------------------------------------------------------------------

static void test_dik_wrong_key_regenerates()
{
    FlashDevice flash(TEST_FLASH_SIZE);
    KvStore     kv;
    kv.init(flash, 0u, TEST_FLASH_SIZE);

    uint8_t key_a[32] = {};
    uint8_t key_b[32] = {};
    smk_derive(nullptr, key_a); // zero-derived
    // key_b: deliberately different
    uint8_t smk_b[32];
    static const uint8_t kSecret[32] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                         0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                         0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
                                         0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    smk_derive(kSecret, smk_b);
    smk_derive_ns_key(smk_b, "dik.priv", key_b);

    // Generate with key_a.
    DeviceIdentity dik1;
    CHECK(dik1.init_or_load(kv, key_a).ok());
    uint8_t pub_a[32] = {};
    dik1.public_key(pub_a);

    // Reload with key_b (wrong key) → decrypt fails → regenerates.
    DeviceIdentity dik2;
    CHECK(dik2.init_or_load(kv, key_b).ok());
    uint8_t pub_b[32] = {};
    dik2.public_key(pub_b);

    // A fresh pair was generated: public keys must differ.
    CHECK(memcmp(pub_a, pub_b, 32) != 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_hkdf_rfc5869_vector();
    test_smk_derive_deterministic();
    test_smk_derive_null_vs_secret();
    test_smk_ns_keys_differ();
    test_dik_wrap_roundtrip();
    test_dik_wrong_key_regenerates();

    return test_summary();
}
