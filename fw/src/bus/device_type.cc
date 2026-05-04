// device_type.cc — Collection device type registry.

#include "bus/device_type.h"
#include <cstring>

static const DeviceTypeInfo kDeviceTypes[] = {
    // PSG: IO 0xA0–0xA2 (3 ports)
    { DeviceType::PSG,         "psg",          128u,  0xA0u, 3u, false },
    // SCC: memory-mapped, embedded in KONAMI_SCC mapper
    { DeviceType::SCC,         "scc",          256u,  0u,    0u, true  },
    // OPL4: IO 0x7C–0x7F (primary range; 0xF5–0xF7 is secondary, not checked here)
    { DeviceType::OPL4,        "opl4",        5120u,  0x7Cu, 4u, false },
    // V9990: memory-mapped VDP
    { DeviceType::V9990,       "v9990",       2048u,  0u,    0u, true  },
    // Sunrise IDE: memory-mapped storage controller
    { DeviceType::SUNRISE_IDE, "sunrise_ide", 1024u,  0u,    0u, true  },
};

static constexpr size_t kDeviceTypeCount =
    sizeof(kDeviceTypes) / sizeof(kDeviceTypes[0]);

DeviceType device_type_from_string(const char* s) {
    if (!s || s[0] == '\0') return DeviceType::UNKNOWN;
    for (size_t i = 0; i < kDeviceTypeCount; ++i)
        if (strcmp(s, kDeviceTypes[i].name) == 0)
            return kDeviceTypes[i].type;
    return DeviceType::UNKNOWN;
}

const char* device_type_to_string(DeviceType t) {
    for (size_t i = 0; i < kDeviceTypeCount; ++i)
        if (kDeviceTypes[i].type == t) return kDeviceTypes[i].name;
    return "unknown";
}

const DeviceTypeInfo* device_type_info(DeviceType t) {
    for (size_t i = 0; i < kDeviceTypeCount; ++i)
        if (kDeviceTypes[i].type == t) return &kDeviceTypes[i];
    return nullptr;
}
