#pragma once
// peripheral_descriptor.h — Hardware descriptor for collection-addressable peripherals.
//
// Each entry describes the bus resource footprint of a peripheral a collection
// manifest can request (PSG, SCC, OPL4, V9990, Sunrise IDE).
// This header has no dependency on bus/ so it is safe to include from any layer.

#include <cstdint>

struct PeripheralDescriptor {
    const char* name;           // canonical manifest string, e.g. "psg"
    uint8_t     type_id;        // stable on-disk ID (PayloadDeviceRecord.type)
    uint32_t    sram_bytes;     // SRAM consumed while active
    uint16_t    io_port_base;   // first IO port; 0 = memory-mapped
    uint8_t     io_port_count;  // number of consecutive IO ports; 0 = memory-mapped
    bool        memory_mapped;  // true → occupies an MSX subslot
};

// Returns nullptr for unknown names.
const PeripheralDescriptor* find_peripheral_by_name(const char* name);

// Returns nullptr for unknown IDs.
const PeripheralDescriptor* find_peripheral_by_id(uint8_t type_id);
