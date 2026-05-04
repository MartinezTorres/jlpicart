#pragma once
// bus_map.h — map flat SRAM regions into BUS::cartridges[] segments.
//
// Called from PeripheralManager::map_menu_page() and map_api_window() before
// BUS::start() to wire the 16 KB menu page (RW) and API window buffer (RO)
// into the bus loop.  Must not be called after BUS::start().
//
// On host test builds (JLPICART_HOST_TEST), both functions are no-ops.

#include <cstdint>

namespace BusMap {

    // Map a read-only 16 KB region into BUS::cartridges[subslot] at page_base.
    // page_base must be one of: 0x0000, 0x4000, 0x8000, 0xC000.
    // Fills the two 8 KB read segments for that page; write segments are
    // left unchanged (null pointer = writes silently discarded by bus loop).
    void map_ro_region(uint8_t subslot, uint16_t page_base, const uint8_t* data);

    // Map a read-write 16 KB region (Z80 can write back, e.g., mailbox).
    // Fills both read and write segment pointers for the two 8 KB segments.
    void map_rw_region(uint8_t subslot, uint16_t page_base, uint8_t* data);

}  // namespace BusMap
