#pragma once
// mappers.h — MSX mapper setup functions for jlpicart_board.
//
// Each function initialises a Cartridge for a specific mapper type.
// Call Cartridge::clear() is handled internally (each setup starts fresh).
//
// ROM data is passed as a const pointer (lives in XIP flash or a SRAM buffer).
// RAM data is passed as a mutable pointer (lives in SRAM).
//
// The switch callbacks defined in mappers.cc are annotated with RAMFUNC so
// they run from SRAM on hardware and never stall on XIP cache misses.
//
// See spec.md §5.1, bootstrapping.md Stage 9.

#include "cartridges/cartridge.h"
#include <cstddef>

// ---------------------------------------------------------------------------
// ROM mappers (read-only; no SRAM allocation needed)
// ---------------------------------------------------------------------------

// Linear ROM.  ROM up to 64 KB; last segment is mirrored if rom_size < 64 KB.
void mapper_setup_rom(Cartridge& c, const uint8_t* rom_base, size_t rom_size);

// 32 KB ROM mirrored across all four 16 KB MSX pages using the ((i+2)%4) pattern.
// Designed for cartridges that place their header at 0x4000 within the ROM file.
void mapper_setup_rom_32k_mirrored(Cartridge& c, const uint8_t* rom_base);

// Konami 8 KB banking.
// Pages 2–5 (0x4000–0xBFFF) are switchable; writes to any of those pages
// set the 8 KB segment for that page.
void mapper_setup_konami(Cartridge& c, const uint8_t* rom_base);

// Konami without 0x6000 register (Zanac / SCC variant).
// Page 2 (0x4000–0x5FFF) and page 3 (0x6000–0x7FFF) are fixed;
// pages 4–5 (0x8000–0xBFFF) are switchable.
void mapper_setup_konami_z(Cartridge& c, const uint8_t* rom_base);

// ASCII 8 KB banking.
// Four independent 8 KB windows in 0x4000–0xBFFF; each switched by a write
// to a specific sub-range within page 3 (0x6000–0x77FF).
void mapper_setup_ascii8(Cartridge& c, const uint8_t* rom_base);

// ASCII 16 KB banking.
// Two independent 16 KB windows in 0x4000–0xBFFF; each switched by a write
// to page 3; segments are 16 KB (two consecutive 8 KB chunks).
void mapper_setup_ascii16(Cartridge& c, const uint8_t* rom_base);

// ---------------------------------------------------------------------------
// RAM mapper (read-write; SRAM allocation by caller)
// ---------------------------------------------------------------------------

// Flat RAM.  ram_size is rounded up to the nearest 8 KB; remaining segments
// are left null (no read/write, bus returns open-bus value).
void mapper_setup_ram(Cartridge& c, uint8_t* ram_base, size_t ram_size);
