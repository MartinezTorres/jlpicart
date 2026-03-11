#pragma once
#include <cstddef>
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"
#include "policy/policy_types.h"

// capability_registry.h — Declared → Allowed → Activated capability pipeline.
//
// The registry owns the three-stage capability lifecycle (spec.md §5.1):
//   Declared:  board declares HW / driver table declares SW
//   Allowed:   declared ∩ (not masked by policy)
//   Activated: allowed ∩ requested ∩ (resources available / probe passed)
//
// Declared and Allowed are populated at init() time (Stage 3).
// Activated is set by PeripheralManager::apply() after Allocator::compute()
// runs during the boot preflight (Stage 8).
//
// This is the single query surface for "what exists and is permitted".
// No other module queries board descriptors or driver tables directly.
// See spec.md §5.1 and bootstrapping.md Appendix.

#include "allocator/resource_model.h"

static constexpr size_t CAPABILITY_REGISTRY_MAX = 32;

struct CapabilityEntry {
    const char*          name;
    bool                 is_hw;        // true = from board descriptor; false = SW
    bool                 safe_verify;
    bool                 allowed;      // computed from policy masking
    bool                 activated;    // set by PeripheralManager after preflight
    ResourceRequirements requirements; // resource budget for this capability
};

class CapabilityRegistry {
public:
    // Initialize from board descriptor + driver table + active policy.
    // Computes the Allowed set; Activated starts false for all entries.
    void init(const BoardDescriptor&  board,
              const DriverDescriptor* drivers,
              size_t                  driver_count,
              const PolicyInfo&       policy);

    // Declared / Allowed queries — O(n); registry is small and called rarely.
    bool is_declared(const char* name) const;
    bool is_allowed (const char* name) const;

    // Descriptor queries used by the Allocator.
    bool                 is_hw           (const char* name) const;
    bool                 is_safe_verify  (const char* name) const;
    ResourceRequirements get_requirements(const char* name) const;

    // Activated queries (populated after PeripheralManager::apply()).
    bool is_activated(const char* name) const;

    // Mark a capability as activated (called by PeripheralManager::apply()).
    void mark_activated(const char* name);

    // List all declared, allowed, or activated names (up to `max` entries).
    // Returns the number of entries written to `out`.
    size_t list_declared (const char** out, size_t max) const;
    size_t list_allowed  (const char** out, size_t max) const;
    size_t list_activated(const char** out, size_t max) const;

    size_t declared_count () const { return count_; }
    size_t allowed_count  () const;
    size_t activated_count() const;

    bool initialized() const { return initialized_; }

private:
    CapabilityEntry entries_[CAPABILITY_REGISTRY_MAX];
    size_t          count_       = 0;
    bool            initialized_ = false;

    // Policy masking: returns true if `name` is blocked by the given policy.
    // Currently no capability names are policy-gated (masking will be
    // populated as features are added in Stage 9+).
    // spec.md §5.1: "Board declaration MUST declare which Capabilities exist".
    static bool is_masked_by_policy(const char* name, const PolicyInfo& policy);
};
