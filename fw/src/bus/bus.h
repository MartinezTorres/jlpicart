#pragma once
// bus.h — MSX bus interface for jlpicart_board (Core 0 tight loop).
//
// The bus loop is the innermost real-time loop of the firmware.  It runs on
// Core 0 and never returns.  All other firmware (API window, menu mailbox,
// etc.) runs on Core 1.
//
// Configuration (BUS::subslots[], BUS::is_expanded, BUS::reset_callback)
// MUST be set before calling BUS::start().  Once start() is called, the bus
// state may only be written from within reset_callback (called while WAIT is
// asserted and the MSX bus is stalled).
//

#include "bus/cartridge.h"
#include <cstdint>

namespace BUS {

    // Total number of subslots.
    static constexpr size_t SUBSLOT_COUNT = 16;

    // Subslots 0–3 are memory-capable (when JLPiCart is in a primary MSX slot).
    // Subslots 4–15 are IO-only in all configurations.
    static constexpr size_t MEMORY_SUBSLOT_COUNT = 4;

    // Subslot array.  Configured by PeripheralManager before BUS::start().
    extern Subslot subslots[SUBSLOT_COUNT];

    // Subslot routing for each of the four 16 KB MSX pages (0–3).
    // subslot_indexes[page] = which of slots 0–3 handles that page.
    // Only meaningful when is_expanded = true.
    extern uint8_t subslot_indexes[4];

    // When true, the subslot expansion register at MSX address 0xFFFF is
    // handled by the bus loop.  Set by PeripheralManager::apply_mapping()
    // when more than one cartridge slot is in use.
    extern bool is_expanded;

    // Called by the bus loop while WAIT is asserted on MSX RESET.
    // Use this to re-initialise cartridge state on a hot reset.
    // Set to nullptr if no reset handling is needed.
    using ResetCallback = void(*)();
    extern ResetCallback reset_callback;

    // Enter the Core 0 MSX bus loop.  Never returns.
    // Must only be called after all cartridges and flags are configured.
    [[noreturn]] void start();

}  // namespace BUS
