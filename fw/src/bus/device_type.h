#pragma once
// device_type.h — Collection device type registry.
//
// A collection device is an MSX bus device declared in a payload manifest
// (PSG, SCC, OPL4, V9990, Sunrise IDE, …).  These are NOT platform
// capabilities; they are provisioned per-payload subject to resource availability.

#include <cstdint>

enum class DeviceType : uint8_t {
    UNKNOWN     = 0,
    PSG         = 1,   // AY-3-8910 / YM2149 — IO 0xA0-0xA2
    SCC         = 2,   // Konami SCC/SCC+ — memory-mapped (Konami SCC mapper)
    OPL4        = 3,   // YMF278B — IO 0x7C-0x7F + 0xF5-0xF7
    V9990       = 4,   // Yamaha V9990/G9000 VDP — memory-mapped
    SUNRISE_IDE = 5,   // Sunrise ATA-IDE v2 — memory-mapped
};

struct DeviceTypeInfo {
    DeviceType  type;
    const char* name;           // canonical string, e.g. "psg"
    uint32_t    sram_bytes;     // SRAM consumed while active
    uint16_t    io_port_base;   // first IO port; 0 = memory-mapped
    uint8_t     io_port_count;  // number of consecutive IO ports; 0 = memory-mapped
    bool        memory_mapped;  // true → occupies a MSX subslot
};

// Parse a device type string (e.g. "psg") → DeviceType.
// Returns DeviceType::UNKNOWN for unrecognised strings.
DeviceType device_type_from_string(const char* s);

// Return the canonical lowercase string for a device type.
// Returns "unknown" for DeviceType::UNKNOWN.
const char* device_type_to_string(DeviceType t);

// Return the full info record for a device type, or nullptr for UNKNOWN.
const DeviceTypeInfo* device_type_info(DeviceType t);
