// mapping_plan.cc — MappingPlan helpers.

#include "bus/mapping_plan.h"
#include "bus/device_type.h"
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

    // Memory mapper entry (if ROM data is present).
    MapperType mt = mapper_type_from_string(record.mapper_type);
    if (mt != MapperType::NONE && record.data_size > 0) {
        MappingEntry& entry = plan.entries[0];
        entry.mapper_type   = mt;
        entry.subslot       = record.subslot;
        entry.rom_size      = record.data_size;
        entry.ram_data      = nullptr;
        entry.ram_size      = 0;
#ifndef JLPICART_HOST_TEST
        static constexpr uint32_t XIP_BASE = 0x10000000u;
        entry.rom_data = reinterpret_cast<const uint8_t*>(XIP_BASE + record.data_flash_offset);
#else
        entry.rom_data = nullptr;
#endif
        plan.entry_count = 1;
        plan.expanded    = false;
    }

    // IO devices from device records (memory-mapped devices handled via mapper entries).
    for (uint8_t i = 0; i < record.device_count && i < PAYLOAD_DEVICES_MAX; ++i) {
        const PayloadDeviceRecord& pdr = record.devices[i];
        const DeviceType dt = static_cast<DeviceType>(pdr.type);
        const DeviceTypeInfo* info = device_type_info(dt);
        if (!info || info->memory_mapped) continue;
        if (plan.io_device_count >= MAPPING_MAX_IO_DEVICES) break;
        IoDeviceEntry& io = plan.io_devices[plan.io_device_count++];
        if (dt == DeviceType::PSG) {
            io.type = IoDeviceType::PSG;
        } else if (dt == DeviceType::OPL4) {
            io.type = IoDeviceType::OPL4;
            size_t plen = strnlen(pdr.params, sizeof(pdr.params));
            if (plen > 0 && plen < PAYLOAD_ID_MAX)
                memcpy(io.wave_payload_id, pdr.params, plen + 1u);
        }
    }

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
    plan.entry_count = 1;
    plan.expanded    = false;

    // IO devices from manifest device list.
    for (uint8_t i = 0; i < pe.device_count && i < PAYLOAD_DEVICES_MAX; ++i) {
        const ManifestDeviceEntry& de = pe.devices[i];
        const DeviceTypeInfo* info = device_type_info(de.type);
        if (!info || info->memory_mapped) continue;
        if (plan.io_device_count >= MAPPING_MAX_IO_DEVICES) break;
        IoDeviceEntry& io = plan.io_devices[plan.io_device_count++];
        if (de.type == DeviceType::PSG) {
            io.type = IoDeviceType::PSG;
        } else if (de.type == DeviceType::OPL4) {
            io.type = IoDeviceType::OPL4;
            size_t plen = strnlen(de.params, sizeof(de.params));
            if (plen > 0 && plen < PAYLOAD_ID_MAX)
                memcpy(io.wave_payload_id, de.params, plen + 1u);
        }
    }

    return plan;
}
