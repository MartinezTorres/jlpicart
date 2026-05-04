// bus_map.cc — BusMap implementation.

#include "bus/bus_map.h"
#include "bus/bus.h"
#include "cartridges/cartridge.h"

namespace BusMap {

    // Each 16 KB MSX page spans two 8 KB segments.
    // segment index = page_base >> 13  (8 KB = 2^13 bytes)
    static constexpr uint16_t SEG_SIZE = 0x2000u;

    static_assert(SEG_SIZE == 0x2000u, "segment size must be 8 KB");
    static_assert(sizeof(Subslot::memory_read_addresses) /
                  sizeof(Subslot::memory_read_addresses[0]) == 8,
                  "Subslot must have exactly 8 memory segments");

    void map_ro_region(uint8_t subslot, uint16_t page_base, const uint8_t* data) {
        uint8_t seg = static_cast<uint8_t>(page_base >> 13u);
        Subslot& cart = BUS::subslots[subslot];
        cart.memory_read_addresses[seg]     = data;
        cart.memory_read_addresses[seg + 1] = data + SEG_SIZE;
    }

    void map_rw_region(uint8_t subslot, uint16_t page_base, uint8_t* data) {
        uint8_t seg = static_cast<uint8_t>(page_base >> 13u);
        Subslot& cart = BUS::subslots[subslot];
        cart.memory_read_addresses[seg]      = data;
        cart.memory_read_addresses[seg + 1]  = data + SEG_SIZE;
        cart.memory_write_addresses[seg]     = data;
        cart.memory_write_addresses[seg + 1] = data + SEG_SIZE;
    }

}  // namespace BusMap
