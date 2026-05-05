#pragma once
#include "spine/policy_types.h"
#include "diag/diag.h"
#include "spine/security_posture.h"

// policy_store.h — load, verify, and expose the signed policy document.
//
// Only this module parses policy blobs from flash.
// Policy document lives at POLICY_FLASH_OFFSET in external flash (fixed XIP offset).

class PolicyStore {
public:
    // Load and verify the policy document from flash (or test buffer).
    // On failure, falls back to safe defaults and records the diag code.
    DiagStatus load(const SecurityPosture& posture);

    // Access the active policy. Always valid after load() — even on failure
    // the safe defaults are applied so callers never see uninitialized state.
    const PolicyInfo& info() const { return info_; }
    bool              loaded_ok() const { return loaded_ok_; }

    // True if load() has been called.
    bool initialized() const { return initialized_; }

#ifndef JLPICART_HOST_TEST
    // Flash byte offset where the policy document is stored.
    // Offset is from the start of external flash (XIP base).
    static constexpr uint32_t POLICY_FLASH_OFFSET = 0x1FF000u;  // last 4KB of 2MB
#endif

private:
    PolicyInfo info_        = { POLICY_SAFE_DEFAULTS, {}, 0 };
    bool       loaded_ok_   = false;
    bool       initialized_ = false;

    void apply_safe_defaults();

#ifdef JLPICART_HOST_TEST
public:
    // Inject a raw policy buffer for host testing (bypasses flash read).
    DiagStatus load_from_buffer(const uint8_t* buf, size_t len,
                                const SecurityPosture& posture);
#endif
};
