// peripheral_descriptor.cc — Registry of collection-addressable peripherals.

#include "peripherals/peripheral_descriptor.h"
#include <cstring>

static const PeripheralDescriptor kDescriptors[] = {
    { "psg",         1,  128u, 0xA0u, 3u, false },
    { "scc",         2,  256u, 0u,    0u, true  },
    { "opl4",        3, 5120u, 0x7Cu, 4u, false },
    { "v9990",       4, 2048u, 0u,    0u, true  },
    { "sunrise_ide", 5, 1024u, 0u,    0u, true  },
};
static constexpr size_t kCount = sizeof(kDescriptors) / sizeof(kDescriptors[0]);

const PeripheralDescriptor* find_peripheral_by_name(const char* name) {
    if (!name || name[0] == '\0') return nullptr;
    for (size_t i = 0; i < kCount; ++i)
        if (strcmp(name, kDescriptors[i].name) == 0)
            return &kDescriptors[i];
    return nullptr;
}

const PeripheralDescriptor* find_peripheral_by_id(uint8_t id) {
    for (size_t i = 0; i < kCount; ++i)
        if (kDescriptors[i].type_id == id)
            return &kDescriptors[i];
    return nullptr;
}
