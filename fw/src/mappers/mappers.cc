// mappers.cc — MSX mapper implementations.

#include "mappers/mappers.h"
#include "boards/gpio_defs.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Banking callbacks — run from SRAM (RAMFUNC) to avoid XIP cache stalls.
// Each callback decodes the address and data from the raw GPIO bus word and
// updates the affected memory_read_addresses[] entry in the cartridge.
// ---------------------------------------------------------------------------

static std::pair<bool, uint8_t> RAMFUNC(konami_write_cb)(Cartridge& c, uint32_t bus) {
    // Konami: write address determines which 8 KB page to switch.
    // Page index = (A[15:0] / 8 KB) % 8.
    uint32_t page    = ((bus >> GPIO_A0) / 0x2000u) % 8u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[page] = &c.rom_base[segment * 8192u];
    return {false, 0};
}

static std::pair<bool, uint8_t> RAMFUNC(ascii8_write_cb)(Cartridge& c, uint32_t bus) {
    // ASCII8: four 8 KB windows, each selected by A[12:11] within the register area.
    // Page index within the 4-window bank = (A[15:0] / 2 KB) % 4.
    uint32_t page    = ((bus >> GPIO_A0) / 0x800u) % 4u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[2u + page] = &c.rom_base[segment * 8192u];
    return {false, 0};
}

static std::pair<bool, uint8_t> RAMFUNC(ascii16_write_cb)(Cartridge& c, uint32_t bus) {
    // ASCII16: two 16 KB windows; bit 12 of the address selects window 0 or 1.
    // Each window spans two consecutive 8 KB segments.
    uint32_t window  = ((bus >> GPIO_A0) / 0x1000u) % 2u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[2u + 2u * window + 0u] = &c.rom_base[(2u * segment + 0u) * 8192u];
    c.memory_read_addresses[2u + 2u * window + 1u] = &c.rom_base[(2u * segment + 1u) * 8192u];
    return {false, 0};
}

// ---------------------------------------------------------------------------
// mapper_setup_rom
// ---------------------------------------------------------------------------

void mapper_setup_rom(Cartridge& c, const uint8_t* rom_base, size_t rom_size) {
    c.clear();
    c.name     = "rom";
    c.rom_base = rom_base;
    size_t seg_count = (rom_size + 8191u) / 8192u;
    if (seg_count < 1u)  seg_count = 1u;
    if (seg_count > 8u)  seg_count = 8u;
    for (size_t i = 0; i < 8u; ++i) {
        // Mirror: if ROM is smaller than 64 KB, wrap within available segments.
        c.memory_read_addresses[i] = &rom_base[(i % seg_count) * 8192u];
    }
}

// ---------------------------------------------------------------------------
// mapper_setup_rom_32k_mirrored
// ---------------------------------------------------------------------------

void mapper_setup_rom_32k_mirrored(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "rom_32k_mirrored";
    c.rom_base = rom_base;
    // ((i+2)%4) pattern: designed for 32 KB ROMs whose header sits at 0x4000.
    // MSX address 0x0000–0x3FFF → ROM 0x4000–0x7FFF; 0x4000–0x7FFF → ROM 0x0000–0x3FFF.
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[((i + 2) % 4) * 8192u];
    }
}

// ---------------------------------------------------------------------------
// mapper_setup_konami
// ---------------------------------------------------------------------------

void mapper_setup_konami(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "konami";
    c.rom_base = rom_base;
    // Initial state: ROM segments 0–3 mapped to MSX pages 2–5 (0x4000–0xBFFF).
    for (int i = 0; i < 4; ++i) {
        c.memory_read_addresses[2 + i]  = &rom_base[i * 8192u];
        c.memory_write_callbacks[2 + i] = konami_write_cb;
    }
}

// ---------------------------------------------------------------------------
// mapper_setup_konami_z
// ---------------------------------------------------------------------------

void mapper_setup_konami_z(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "konami_z";
    c.rom_base = rom_base;
    // Initial state: ROM segments 0–3 at pages 2–5.
    for (int i = 0; i < 4; ++i) {
        c.memory_read_addresses[2 + i] = &rom_base[i * 8192u];
    }
    // Only pages 4–5 (0x8000–0xBFFF) are switchable (no 0x6000 register).
    c.memory_write_callbacks[4] = konami_write_cb;
    c.memory_write_callbacks[5] = konami_write_cb;
}

// ---------------------------------------------------------------------------
// mapper_setup_ascii8
// ---------------------------------------------------------------------------

void mapper_setup_ascii8(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "ascii8";
    c.rom_base = rom_base;
    // All 8 segments initially point to ROM segment 0.
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[0];
    }
    // Writes to segment 3 (0x6000–0x7FFF) switch the four 8 KB windows.
    c.memory_write_callbacks[3] = ascii8_write_cb;
}

// ---------------------------------------------------------------------------
// mapper_setup_ascii16
// ---------------------------------------------------------------------------

void mapper_setup_ascii16(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "ascii16";
    c.rom_base = rom_base;
    // All 8 segments initially interleave segments 0 and 1 (i % 2 pattern).
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[(i % 2u) * 8192u];
    }
    // Writes to segment 3 (0x6000–0x7FFF) switch the two 16 KB windows.
    c.memory_write_callbacks[3] = ascii16_write_cb;
}

// ---------------------------------------------------------------------------
// mapper_setup_ram
// ---------------------------------------------------------------------------

void mapper_setup_ram(Cartridge& c, uint8_t* ram_base, size_t ram_size) {
    c.clear();
    c.name     = "ram";
    c.ram_base = ram_base;
    size_t seg_count = (ram_size + 8191u) / 8192u;
    if (seg_count > 8u) seg_count = 8u;
    for (size_t i = 0; i < seg_count; ++i) {
        c.memory_read_addresses[i]  = &ram_base[i * 8192u];
        c.memory_write_addresses[i] = &ram_base[i * 8192u];
    }
}
