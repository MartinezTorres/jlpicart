// driver_descriptor_table.cc — the ONLY software capability declaration list.
//
// Add a new entry here when a new software capability is implemented.
// Remove the entry when the capability is permanently removed.
// Never scatter capability names across multiple files.
// See bootstrapping.md Appendix.

#include "driver_descriptor.h"

const DriverDescriptor kDriverDescriptors[] = {
    // Core API service — always present; provides core.ping, core.get_info, etc.
    { "api.core" },

    // Future entries are added here following the same pattern:
    // { "sw.psg"      },  // PSG audio emulation
    // { "sw.scc"      },  // SCC audio emulation
    // { "sw.mapper"   },  // MSX mapper emulation
    // { "sw.menu"     },  // menu host ABI
    // { "net.esp32"   },  // ESP32 network transport
};

const size_t kDriverDescriptorCount =
    sizeof(kDriverDescriptors) / sizeof(kDriverDescriptors[0]);
