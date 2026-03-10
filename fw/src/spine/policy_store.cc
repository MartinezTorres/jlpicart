#include "policy_store.h"
#include "policy/policy_verify.h"
#include "crypto/sha256.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#include "hardware/flash.h"
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
// XIP_BASE (0x10000000) is defined in hardware/regs/addressmap.h
#endif

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

DiagStatus PolicyStore::load(const SecurityPosture& /*posture*/) {
    // Host build: no flash available.  Callers that want to test policy
    // verification should use load_from_buffer().  Callers that deliberately
    // call load() (e.g. test_collections) get safe defaults, which is correct.
    initialized_ = true;
    apply_safe_defaults();
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
