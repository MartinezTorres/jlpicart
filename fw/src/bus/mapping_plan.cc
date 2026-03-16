// mapping_plan.cc — MappingPlan helpers.

#include "bus/mapping_plan.h"
#include <cstring>

// ---------------------------------------------------------------------------
// mapper_type_from_string / mapper_type_to_string
// ---------------------------------------------------------------------------

MapperType mapper_type_from_string(const char* s) {
    if (!s || s[0] == '\0')           return MapperType::NONE;
    if (strcmp(s, "rom")              == 0) return MapperType::ROM;
    if (strcmp(s, "rom_32k_mirrored") == 0) return MapperType::ROM_32K_MIRRORED;
    if (strcmp(s, "konami")           == 0) return MapperType::KONAMI;
    if (strcmp(s, "konami_scc")       == 0) return MapperType::KONAMI_SCC;
    if (strcmp(s, "konami_z")         == 0) return MapperType::KONAMI_Z;
    if (strcmp(s, "ascii8")           == 0) return MapperType::ASCII8;
    if (strcmp(s, "ascii16")          == 0) return MapperType::ASCII16;
    if (strcmp(s, "ram")              == 0) return MapperType::RAM;
    return MapperType::NONE;
}

const char* mapper_type_to_string(MapperType t) {
    switch (t) {
        case MapperType::ROM:              return "rom";
        case MapperType::ROM_32K_MIRRORED: return "rom_32k_mirrored";
        case MapperType::KONAMI:           return "konami";
        case MapperType::KONAMI_SCC:       return "konami_scc";
        case MapperType::KONAMI_Z:         return "konami_z";
        case MapperType::ASCII8:           return "ascii8";
        case MapperType::ASCII16:          return "ascii16";
        case MapperType::RAM:              return "ram";
        case MapperType::NONE:             return "none";
    }
    return "none";
}

// ---------------------------------------------------------------------------
// mapping_plan_from_payload_record
// ---------------------------------------------------------------------------

MappingPlan mapping_plan_from_payload_record(const PayloadRecord& record)
{
    MappingPlan plan = {};

    MapperType mt = mapper_type_from_string(record.mapper_type);
    if (mt == MapperType::NONE) return plan;   // no mapper specified
    if (record.data_size == 0)  return plan;   // ROM not yet written to flash

    MappingEntry& entry  = plan.entries[0];
    entry.mapper_type    = mt;
    entry.subslot        = record.subslot;
    entry.rom_size       = record.data_size;
    entry.ram_data       = nullptr;
    entry.ram_size       = 0;

#ifndef JLPICART_HOST_TEST
    // XIP_BASE = 0x10000000 on RP2350.  ROM data is directly readable at this address.
    static constexpr uint32_t XIP_BASE = 0x10000000u;
    entry.rom_data = reinterpret_cast<const uint8_t*>(XIP_BASE + record.data_flash_offset);
#else
    // XIP is not available on the host; caller must not dereference rom_data.
    entry.rom_data = nullptr;
#endif

    plan.entry_count = 1;
    plan.expanded    = false;  // single subslot; no subslot expansion register
    return plan;
}

// ---------------------------------------------------------------------------
// mapper_plan_from_manifest
// ---------------------------------------------------------------------------

MappingPlan mapper_plan_from_manifest(const CollectionManifest& manifest,
                                       uint8_t payload_index)
{
    MappingPlan plan = {};

    if (payload_index >= manifest.payload_count) return plan;

    const PayloadEntry& pe = manifest.payloads[payload_index];

    MapperType mt = mapper_type_from_string(pe.mapper_type);
    if (mt == MapperType::NONE) return plan;  // no mapper field → empty plan

    MappingEntry& entry  = plan.entries[0];
    entry.mapper_type    = mt;
    entry.subslot        = pe.subslot;       // 0–3
    entry.rom_data       = nullptr;          // ROM loading deferred to content-load stage
    entry.rom_size       = 0;
    entry.ram_data       = nullptr;
    entry.ram_size       = 0;
    plan.entry_count     = 1;
    plan.expanded        = false;  // single entry → no subslot expansion needed

    return plan;
}
