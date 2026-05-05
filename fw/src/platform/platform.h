#pragma once
// platform.h — Hardware abstraction for platform-specific firmware entry points.
//
// Two things vary between RP2350 and host builds:
//   xip_map()  — translate a flash byte-offset to a readable pointer
//   start()    — enter the main loop (multicore on hardware, single-thread on host)
//
// All other platform differences (PIO, DMA, GPIO) live in their own drivers
// and do not need to appear here.

#include "spine/resource_model.h"
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// Board capability declarations
// ---------------------------------------------------------------------------

struct BoardCapabilityDecl {
    const char*          name;
    bool                 safe_verify;
    ResourceRequirements resources = {};
};

struct BoardDescriptor {
    const char*                  board_id;
    const BoardCapabilityDecl*   capabilities;
    size_t                       capability_count;

    static const BoardDescriptor& for_current_board();
};

// ---------------------------------------------------------------------------

namespace Platform {

    // Map a flash byte-offset to a readable pointer (zero-copy via XIP cache).
    // RP2350: returns XIP_BASE + flash_offset.
    // Host:   returns nullptr — callers must handle gracefully.
    const uint8_t* xip_map(uint32_t flash_offset);

    // Enter the main firmware loop.  Never returns.
    // RP2350: launches service_fn on Core 1 (looped with tight_loop_contents),
    //         wires BUS::reset_callback, then Core 0 enters BUS::start().
    // Host:   calls service_fn() in a tight loop on the current thread.
    [[noreturn]] void start(void (*service_fn)());

    // Returns true if an MSX 3.58 MHz clock signal is present on GPIO64_CLK.
    // Samples pin transitions for ~2 ms; false means no MSX bus connected.
    // RP2350: reads sio_hw->gpio_hi_in, counts edges over a 2 ms window.
    // Host:   always returns true (no physical bus to detect).
    bool msx_clock_present();

}  // namespace Platform
