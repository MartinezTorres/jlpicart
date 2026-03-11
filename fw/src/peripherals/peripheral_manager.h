#pragma once
// peripheral_manager.h — Orchestrates activation and hardware mapping plan.
//
// Takes a LaunchPlan from the Allocator and:
//   1. Marks each activated capability in the CapabilityRegistry.
//   2. Applies MSX-visible hardware mappings (ports, memory pages, subslots)
//      via apply_mapping() once a MappingPlan has been computed from the
//      active payload's manifest.
//
// See spec.md §5.2 "Launch workflow contract (v1)" and bootstrapping.md Stage 9.
// Thread safety: NOT thread-safe. Use only from the boot/preflight path.

#include "allocator/allocator.h"
#include "bus/mapping_plan.h"
#include "spine/capability_registry.h"
#include <cstddef>

class PeripheralManager {
public:
    // Apply the launch plan: mark activated capabilities in the registry.
    // Returns false if plan.ok is false (a hard requirement was not met).
    // Must not be called more than once on the same manager instance.
    bool apply(const LaunchPlan& plan, CapabilityRegistry& registry);

    // Apply the mapping plan: configure BUS::cartridges[] from the mapping.
    // On hardware: calls mapper_setup_XXX() for entries with rom_data set.
    // If rom_data is nullptr, logs that ROM loading is deferred.
    // Returns true on success (including the deferred case).
    bool apply_mapping(const MappingPlan& plan);

    // Log a human-readable activation report via log_info/log_warn.
    void log_report(const LaunchPlan& plan) const;

    bool   launch_ok()       const { return launch_ok_; }
    size_t activated_count() const { return activated_count_; }

private:
    bool   launch_ok_       = false;
    size_t activated_count_ = 0;
};
