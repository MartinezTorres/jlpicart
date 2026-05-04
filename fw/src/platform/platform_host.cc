// platform_host.cc — Linux host implementation of Platform HAL.
//
// Used when building main.cc as a native executable for integration tests.
// xip_map() returns nullptr; callers must handle gracefully.
// start() runs the service function on the single thread.

#include "platform/platform.h"

namespace Platform {

const uint8_t* xip_map(uint32_t) {
    return nullptr;
}

[[noreturn]] void start(void (*service_fn)()) {
    while (true) service_fn();
}

bool msx_clock_present() {
    return true;
}

}  // namespace Platform
