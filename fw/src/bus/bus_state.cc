// bus_state.cc — BUS global variable definitions.
//
// Separated from bus.cc so that BUS::subslots[] and related state are
// available in host-test builds even though the bus loop (bus.cc) is
// hardware-only.  Both translation units share the same declarations in
// bus.h; this one is compiled for all targets.
//
// subslot_indexes, is_expanded, and reset_callback are placed in scratch_y
// on hardware so Core 0 reads them without XIP cache stalls.  subslots[]
// is 35 KB and cannot fit in scratch_y; it lives in normal SRAM.

#include "bus/bus.h"

#ifndef JLPICART_HOST_TEST
#  define BUS_SCRATCH __attribute__((section(".scratch_y")))
#else
#  define BUS_SCRATCH
#endif

namespace BUS {

    Subslot       subslots[SUBSLOT_COUNT];

    uint8_t       subslot_indexes[4]   BUS_SCRATCH = {0, 0, 0, 0};
    bool          is_expanded          BUS_SCRATCH = false;
    ResetCallback reset_callback       BUS_SCRATCH = nullptr;

}  // namespace BUS
