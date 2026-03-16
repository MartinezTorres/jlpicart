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

    // MSX mapper emulation (Stage 9).  No SRAM budget: ROM lives in XIP flash and
    // mapper register state is negligible.  Mapper type (ROM/Konami/ASCII8/…) is
    // specified per-payload in the Collection manifest, not in this descriptor.
    // See bus/mapping_plan.h and bootstrapping.md Stage 9.
    { "sw.mapper", {} },

    // PSG audio emulation (Stage 27).
    { "sw.psg",   {} },

    // Future entries are added here following the same pattern:
    // { "sw.scc",   {} },  // SCC audio emulation
    // { "sw.menu",  {} },  // menu host ABI
    // { "net.esp32",{} },  // ESP32 network transport
};

const size_t kDriverDescriptorCount =
    sizeof(kDriverDescriptors) / sizeof(kDriverDescriptors[0]);
