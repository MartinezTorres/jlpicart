// bus_map.cc — BusMap implementation.
//
// On host test builds this module compiles to no-ops so that unit tests can
// link without the BUS:: and Cartridge dependencies.

#include "bus/bus_map.h"

#ifndef JLPICART_HOST_TEST

#include "bus/bus.h"
#include "cartridges/cartridge.h"

namespace BusMap {

    // Each 16 KB MSX page spans two 8 KB segments.
    // segment index = page_base >> 13  (8 KB = 2^13 bytes)
    static constexpr uint16_t SEG_SIZE = 0x2000u;

    // Segment indices for any valid 16 KB page_base (0x0000/0x4000/0x8000/0xC000)
    // are 0–1, 2–3, 4–5, 6–7 — all within the Cartridge::memory_*[8] arrays.
    static_assert(SEG_SIZE == 0x2000u, "segment size must be 8 KB");
    static_assert(sizeof(Cartridge::memory_read_addresses) /
                  sizeof(Cartridge::memory_read_addresses[0]) == 8,
                  "Cartridge must have exactly 8 memory segments");

    void map_ro_region(uint8_t subslot, uint16_t page_base, const uint8_t* data) {
        uint8_t seg = static_cast<uint8_t>(page_base >> 13u);
        Cartridge& cart = BUS::cartridges[subslot];
        cart.memory_read_addresses[seg]     = data;
        cart.memory_read_addresses[seg + 1] = data + SEG_SIZE;
    }

    void map_rw_region(uint8_t subslot, uint16_t page_base, uint8_t* data) {
        uint8_t seg = static_cast<uint8_t>(page_base >> 13u);
        Cartridge& cart = BUS::cartridges[subslot];
        cart.memory_read_addresses[seg]      = data;
        cart.memory_read_addresses[seg + 1]  = data + SEG_SIZE;
        cart.memory_write_addresses[seg]     = data;
        cart.memory_write_addresses[seg + 1] = data + SEG_SIZE;
    }

}  // namespace BusMap

#else  // JLPICART_HOST_TEST

namespace BusMap {
    void map_ro_region(uint8_t, uint16_t, const uint8_t*) {}
    void map_rw_region(uint8_t, uint16_t, uint8_t*)       {}
}  // namespace BusMap

#endif  // JLPICART_HOST_TEST
