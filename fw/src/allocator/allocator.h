#pragma once
// allocator.h — Deterministic Launch Plan computer (Stage 8).
//
// Given a CapabilityRegistry (declared/allowed) and a RequestedCapabilities
// set (derived from a Payload Manifest), produces a LaunchPlan that specifies
// which capabilities are Activated and which failed (with reasons).
//
// Algorithm (spec.md §5.1 "Deterministic algorithm"):
//   1. Collect all declared/allowed capabilities.
//   2. Sort candidates by capability_id (stable alphabetical order).
//   3. For each requested capability, in sorted order:
//      - HW: activate directly, or run safe probe if descriptor permits it.
//      - SW: activate only if resource budget allows.
//   4. Record any hard-required capability that could not be activated.
//
// The plan's `ok` field is false only if a HARD requirement was not satisfied.
// Optional capability failures are recorded in `failures` but do not set ok=false.
//
// Thread safety: NOT thread-safe. Use only from the boot/preflight path.

#include "allocator/resource_model.h"
#include "spine/capability_registry.h"
#include <cstdint>
#include <cstddef>

// Maximum capability id length.  Matches PAYLOAD_CAP_ID_MAX in manifest.h
// and the descriptor name string lengths throughout.
static constexpr size_t CAP_ID_MAX = 64;

// ---------------------------------------------------------------------------
// RequestedCapabilities — what a Payload wants to activate.
// ---------------------------------------------------------------------------

static constexpr size_t REQUESTED_CAPS_MAX = 16;

struct RequestedCapabilities {
    char   required[REQUESTED_CAPS_MAX][CAP_ID_MAX];  // hard requirements
    size_t required_count;
    char   optional_caps[REQUESTED_CAPS_MAX][CAP_ID_MAX];  // nice-to-have
    size_t optional_count;

    // If true, all allowed capabilities are treated as requested.
    // Used when no specific Payload is loaded (menu / standby mode).
    bool all_allowed;

    bool is_required (const char* name) const;
    bool is_requested(const char* name) const;  // required || optional || all_allowed
};

// ---------------------------------------------------------------------------
// LaunchPlan — output of Allocator::compute().
// ---------------------------------------------------------------------------

enum class LaunchFailureKind : uint8_t {
    MISSING_CAPABILITY,  // not declared by board or driver table
    POLICY_DISABLED,     // declared but masked by policy
    VERIFY_FAILED,       // HW probe ran and returned failure
    ALLOC_FAILED,        // SW resource budget exhausted
    MAPPING_CONFLICT,    // (future) exclusive MSX port/page conflict
};

struct LaunchFailureReason {
    char              capability_id[CAP_ID_MAX];
    LaunchFailureKind kind;
    bool              was_hard_requirement;
};

static constexpr size_t LAUNCH_MAX_ACTIVATED = CAPABILITY_REGISTRY_MAX;
static constexpr size_t LAUNCH_MAX_FAILURES  = 8;

struct LaunchPlan {
    char   activated_ids[LAUNCH_MAX_ACTIVATED][CAP_ID_MAX];
    size_t activated_count;

    LaunchFailureReason failures[LAUNCH_MAX_FAILURES];
    size_t              failure_count;

    // false only if a hard requirement was not activated.
    bool ok;

    bool is_activated(const char* name) const;

    // Add a failure reason; no-op if the failure table is full.
    void add_failure(const char* cap_id, LaunchFailureKind kind, bool was_hard);
};

// ---------------------------------------------------------------------------
// Allocator
// ---------------------------------------------------------------------------

class Allocator {
public:
    // Compute the Launch Plan deterministically.
    //
    // registry  — declared/allowed capabilities from capability_registry.
    // requested — capabilities the current Payload (or system) requests.
    // resources — mutable budget; deducted for each activated SW capability.
    LaunchPlan compute(const CapabilityRegistry& registry,
                       const RequestedCapabilities& requested,
                       ResourceModel& resources);

private:
    // Probe a HW capability for presence/health.
    // Delegates to hw_probe() in allocator/hw_probe.cc, which performs
    // real hardware checks (AT ping, I2C scan) on firmware and returns
    // true unconditionally in host tests.
    static bool probe_hw_capability(const char* name);
};
