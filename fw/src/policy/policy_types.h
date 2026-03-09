#pragma once
#include <cstdint>
#include <cstring>

// policy_types.h — policy flags and metadata for JLPiCart.
//
// Policy flags are mutable operational rules stored in flash, authenticated
// by HMAC-SHA256 (Stage 3: dev key in firmware; Stage 10: OTP-backed key).
// Bit assignments are normative per spec.md §10.

using PolicyFlags = uint64_t;

// Normative bit assignments (spec.md §10 — Policy flags bit assignments v1)
static constexpr PolicyFlags POLICY_ALLOW_USB_COLLECTION_INSTALL     = (1ULL << 0);
static constexpr PolicyFlags POLICY_ALLOW_NETWORK_COLLECTION_INSTALL = (1ULL << 1);
static constexpr PolicyFlags POLICY_ALLOW_UNSIGNED_COLLECTIONS       = (1ULL << 2);
static constexpr PolicyFlags POLICY_ALLOW_USER_REPLACE_COLLECTIONS   = (1ULL << 3);
static constexpr PolicyFlags POLICY_REQUIRE_PUBLISHER_SIGNATURE      = (1ULL << 4);
static constexpr PolicyFlags POLICY_ALLOW_BOOT_KEY_ENROLLMENT        = (1ULL << 5);
static constexpr PolicyFlags POLICY_ALLOW_BOOT_KEY_REVOCATION        = (1ULL << 6);
static constexpr PolicyFlags POLICY_EXPOSE_STABLE_DEVICE_ID          = (1ULL << 7);
// bits 8..31: reserved, MUST be zero in v1.

// Safe defaults applied when policy is missing or invalid.
// All permissive actions disabled; device is maximally restrictive.
// spec.md §10: "If the policy document is missing or invalid, the platform
// MUST fall back to a safe default (no unsigned installs; no key enrollment)."
static constexpr PolicyFlags POLICY_SAFE_DEFAULTS = 0ULL;

// Permissive defaults for DEV mode (secure_boot not enforced).
// Allows USB install and unsigned collections so development is frictionless.
static constexpr PolicyFlags POLICY_DEV_DEFAULTS =
    POLICY_ALLOW_USB_COLLECTION_INSTALL     |
    POLICY_ALLOW_UNSIGNED_COLLECTIONS       |
    POLICY_ALLOW_USER_REPLACE_COLLECTIONS   |
    POLICY_ALLOW_BOOT_KEY_ENROLLMENT;

static constexpr uint32_t POLICY_VERSION_V1 = 1u;

// On-flash/wire layout for a policy document.
// HMAC-SHA256 covers the canonical bytes: version || flags || reserved
// (little-endian, exactly 16 bytes), then the HMAC tag follows.
#pragma pack(push, 1)
struct PolicyDocument {
    uint32_t version;        // must be POLICY_VERSION_V1
    PolicyFlags flags;       // 8 bytes
    uint8_t  reserved[4];   // must be zero in v1
    uint8_t  hmac_tag[32];  // HMAC-SHA256 over the preceding 16 bytes
};
#pragma pack(pop)

static_assert(sizeof(PolicyDocument) == 4 + 8 + 4 + 32, "PolicyDocument layout");

// Runtime policy info exposed to the rest of the system.
struct PolicyInfo {
    PolicyFlags flags;
    uint8_t     digest16[16];  // first 16 bytes of SHA-256(canonical policy bytes)
    uint32_t    version;
};
