#pragma once
#include "spine/policy_store.h"
#include "diag/diag.h"
#include "spine/security_posture.h"

// policy_verify.h — authenticate a PolicyDocument before use.
//
// Verification uses HMAC-SHA256 with a development key embedded in firmware.
// In DEV mode (secure_boot_enabled = false), signature checking is skipped.
// Only PolicyStore calls policy_verify().

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
