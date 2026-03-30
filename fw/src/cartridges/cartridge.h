#pragma once
// cartridge.h — MSX cartridge abstraction for jlpicart_board.
//
// A Cartridge represents one MSX slot as seen by the bus loop on Core 0.
// It holds 8 independent 8 KB memory segments (covering the full 64 KB MSX
// address space) and 256 IO port slots.  For each segment and IO port, either
// a direct pointer (fast path) or a callback (flexible path) may be installed.
//
// Usage:
//   1. Call Cartridge::clear() to zero all fields.
//   2. Call the appropriate mapper_setup_XXX() function from mappers.h.
//   3. Pass &BUS::cartridges[subslot] to BUS::start() (already wired by default).
//
// This header has no SDK dependencies and is safe to include in host tests.
// See bootstrapping.md Stage 9.

#include <cstdint>
#include <cstring>
#include <utility>  // std::pair

// RAMFUNC: mark a function to run from SRAM (not flash) on hardware.
// On hardware, bus callbacks run on Core 0 in the tight loop and must not
// stall on flash XIP cache misses.  On host, the attribute is a no-op.
#ifdef JLPICART_HOST_TEST
#  define RAMFUNC
#else
#  define RAMFUNC __no_inline_not_in_flash_func
#endif

struct Cartridge {
    // Friendly name for logging (e.g. "konami", "rom").  May be null.
    const char* name = nullptr;

    // Base pointers — set by mapper_setup_XXX() and referenced by callbacks.
    const uint8_t* rom_base = nullptr;
    uint8_t*       ram_base = nullptr;

    // Bus callback type: (this cartridge, raw 32-bit GPIO bus word) →
    //   {true, data} if the callback drives the data bus, {false, 0} otherwise.
    using BusCallback = std::pair<bool, uint8_t>(*)(Cartridge&, uint32_t);

    // Memory: 8 segments of 8 KB each, covering 0x0000–0xFFFF.
    // Direct pointer path (fast): bus loop reads/writes memory_read/write_addresses.
    // Callback path (flexible):  bus loop calls memory_read/write_callbacks.
    // Both may be set simultaneously; the callback result overrides the pointer read.
    const uint8_t* memory_read_addresses[8]  = {};
    uint8_t*       memory_write_addresses[8] = {};
    BusCallback    memory_read_callbacks[8]  = {};
    BusCallback    memory_write_callbacks[8] = {};

    // IO: 256 port slots (0x00–0xFF).
    // Cartridge slots 4–7 are IO-only (no memory mapping).
    BusCallback    io_read_callbacks[256]    = {};
    BusCallback    io_write_callbacks[256]   = {};

    // Optional reset callback — called by BUS::reset_callback when the MSX
    // /RESET line is asserted.  Restores mapper bank registers to their
    // power-on defaults so the Z80 BIOS sees a clean ROM layout after a
    // warm reset.  Set by mapper_setup_XXX(); null for ROM/RAM mappers that
    // carry no per-session state.
    using ResetFn = void(*)(Cartridge&);
    ResetFn reset_fn = nullptr;

    // Reset all fields to their zero/null defaults.
    void clear() { *this = Cartridge{}; }
};
