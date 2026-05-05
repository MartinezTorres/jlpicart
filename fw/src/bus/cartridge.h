#pragma once
// cartridge.h — MSX subslot abstraction for jlpicart_board.
//
// A Subslot represents one entry in BUS::subslots[].  Subslots 0–3 are
// memory-capable (when JLPiCart is in a primary MSX slot); subslots 4–15
// are IO-only.  Each subslot holds 8 independent 8 KB memory segments and
// 256 IO port callbacks.
//
// Usage:
//   1. Call Subslot::clear() to zero all fields.
//   2. Call the appropriate mapper_setup_XXX() or peripheral setup function.
//
// The name "Cartridge" is kept as a type alias for source compatibility.
// Prefer "Subslot" in new code.
//
// This header has no SDK dependencies and is safe to include in host tests.

#include <cstdint>
#include <cstring>
#include <utility>  // std::pair

// RAMFUNC(name): mark a function to run from SRAM (not flash) on hardware.
// On hardware, bus callbacks run on Core 0 in the tight loop and must not
// stall on flash XIP cache misses.  On host, the attribute is a no-op.
//
// Uses raw GCC attributes rather than the pico-sdk __no_inline_not_in_flash_func
// wrapper so this header stays SDK-independent and safe to use in host tests.
#ifdef JLPICART_HOST_TEST
#  define RAMFUNC(name) name
#else
#  define RAMFUNC(name) __attribute__((noinline, section(".time_critical." #name))) name
#endif

struct Subslot {
    // Friendly name for logging (e.g. "konami", "psg").  May be null.
    const char* name = nullptr;

    // Base pointers — set by mapper_setup_XXX() and referenced by callbacks.
    const uint8_t* rom_base = nullptr;
    uint8_t*       ram_base = nullptr;

    // Bus callback type: (this subslot, raw 32-bit GPIO bus word) →
    //   {true, data} if the callback drives the data bus, {false, 0} otherwise.
    using BusCallback = std::pair<bool, uint8_t>(*)(Subslot&, uint32_t);

    // Memory: 8 segments of 8 KB each, covering 0x0000–0xFFFF.
    // Direct pointer path (fast): bus loop reads/writes memory_read/write_addresses.
    // Callback path (flexible):  bus loop calls memory_read/write_callbacks.
    // Both may be set simultaneously; the callback result overrides the pointer read.
    // Only meaningful for subslots 0–3 (memory-capable); ignored for subslots 4–15.
    const uint8_t* memory_read_addresses[8]  = {};
    uint8_t*       memory_write_addresses[8] = {};
    BusCallback    memory_read_callbacks[8]  = {};
    BusCallback    memory_write_callbacks[8] = {};

    // IO: 256 port slots (0x00–0xFF).  All subslots (0–15) may register IO callbacks.
    BusCallback    io_read_callbacks[256]    = {};
    BusCallback    io_write_callbacks[256]   = {};

    // Optional reset callback — called by BUS::reset_callback when the MSX
    // /RESET line is asserted.  Restores peripheral/mapper state to power-on
    // defaults.  Set by mapper_setup_XXX() and peripheral setup functions.
    using ResetFn = void(*)(Subslot&);
    ResetFn reset_fn = nullptr;

    // Reset all fields to their zero/null defaults.
    void clear() { *this = Subslot{}; }
};

// Source-compatibility alias.  Prefer Subslot in new code.
using Cartridge = Subslot;
