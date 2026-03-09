// test_policy.cc — unit tests for SHA-256, HMAC, policy verification, and PolicyStore.
#include "test_helpers.h"
#include "crypto/sha256.h"
#include "policy/policy_types.h"
#include "policy/policy_verify.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "security/otp_reader.h"
#include <cstring>

// ---------------------------------------------------------------------------
// SHA-256 known test vectors (NIST FIPS 180-4)
// ---------------------------------------------------------------------------

static void test_sha256_empty() {
    // SHA256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    static const uint8_t expected[32] = {
        0xe3,0xb0,0xc4,0x42,0x98,0xfc,0x1c,0x14,
        0x9a,0xfb,0xf4,0xc8,0x99,0x6f,0xb9,0x24,
        0x27,0xae,0x41,0xe4,0x64,0x9b,0x93,0x4c,
        0xa4,0x95,0x99,0x1b,0x78,0x52,0xb8,0x55,
    };
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256(nullptr, 0, digest);
    CHECK(memcmp(digest, expected, SHA256_DIGEST_SIZE) == 0);
}

static void test_sha256_abc() {
    // SHA256("abc") = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    static const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad,
    };
    const uint8_t msg[] = {'a', 'b', 'c'};
    uint8_t digest[SHA256_DIGEST_SIZE];
    sha256(msg, sizeof(msg), digest);
    CHECK(memcmp(digest, expected, SHA256_DIGEST_SIZE) == 0);
}

static void test_sha256_incremental_equals_oneshot() {
    const uint8_t data[] = "The quick brown fox jumps over the lazy dog";
    uint8_t oneshot[SHA256_DIGEST_SIZE];
    sha256(data, sizeof(data) - 1, oneshot);

    // Feed in two parts.
    Sha256Ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, 10);
    sha256_update(&ctx, data + 10, sizeof(data) - 1 - 10);
    uint8_t incremental[SHA256_DIGEST_SIZE];
    sha256_final(&ctx, incremental);

    CHECK(memcmp(oneshot, incremental, SHA256_DIGEST_SIZE) == 0);
}

static void test_hmac_sha256_deterministic() {
    const uint8_t key[]  = "test-key";
    const uint8_t data[] = "test-data";
    uint8_t mac1[SHA256_DIGEST_SIZE];
    uint8_t mac2[SHA256_DIGEST_SIZE];
    hmac_sha256(key, sizeof(key) - 1, data, sizeof(data) - 1, mac1);
    hmac_sha256(key, sizeof(key) - 1, data, sizeof(data) - 1, mac2);
    CHECK(memcmp(mac1, mac2, SHA256_DIGEST_SIZE) == 0);
}

static void test_hmac_sha256_different_keys_differ() {
    const uint8_t key1[] = "key-a";
    const uint8_t key2[] = "key-b";
    const uint8_t data[] = "same-data";
    uint8_t mac1[SHA256_DIGEST_SIZE];
    uint8_t mac2[SHA256_DIGEST_SIZE];
    hmac_sha256(key1, sizeof(key1) - 1, data, sizeof(data) - 1, mac1);
    hmac_sha256(key2, sizeof(key2) - 1, data, sizeof(data) - 1, mac2);
    CHECK(memcmp(mac1, mac2, SHA256_DIGEST_SIZE) != 0);
}

// ---------------------------------------------------------------------------
// policy_canonical_bytes
// ---------------------------------------------------------------------------

static void test_policy_canonical_bytes_layout() {
    PolicyDocument doc = {};
    doc.version = 0x01020304u;
    doc.flags   = 0x0102030405060708ULL;
    memset(doc.reserved, 0xAB, sizeof(doc.reserved));

    uint8_t canonical[16];
    policy_canonical_bytes(doc, canonical);

    // version LE
    CHECK(canonical[0] == 0x04);
    CHECK(canonical[1] == 0x03);
    CHECK(canonical[2] == 0x02);
    CHECK(canonical[3] == 0x01);
    // flags LE
    CHECK(canonical[4] == 0x08);
    CHECK(canonical[5] == 0x07);
    CHECK(canonical[6] == 0x06);
    CHECK(canonical[7] == 0x05);
    CHECK(canonical[8]  == 0x04);
    CHECK(canonical[9]  == 0x03);
    CHECK(canonical[10] == 0x02);
    CHECK(canonical[11] == 0x01);
    // reserved
    CHECK(canonical[12] == 0xAB);
    CHECK(canonical[13] == 0xAB);
    CHECK(canonical[14] == 0xAB);
    CHECK(canonical[15] == 0xAB);
}

// ---------------------------------------------------------------------------
// policy_verify — DEV mode (secure_boot_enabled = false)
// ---------------------------------------------------------------------------

static SecurityPosture make_dev_posture() {
    uint8_t buf[256] = {};
    FakeOtpReader otp(buf, sizeof(buf));
    return SecurityPosture::read(otp);
}

static SecurityPosture make_sealed_posture() {
    uint8_t buf[256] = {};
    buf[otp_offsets::CRIT1] = otp_offsets::CRIT1_SECURE_BOOT_ENABLE_BIT;
    FakeOtpReader otp(buf, sizeof(buf));
    return SecurityPosture::read(otp);
}

static void test_policy_verify_dev_mode_always_passes() {
    SecurityPosture posture = make_dev_posture();
    CHECK(!posture.secure_boot_enabled);

    // Any document, even with a zeroed HMAC, should pass in DEV mode.
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_DEV_DEFAULTS;
    // hmac_tag is all zeros — wrong but DEV mode skips check.
    DiagStatus st = policy_verify(doc, posture);
    CHECK(st.ok());
}

static void test_policy_verify_sealed_bad_hmac_fails() {
    SecurityPosture posture = make_sealed_posture();
    CHECK(posture.secure_boot_enabled);

    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_DEV_DEFAULTS;
    // hmac_tag all zeros — invalid.
    DiagStatus st = policy_verify(doc, posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_BAD_SIGNATURE);
}

static void test_policy_verify_sealed_wrong_version_fails() {
    SecurityPosture posture = make_sealed_posture();

    PolicyDocument doc = {};
    doc.version = 0;  // not POLICY_VERSION_V1
    DiagStatus st = policy_verify(doc, posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_VERSION_UNSUPPORTED);
}

// Build a PolicyDocument with a valid HMAC using the dev key.
// Requires knowing the key bytes used in policy_verify.cc.
// We verify the roundtrip: compute HMAC, put in doc, verify → OK.
static PolicyDocument make_valid_sealed_doc(PolicyFlags flags) {
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = flags;
    memset(doc.reserved, 0, sizeof(doc.reserved));

    // Compute canonical bytes and HMAC (mirrors policy_verify.cc logic).
    uint8_t canonical[16];
    policy_canonical_bytes(doc, canonical);

    // The dev key from policy_verify.cc: "JLPCart-dev-policy-hmac-key-v001"
    static const uint8_t kDevKey[32] = {
        0x4a,0x4c,0x50,0x43,0x61,0x72,0x74,0x2d,
        0x64,0x65,0x76,0x2d,0x70,0x6f,0x6c,0x69,
        0x63,0x79,0x2d,0x68,0x6d,0x61,0x63,0x2d,
        0x6b,0x65,0x79,0x2d,0x76,0x30,0x30,0x31,
    };
    hmac_sha256(kDevKey, sizeof(kDevKey), canonical, sizeof(canonical), doc.hmac_tag);
    return doc;
}

static void test_policy_verify_sealed_valid_hmac_passes() {
    SecurityPosture posture = make_sealed_posture();

    PolicyDocument doc = make_valid_sealed_doc(POLICY_DEV_DEFAULTS);
    DiagStatus st = policy_verify(doc, posture);
    CHECK(st.ok());
}

static void test_policy_verify_sealed_one_bit_mutation_fails() {
    SecurityPosture posture = make_sealed_posture();

    PolicyDocument doc = make_valid_sealed_doc(POLICY_DEV_DEFAULTS);
    CHECK(policy_verify(doc, posture).ok());  // baseline: valid

    // Flip one bit in the flags field. HMAC must now fail.
    doc.flags ^= 1ULL;
    DiagStatus st = policy_verify(doc, posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_BAD_SIGNATURE);
}

static void test_policy_verify_sealed_wrong_key_fails() {
    SecurityPosture posture = make_sealed_posture();

    // Build a doc whose HMAC was computed with a different (wrong) key.
    static const uint8_t kWrongKey[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,
    };
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_DEV_DEFAULTS;
    uint8_t canonical[16];
    policy_canonical_bytes(doc, canonical);
    hmac_sha256(kWrongKey, sizeof(kWrongKey), canonical, sizeof(canonical), doc.hmac_tag);

    DiagStatus st = policy_verify(doc, posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_BAD_SIGNATURE);
}

// ---------------------------------------------------------------------------
// PolicyStore host tests (via load_from_buffer)
// ---------------------------------------------------------------------------

static void test_policy_store_dev_missing_policy_uses_dev_defaults() {
    SecurityPosture posture = make_dev_posture();
    PolicyStore store;

    // Empty buffer (too small for a PolicyDocument).
    DiagStatus st = store.load_from_buffer(nullptr, 0, posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_MISSING);
    // Even on missing, safe defaults are applied; loaded_ok is false.
    CHECK(!store.loaded_ok());
    CHECK(store.initialized());
}

static void test_policy_store_dev_valid_doc_loads() {
    SecurityPosture posture = make_dev_posture();
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_ALLOW_USB_COLLECTION_INSTALL;

    PolicyStore store;
    DiagStatus st = store.load_from_buffer(
        reinterpret_cast<const uint8_t*>(&doc), sizeof(doc), posture);
    CHECK(st.ok());
    CHECK(store.loaded_ok());
    CHECK(store.info().flags == POLICY_ALLOW_USB_COLLECTION_INSTALL);
    CHECK(store.info().version == POLICY_VERSION_V1);
}

static void test_policy_store_sealed_valid_hmac_loads() {
    SecurityPosture posture = make_sealed_posture();
    PolicyDocument doc = make_valid_sealed_doc(POLICY_ALLOW_USB_COLLECTION_INSTALL);

    PolicyStore store;
    DiagStatus st = store.load_from_buffer(
        reinterpret_cast<const uint8_t*>(&doc), sizeof(doc), posture);
    CHECK(st.ok());
    CHECK(store.loaded_ok());
    CHECK(store.info().flags == POLICY_ALLOW_USB_COLLECTION_INSTALL);
}

static void test_policy_store_sealed_bad_hmac_safe_defaults() {
    SecurityPosture posture = make_sealed_posture();
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_DEV_DEFAULTS;
    // hmac_tag zeroed — invalid in sealed mode.

    PolicyStore store;
    DiagStatus st = store.load_from_buffer(
        reinterpret_cast<const uint8_t*>(&doc), sizeof(doc), posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_BAD_SIGNATURE);
    CHECK(!store.loaded_ok());
    // Safe defaults = zero flags.
    CHECK(store.info().flags == POLICY_SAFE_DEFAULTS);
}

static void test_policy_store_load_on_host_returns_error() {
    SecurityPosture posture = make_dev_posture();
    PolicyStore store;
    DiagStatus st = store.load(posture);
    CHECK(!st.ok());
    CHECK(st.code == DiagCode::POLICY_FLASH_READ_ERROR);
}

static void test_policy_store_digest16_populated() {
    SecurityPosture posture = make_dev_posture();
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = POLICY_ALLOW_USB_COLLECTION_INSTALL;

    PolicyStore store;
    store.load_from_buffer(
        reinterpret_cast<const uint8_t*>(&doc), sizeof(doc), posture);

    // digest16 must be non-zero (it's SHA-256 of canonical bytes, not all zeros).
    bool any_nonzero = false;
    for (size_t i = 0; i < 16; i++) {
        if (store.info().digest16[i] != 0) { any_nonzero = true; break; }
    }
    CHECK(any_nonzero);
}

int main() {
    test_sha256_empty();
    test_sha256_abc();
    test_sha256_incremental_equals_oneshot();
    test_hmac_sha256_deterministic();
    test_hmac_sha256_different_keys_differ();
    test_policy_canonical_bytes_layout();
    test_policy_verify_dev_mode_always_passes();
    test_policy_verify_sealed_bad_hmac_fails();
    test_policy_verify_sealed_wrong_version_fails();
    test_policy_verify_sealed_valid_hmac_passes();
    test_policy_verify_sealed_one_bit_mutation_fails();
    test_policy_verify_sealed_wrong_key_fails();
    test_policy_store_dev_missing_policy_uses_dev_defaults();
    test_policy_store_dev_valid_doc_loads();
    test_policy_store_sealed_valid_hmac_loads();
    test_policy_store_sealed_bad_hmac_safe_defaults();
    test_policy_store_load_on_host_returns_error();
    test_policy_store_digest16_populated();
    return test_summary();
}
