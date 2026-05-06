#pragma once
// mappers.h — MSX mapper types, MappingPlan, and Cartridge setup functions.
//
// Adding a new mapper requires touching this file (MapperType enum + setup fn),
// mappers.cc (implementation), and nothing else — the plan builders here
// translate manifest strings to MapperType automatically.

#include "bus/cartridge.h"
#include "content/collection_format.h"
#include "peripherals/peripheral_descriptor.h"
#include "content/manifest.h"
#include "peripherals/scc.h"
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// MapperType — canonical mapper identifier (mirrors manifest "mapper_type" strings)
// ---------------------------------------------------------------------------

enum class MapperType : uint8_t {
    ROM,               // Linear ROM (up to 64 KB, mirrored if smaller)
    ROM_32K_MIRRORED,  // 32 KB ROM with header at 0x4000, mirrored pattern
    KONAMI,            // Konami 8 KB banking (pages 2–5 switchable)
    KONAMI_SCC,        // Konami SCC: Konami banking + SCC sound chip registers
    KONAMI_Z,          // Konami without 0x6000 register (pages 4–5 switchable)
    ASCII8,            // ASCII 8 KB banking
    ASCII16,           // ASCII 16 KB banking
    RAM,               // Flat RAM (no banking)
    NONE,              // No mapper (slot disabled)
};

MapperType  mapper_type_from_string(const char* s);
const char* mapper_type_to_string(MapperType t);

// ---------------------------------------------------------------------------
// MappingPlan — what to wire on the bus for one collection payload
// ---------------------------------------------------------------------------

struct MappingEntry {
    MapperType     mapper_type;
    uint8_t        subslot;
    const uint8_t* rom_data;
    size_t         rom_size;
    uint8_t*       ram_data;
    size_t         ram_size;
};

static constexpr size_t MAPPING_MAX_ENTRIES = 4;

enum class IoDeviceType : uint8_t {
    NONE = 0,
    PSG,
    OPL4,
};

struct IoDeviceEntry {
    IoDeviceType type;
    char         wave_payload_id[PAYLOAD_ID_MAX];
};

static constexpr size_t MAPPING_MAX_IO_DEVICES = 4;

struct MappingPlan {
    MappingEntry  entries[MAPPING_MAX_ENTRIES];
    size_t        entry_count;
    IoDeviceEntry io_devices[MAPPING_MAX_IO_DEVICES];
    size_t        io_device_count;
    bool          expanded;
};

MappingPlan mapping_plan_from_payload_record(const PayloadRecord& record);
MappingPlan mapper_plan_from_manifest(const CollectionManifest& manifest,
                                       uint8_t payload_index);

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

// Konami SCC: 8 KB banking with SCC sound chip register space.
// Pages 2–5 (0x4000–0xBFFF) are switchable; writing 0x3F to segment 4
// (0x8000–0x97FF) enables the SCC register space at 0x9800–0x9FFF.
// SccState must outlive the Cartridge; the caller owns the SccState object.
void mapper_setup_konami_scc(Cartridge& c, const uint8_t* rom_base,
                              SccState& state);

// ---------------------------------------------------------------------------
// RAM mapper (read-write; SRAM allocation by caller)
// ---------------------------------------------------------------------------

// Flat RAM.  ram_size is rounded up to the nearest 8 KB; remaining segments
// are left null (no read/write, bus returns open-bus value).
void mapper_setup_ram(Cartridge& c, uint8_t* ram_base, size_t ram_size);
