// JLPiCart firmware — Stage 4: API window v1 boot sequence.
//
// Boot order (spec.md §4.4, bootstrapping.md Stage 4):
//   1. diag/log init
//   2. SecurityPosture (OTP read — once, never again)
//   3. PolicyStore (flash read + HMAC verify)
//   4. CapabilityRegistry (declared → allowed)
//   5. ApiWindow init (header + rings)
//   6. Print boot banner
//   7. Service loop (poll request ring, dispatch, post response)
//
// The API window is not yet mapped into the MSX bus — that integration
// requires the bus loop (Stage 5).  Until then the window is initialised
// and serviced from a software loop so host tests and emulator tests can
// exercise the framing and service logic.
//
// TODO(stage5): map buf_ into MSX page 2 subslot 2 via bus layer.
//
// See fw/spec.md and fw/bootstrapping.md for context.

#include "diag/diag.h"
#include "log/log.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"
#include "msx/api/api_window.h"
#include "pico/stdlib.h"
#include <cstdio>

// Build ID injected by CMake (-DFW_BUILD_ID=...).
#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

int main() {
    // 1. Init diagnostics and logging.
    log_init();
    log_info("JLPiCart boot start build=" FW_BUILD_ID);

    // 2. Read security posture from OTP (exactly once).
    const OtpReader& otp = get_hardware_otp_reader();
    SecurityPosture posture = SecurityPosture::read(otp);
    {
        char buf[128];
        posture.describe(buf, sizeof(buf));
        log_info(buf);
    }

    // 3. Load and verify policy.
    PolicyStore policy_store;
    DiagStatus ps = policy_store.load(posture);
    if (!ps.ok()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "policy load failed: %s (safe defaults applied)",
                 diag_code_to_string(ps.code));
        log_warn(buf);
    }

    // 4. Build capability registry (declared → allowed).
    CapabilityRegistry registry;
    registry.init(BoardDescriptor::for_current_board(),
                  kDriverDescriptors, kDriverDescriptorCount,
                  policy_store.info());

    // 5. Init API window (16KB buffer; bus mapping added in Stage 5).
    ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    log_info("API window initialised");

    // 6. Print boot banner to log (flushed to UART/OLED in later stages).
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "JLPiCart " FW_BUILD_ID " | declared=%zu allowed=%zu | policy_ok=%d secure_boot=%d",
            registry.declared_count(),
            registry.allowed_count(),
            policy_store.loaded_ok(),
            posture.secure_boot_enabled);
        log_info(buf);
    }

    // 7. Service loop — poll the request ring and process one frame per iteration.
    // TODO(stage5): replace with interrupt-driven or Core1 handler once bus is wired.
    while (true) {
        api_win.service_once();
        tight_loop_contents();
    }
}
