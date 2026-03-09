#pragma once
#include <cstdint>

// diag.h — stable diagnostic codes for JLPiCart firmware.
//
// Rules:
//   - Never renumber an existing code (breaks persisted receipts/logs).
//   - Add new codes at the end of each range.
//   - Keep diag_code_to_string() in sync with this enum.
//
// See fw/spec.md §13 (Diagnostics and conformance).

enum class DiagCode : uint16_t {
    // --- General ---
    OK                          = 0,
    INTERNAL_ASSERT             = 1,

    // --- OTP / Security posture ---
    OTP_UNREADABLE              = 0x0100,

    // --- Policy ---
    POLICY_MISSING              = 0x0200,
    POLICY_BAD_SIGNATURE        = 0x0201,
    POLICY_BAD_CANONICALIZATION = 0x0202,
    POLICY_VERSION_UNSUPPORTED  = 0x0203,
    POLICY_FLASH_READ_ERROR     = 0x0204,

    // --- Storage ---
    STORAGE_CORRUPT             = 0x0300,
    STORAGE_FULL                = 0x0301,

    // --- Content / Collections ---
    COLLECTION_HASH_MISMATCH    = 0x0400,
    COLLECTION_BAD_MANIFEST     = 0x0401,
    COLLECTION_UNSATISFIED_REQ  = 0x0402,
};

struct DiagStatus {
    DiagCode code;
    uint32_t detail;  // code-specific auxiliary information

    bool ok() const { return code == DiagCode::OK; }
    static DiagStatus success() { return {DiagCode::OK, 0}; }
    static DiagStatus error(DiagCode c, uint32_t d = 0) { return {c, d}; }
};

// Returns a stable, non-null human-readable string for any DiagCode.
const char* diag_code_to_string(DiagCode code);
