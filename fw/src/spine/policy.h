#pragma once
#include "diag/diag.h"
#include "spine/security_posture.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Policy types
// ---------------------------------------------------------------------------

using PolicyFlags = uint64_t;

static constexpr PolicyFlags POLICY_ALLOW_USB_COLLECTION_INSTALL     = (1ULL << 0);
static constexpr PolicyFlags POLICY_ALLOW_NETWORK_COLLECTION_INSTALL = (1ULL << 1);
static constexpr PolicyFlags POLICY_ALLOW_UNSIGNED_COLLECTIONS       = (1ULL << 2);
static constexpr PolicyFlags POLICY_ALLOW_USER_REPLACE_COLLECTIONS   = (1ULL << 3);
static constexpr PolicyFlags POLICY_REQUIRE_PUBLISHER_SIGNATURE      = (1ULL << 4);
static constexpr PolicyFlags POLICY_ALLOW_BOOT_KEY_ENROLLMENT        = (1ULL << 5);
static constexpr PolicyFlags POLICY_ALLOW_BOOT_KEY_REVOCATION        = (1ULL << 6);
static constexpr PolicyFlags POLICY_EXPOSE_STABLE_DEVICE_ID          = (1ULL << 7);

static constexpr PolicyFlags POLICY_SAFE_DEFAULTS = 0ULL;
static constexpr PolicyFlags POLICY_DEV_DEFAULTS =
    POLICY_ALLOW_USB_COLLECTION_INSTALL     |
    POLICY_ALLOW_UNSIGNED_COLLECTIONS       |
    POLICY_ALLOW_USER_REPLACE_COLLECTIONS   |
    POLICY_ALLOW_BOOT_KEY_ENROLLMENT        |
    POLICY_EXPOSE_STABLE_DEVICE_ID;

static constexpr uint32_t POLICY_VERSION_V1 = 1u;

#pragma pack(push, 1)
struct PolicyDocument {
    uint32_t    version;
    PolicyFlags flags;
    uint8_t     reserved[4];
    uint8_t     hmac_tag[32];
};
#pragma pack(pop)
static_assert(sizeof(PolicyDocument) == 4 + 8 + 4 + 32, "PolicyDocument layout");

struct PolicyInfo {
    PolicyFlags flags;
    uint8_t     digest16[16];
    uint32_t    version;
};

// ---------------------------------------------------------------------------
// PolicyStore — load, verify, and expose the signed policy document.
//
// Only this module parses policy blobs from flash.
// Policy document lives at POLICY_FLASH_OFFSET in external flash (fixed XIP offset).
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// policy_verify / policy_canonical_bytes — authenticate a PolicyDocument.
//
// Verification uses HMAC-SHA256 with a development key embedded in firmware.
// In DEV mode (secure_boot_enabled = false), signature checking is skipped.
// Only PolicyStore calls policy_verify().
// ---------------------------------------------------------------------------

// Returns DiagStatus::success() if:
//   - posture.secure_boot_enabled == false  (DEV mode, skip verification), OR
//   - HMAC-SHA256 over doc's canonical bytes matches doc.hmac_tag.
// Returns an error DiagCode otherwise.
DiagStatus policy_verify(const PolicyDocument& doc,
                         const SecurityPosture& posture);

// The canonical bytes covered by the HMAC are exactly:
//   version (4 bytes LE) || flags (8 bytes LE) || reserved (4 bytes)
// = 16 bytes total. The hmac_tag field is NOT included.
void policy_canonical_bytes(const PolicyDocument& doc, uint8_t out[16]);
