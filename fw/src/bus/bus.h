#pragma once
// bus.h — MSX bus interface for jlpicart_board (Core 0 tight loop).
//
// The bus loop is the innermost real-time loop of the firmware.  It runs on
// Core 0 and never returns.  All other firmware (API window, menu mailbox,
// etc.) runs on Core 1.
//
// Configuration (BUS::cartridges[], BUS::is_expanded, BUS::reset_callback)
// MUST be set before calling BUS::start().  Once start() is called, the bus
// state may only be written from within reset_callback (called while WAIT is
// asserted and the MSX bus is stalled).
//
// See bootstrapping.md Stage 9 and old_src/bus/bus.cc for the original
// implementation notes.

#include "cartridges/cartridge.h"
#include <cstdint>

namespace BUS {

    static constexpr size_t CARTRIDGE_COUNT = 8;

    // Cartridge slots.  Slots 0–3 have memory mapping; slots 4–7 are IO-only.
    // Configured by PeripheralManager::apply_mapping() before BUS::start().
    extern Cartridge cartridges[CARTRIDGE_COUNT];

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
