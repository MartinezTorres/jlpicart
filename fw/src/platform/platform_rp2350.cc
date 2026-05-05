// platform_rp2350.cc — RP2350 implementation of Platform HAL.
//
// Hardware-only: excluded from host-test builds via CMakeLists.txt.

#include "platform/platform.h"
#include "boards/gpio_defs.h"
#include "bus/bus.h"
#include "diag/log.h"
#include <pico.h>
#include <pico/multicore.h>
#include <pico/time.h>
#include <hardware/structs/sio.h>

namespace Platform {

const uint8_t* xip_map(uint32_t flash_offset) {
    return reinterpret_cast<const uint8_t*>(XIP_BASE + flash_offset);
}

static void (*g_service_fn)() = nullptr;

[[noreturn]] void start(void (*service_fn)()) {
    g_service_fn = service_fn;

    multicore_launch_core1([]() {
        while (true) {
            g_service_fn();
            tight_loop_contents();
        }
    });

    BUS::reset_callback = []() {
        for (size_t i = 0; i < BUS::SUBSLOT_COUNT; ++i) {
            if (BUS::subslots[i].reset_fn)
                BUS::subslots[i].reset_fn(BUS::subslots[i]);
        }
    };

    log_info("entering bus loop on Core 0");
    BUS::start();
}

bool msx_clock_present() {
    // Count BIT64_CLK_HI transitions over a 2 ms window.
    // MSX 3.58 MHz clock produces ~7160 edges/ms — 10 transitions is a safe threshold.
    constexpr uint64_t WINDOW_US = 2000u;
    constexpr uint32_t THRESHOLD = 10u;

    bool prev = (sio_hw->gpio_hi_in & BIT64_CLK_HI) != 0u;
    uint32_t transitions = 0u;
    uint64_t deadline = time_us_64() + WINDOW_US;
    while (time_us_64() < deadline) {
        bool cur = (sio_hw->gpio_hi_in & BIT64_CLK_HI) != 0u;
        if (cur != prev) {
            ++transitions;
            prev = cur;
        }
    }
    return transitions >= THRESHOLD;
}

}  // namespace Platform
