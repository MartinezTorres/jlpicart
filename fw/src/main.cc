// JLPiCart firmware — Stage 5: Menu Host ABI + API window boot sequence.
//
// Boot order (spec.md §4.4, bootstrapping.md Stage 5):
//   1. diag/log init
//   2. SecurityPosture (OTP read — once, never again)
//   3. PolicyStore (flash read + HMAC verify)
//   4. CapabilityRegistry (declared → allowed)
//   5. ApiWindow init (header + rings)
//   6. MenuMailbox init (menu page header + mailbox registers)
//   7. Print boot banner
//   8. Service loop (poll request ring, dispatch, post response; tick mailbox)
//
// Neither the API window nor the menu page is yet wired into the MSX bus —
// that integration belongs to Stage 6 (bus layer mapping).  Both objects are
// initialised here so host tests and emulator tests can exercise the protocol
// logic independently.
//
// TODO(stage6): map api_win.buf() into MSX page 2 subslot 2 via bus layer.
// TODO(stage6): map menu_page into MSX page 1 subslot 1 and load menu_stub.rom.
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
#include "msx/menu/menu_host_abi.h"
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

    // 5. Init API window (16KB buffer; bus mapping added in Stage 6).
    ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    log_info("API window initialised");

    // 6. Init Menu mailbox (16KB page buffer; bus mapping and stub ROM load in Stage 6).
    static uint8_t menu_page[MENU_PAGE_SIZE];
    MenuMailbox menu_mbx;
    menu_mbx.init(menu_page, MENU_DATA_OFS); // stub_entry = 0x0100 (page-relative)
    log_info("Menu mailbox initialised");

    // 7. Print boot banner to log (flushed to UART/OLED in later stages).
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

    // 8. Service loop — poll the API request ring and tick the menu mailbox.
    // TODO(stage6): replace with interrupt-driven or Core1 handler once bus is wired.
    while (true) {
        api_win.service_once();
        menu_mbx.tick();
        tight_loop_contents();
    }
}
