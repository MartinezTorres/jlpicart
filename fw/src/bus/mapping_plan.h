#pragma once
// mapping_plan.h — MappingPlan: bridge between the manifest and the bus layer.
//
// After the Allocator produces a LaunchPlan (which capabilities are activated),
// mapper_plan_from_manifest() converts a PayloadEntry's mapper configuration
// into a MappingPlan that PeripheralManager::apply_mapping() uses to configure
// BUS::cartridges[].
//
// rom_data in MappingEntry may be nullptr when no payload is loaded;
// apply_mapping() logs and skips bus wiring in that case.

#include "content/collection_format.h"
#include "content/manifest.h"
#include <cstddef>
#include <cstdint>

// Canonical mapper type identifier.
// Must mirror the "mapper_type" string values accepted in the Collection manifest.
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

// Parse a canonical mapper type string (e.g. "konami") → MapperType.
// Returns MapperType::NONE for unknown or empty strings.
MapperType mapper_type_from_string(const char* s);

// Return the canonical lowercase string for a mapper type.
// Returns "none" for MapperType::NONE.
const char* mapper_type_to_string(MapperType t);

// One entry in a MappingPlan: maps a mapper + ROM/RAM buffer to a MSX subslot.
struct MappingEntry {
    MapperType    mapper_type;   // which mapper to configure
    uint8_t       subslot;       // MSX subslot (0–3) to mount in
    const uint8_t* rom_data;     // ROM data pointer (XIP flash); null if not loaded
    size_t        rom_size;      // ROM size in bytes (0 if not loaded)
    uint8_t*      ram_data;      // RAM buffer pointer (SRAM); null for ROM mappers
    size_t        ram_size;      // RAM size in bytes
};

static constexpr size_t MAPPING_MAX_ENTRIES = 4;

// IO-only devices declared by a collection: no memory mapping, but wired to
// MSX IO ports.  apply_mapping() iterates these after memory entries.
enum class IoDeviceType : uint8_t {
    NONE = 0,
    PSG,   // AY-3-8910 / YM2149 — IO 0xA0-0xA2
    OPL4,  // YMF278B — IO 0x7C-0x7F + 0xF5-0xF7
};

struct IoDeviceEntry {
    IoDeviceType type;
    char         wave_payload_id[PAYLOAD_ID_MAX];  // OPL4 wave ROM payload ID; "" = none
};

static constexpr size_t MAPPING_MAX_IO_DEVICES = 4;

struct MappingPlan {
    MappingEntry  entries[MAPPING_MAX_ENTRIES];
    size_t        entry_count;
    IoDeviceEntry io_devices[MAPPING_MAX_IO_DEVICES];
    size_t        io_device_count;
    // When multiple subslots are used, the subslot expansion register at 0xFFFF
    // must be enabled.  apply_mapping() sets BUS::is_expanded accordingly.
    bool          expanded;
};

// Build a MappingPlan from a stored PayloadRecord (read from ContentStore).
//
// Returns an empty plan (entry_count=0) if mapper_type is empty/NONE or
// data_size is zero (ROM not yet written to flash).
//
// On hardware: rom_data is set to the XIP flash pointer
//   (0x10000000 + record.data_flash_offset).
// On host (JLPICART_HOST_TEST): rom_data is always nullptr — XIP is
//   not available; verify other fields (mapper_type, subslot, rom_size).
MappingPlan mapping_plan_from_payload_record(const PayloadRecord& record);

// Build a MappingPlan from one payload entry in a manifest.
//
// Reads payload.mapper_type and payload.subslot to populate the first entry.
// rom_data is always set to nullptr in Stage 9 (ROM loading deferred).
// Returns an empty plan (entry_count=0) if payload_index is out of range or
// the payload has no mapper_type field.
MappingPlan mapper_plan_from_manifest(const CollectionManifest& manifest,
                                       uint8_t payload_index);
