#pragma once
// peripheral_manager.h — Orchestrates activation and hardware mapping plan.
//
// Takes a LaunchPlan from the Allocator and:
//   1. Marks each activated capability in the CapabilityRegistry.
//   2. Applies MSX-visible hardware mappings (ports, memory pages, subslots)
//      via apply_mapping() once a MappingPlan has been computed from the
//      active payload's manifest.
//
// Owns all peripheral state (PsgState, Opl4State, SccState) so that main.cc
// stays device-agnostic.  Adding a new device means adding map_X() +
// updating service_all() here, not touching main.cc.
//
// Thread safety: NOT thread-safe. Use only from the boot/preflight path.

#include "spine/allocator.h"
#include "bus/mapping_plan.h"
#include "peripherals/psg.h"
#include "peripherals/scc.h"
#include "peripherals/opl4.h"
#include "peripherals/sunrise_ide.h"
#include "spine/capability_registry.h"
#include <cstddef>
#include <cstdint>

class PeripheralManager {
public:
    // Apply the launch plan: mark activated capabilities in the registry.
    // Returns false if plan.ok is false (a hard requirement was not met).
    // Must not be called more than once on the same manager instance.
    bool apply(const LaunchPlan& plan, CapabilityRegistry& registry);

    // Apply the mapping plan: configure BUS::subslots[] from the mapping.
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

    // Service all active peripherals once.
    // Order: SCC → OPL4 → PSG (PSG mixes SCC and OPL4 samples).
    // Call from the Core 1 / host service loop.
    void service_all();

    // Log a human-readable activation report via log_info/log_warn.
    void log_report(const LaunchPlan& plan) const;

    bool   launch_ok()       const { return launch_ok_; }
    size_t activated_count() const { return activated_count_; }

    // Returns the SCC state wired by apply_mapping() for a KONAMI_SCC slot,
    // or nullptr if no such mapping has been applied.
    SccState* active_scc() { return active_scc_; }

private:
    void map_psg();
    void map_opl4(const char* wave_payload_id);
    void map_sunrise_ide(uint8_t slot, IdeState& state,
                         const uint8_t* nextor_rom, uint32_t nextor_size,
                         const uint8_t* disk_image, uint32_t disk_sectors);

    bool      launch_ok_       = false;
    size_t    activated_count_ = 0;

    // Next available IO-only subslot index (starts at MEMORY_SUBSLOT_COUNT = 4).
    size_t    next_io_subslot_ = 4u;

    // Per-device state and active flags.  map_X() sets the flag; service_all()
    // checks it.  Adding a device means adding state + flag here, not in main.cc.
    PsgState  psg_state_  = {};
    bool      psg_active_ = false;

    Opl4State opl4_state_  = {};
    bool      opl4_active_ = false;

    // SCC states for each memory subslot (wired by apply_mapping KONAMI_SCC case).
    SccState  scc_states_[MAPPING_MAX_ENTRIES] = {};
    SccState* active_scc_ = nullptr;
};
