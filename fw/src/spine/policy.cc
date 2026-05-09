#include "policy.h"
#include "crypto/sha256.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
// XIP_BASE (0x10000000) is defined in hardware/regs/addressmap.h
#endif

// ---------------------------------------------------------------------------
// Development HMAC key — embedded in firmware for unsealed/DEV operation.
// In sealed production, this key is replaced by an OTP-backed key or
// verified against a boot-key-signed document (Stage 10).
// This key MUST NOT be used for any purpose other than policy authentication.
// ---------------------------------------------------------------------------
static const uint8_t kDevPolicyHmacKey[32] = {
    0x4a,0x4c,0x50,0x43,0x61,0x72,0x74,0x2d,  // "JLPCart-"
    0x64,0x65,0x76,0x2d,0x70,0x6f,0x6c,0x69,  // "dev-poli"
    0x63,0x79,0x2d,0x68,0x6d,0x61,0x63,0x2d,  // "cy-hmac-"
    0x6b,0x65,0x79,0x2d,0x76,0x30,0x30,0x31,  // "key-v001"
};

void policy_canonical_bytes(const PolicyDocument& doc, uint8_t out[16]) {
    // version (4 bytes LE) || flags (8 bytes LE) || reserved (4 bytes)
    out[0]  = (uint8_t)(doc.version & 0xFF);
    out[1]  = (uint8_t)((doc.version >> 8) & 0xFF);
    out[2]  = (uint8_t)((doc.version >> 16) & 0xFF);
    out[3]  = (uint8_t)((doc.version >> 24) & 0xFF);
    out[4]  = (uint8_t)(doc.flags & 0xFF);
    out[5]  = (uint8_t)((doc.flags >> 8) & 0xFF);
    out[6]  = (uint8_t)((doc.flags >> 16) & 0xFF);
    out[7]  = (uint8_t)((doc.flags >> 24) & 0xFF);
    out[8]  = (uint8_t)((doc.flags >> 32) & 0xFF);
    out[9]  = (uint8_t)((doc.flags >> 40) & 0xFF);
    out[10] = (uint8_t)((doc.flags >> 48) & 0xFF);
    out[11] = (uint8_t)((doc.flags >> 56) & 0xFF);
    out[12] = doc.reserved[0];
    out[13] = doc.reserved[1];
    out[14] = doc.reserved[2];
    out[15] = doc.reserved[3];
}

DiagStatus policy_verify(const PolicyDocument& doc,
                         const SecurityPosture& posture) {
    // DEV mode: secure boot not enforced, skip verification entirely.
    if (!posture.secure_boot_enabled) {
        return DiagStatus::success();
    }

    if (doc.version != POLICY_VERSION_V1) {
        return DiagStatus::error(DiagCode::POLICY_VERSION_UNSUPPORTED, doc.version);
    }

    // Compute expected HMAC over the canonical 16 bytes.
    uint8_t canonical[16];
    policy_canonical_bytes(doc, canonical);

    uint8_t expected_mac[SHA256_DIGEST_SIZE];
    hmac_sha256(kDevPolicyHmacKey, sizeof(kDevPolicyHmacKey),
                canonical, sizeof(canonical),
                expected_mac);

    // Constant-time comparison to avoid timing side-channels.
    uint8_t diff = 0;
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        diff |= (expected_mac[i] ^ doc.hmac_tag[i]);
    }
    if (diff != 0) {
        return DiagStatus::error(DiagCode::POLICY_BAD_SIGNATURE);
    }

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// PolicyStore
// ---------------------------------------------------------------------------

void PolicyStore::apply_safe_defaults() {
    info_.flags    = POLICY_SAFE_DEFAULTS;
    info_.version  = 0;
    memset(info_.digest16, 0, sizeof(info_.digest16));
    loaded_ok_     = false;
}

static DiagStatus process_document(const PolicyDocument& doc,
                                   const SecurityPosture& posture,
                                   PolicyInfo& out_info) {
    DiagStatus st = policy_verify(doc, posture);
    if (!st.ok()) return st;

    out_info.flags   = doc.flags;
    out_info.version = doc.version;

    // Compute digest16 = first 16 bytes of SHA-256(canonical policy bytes).
    uint8_t canonical[16];
    policy_canonical_bytes(doc, canonical);
    uint8_t full_hash[SHA256_DIGEST_SIZE];
    sha256(canonical, sizeof(canonical), full_hash);
    memcpy(out_info.digest16, full_hash, 16);

    return DiagStatus::success();
}

#ifdef JLPICART_HOST_TEST

DiagStatus PolicyStore::load_from_buffer(const uint8_t* buf, size_t len,
                                         const SecurityPosture& posture) {
    initialized_ = true;
    if (len < sizeof(PolicyDocument)) {
        apply_safe_defaults();
        return DiagStatus::error(DiagCode::POLICY_MISSING);
    }
    PolicyDocument doc;
    memcpy(&doc, buf, sizeof(PolicyDocument));
    DiagStatus st = process_document(doc, posture, info_);
    if (!st.ok()) apply_safe_defaults();
    else          loaded_ok_ = true;
    return st;
}

DiagStatus PolicyStore::load(const SecurityPosture& posture) {
    // Host build: no flash available.  Callers that want to test specific policy
    // flag combinations should use load_from_buffer().
    //
    // Mirrors firmware behaviour: DEV mode (secure_boot not enabled) gets
    // POLICY_DEV_DEFAULTS so that host tests which exercise install / scan /
    // GET_DEVICE_ID paths are not silently blocked by POLICY_SAFE_DEFAULTS.
    initialized_ = true;
    if (!posture.secure_boot_enabled) {
        info_.flags   = POLICY_DEV_DEFAULTS;
        info_.version = POLICY_VERSION_V1;
        memset(info_.digest16, 0xDE, sizeof(info_.digest16));
        loaded_ok_    = false;  // no stored document — still an error
    } else {
        apply_safe_defaults();
    }
    return DiagStatus::error(DiagCode::POLICY_FLASH_READ_ERROR);
}

#else  // firmware

DiagStatus PolicyStore::load(const SecurityPosture& posture) {
    initialized_ = true;

    // Read policy document from flash (XIP window).
    // Temporary layout: fixed offset in last 4KB of 2MB flash.
    const uint8_t* flash_ptr =
        reinterpret_cast<const uint8_t*>(XIP_BASE + POLICY_FLASH_OFFSET);

    // Validate that flash looks like a policy document (not erased 0xFF).
    // An erased page has all bytes = 0xFF; version 0xFFFFFFFF is invalid.
    PolicyDocument doc;
    memcpy(&doc, flash_ptr, sizeof(PolicyDocument));

    if (doc.version == 0xFFFFFFFFu || doc.version == 0u) {
        // Flash is erased or uninitialized — policy is missing.
        // In DEV mode this is fine; in sealed mode it's an error.
        if (!posture.secure_boot_enabled) {
            // DEV mode: use dev defaults without a stored document.
            info_.flags   = POLICY_DEV_DEFAULTS;
            info_.version = POLICY_VERSION_V1;
            memset(info_.digest16, 0xDE, sizeof(info_.digest16));
            loaded_ok_    = true;
            return DiagStatus::success();
        }
        apply_safe_defaults();
        return DiagStatus::error(DiagCode::POLICY_MISSING);
    }

    DiagStatus st = process_document(doc, posture, info_);
    if (!st.ok()) apply_safe_defaults();
    else          loaded_ok_ = true;
    return st;
}

#endif  // JLPICART_HOST_TEST
