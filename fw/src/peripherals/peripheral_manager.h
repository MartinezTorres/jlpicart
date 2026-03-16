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
#include "peripherals/psg.h"
#include "spine/capability_registry.h"
#include <cstddef>
#include <cstdint>

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

    // Map the 16 KB menu page buffer into the bus at MSX page 1 (0x4000–0x7FFF,
    // subslot 1) as a read-write region.  The Z80 stub and the RP2350 both
    // read and write this buffer (mailbox registers, data buffer).
    // Sets BUS::is_expanded = true.
    void map_menu_page(uint8_t* page);

    // Map the 16 KB API window buffer into the bus at MSX page 2 (0x8000–0xBFFF,
    // subslot 2) as a read-only region.  The Z80 reads the API window; writes
    // from the Z80 are silently discarded by the bus loop.
    void map_api_window(const uint8_t* buf);

    // Wire PSG (AY-3-8910) IO callbacks into the bus at slot 4 (IO-only).
    // Calls psg_setup(); the caller must have already called psg_reset().
    // On hardware: also calls psg_audio_init() to start PWM output.
    void map_psg(PsgState& state);

    // Log a human-readable activation report via log_info/log_warn.
    void log_report(const LaunchPlan& plan) const;

    bool   launch_ok()       const { return launch_ok_; }
    size_t activated_count() const { return activated_count_; }

private:
    bool   launch_ok_       = false;
    size_t activated_count_ = 0;
};
