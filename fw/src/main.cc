// JLPiCart firmware — Stage 6: Storage substrate + Menu Host ABI + API window.
//
// Boot order (spec.md §4.4, bootstrapping.md Stage 6):
//   1. diag/log init
//   2. SecurityPosture (OTP read — once, never again)
//   3. Storage init: KvStore (SYSTEM_KV) + AppendLog (EVENT_LOG)
//   4. PolicyStore (flash read + HMAC verify)
//   5. CapabilityRegistry (declared → allowed)
//   6. ApiWindow init (header + rings)
//   7. MenuMailbox init (menu page header + mailbox registers)
//   8. Append BOOT record to EVENT_LOG
//   9. Print boot banner
//  10. Service loop (poll request ring, dispatch, post response; tick mailbox)
//
// Neither the API window nor the menu page is yet wired into the MSX bus —
// that integration belongs to the bus-layer stage.  Both objects are
// initialised here so host tests and emulator tests can exercise the protocol
// logic independently.
//
// TODO(bus-layer): map api_win.buf() into MSX page 2 subslot 2 via bus layer.
// TODO(bus-layer): map menu_page into MSX page 1 subslot 1 and load menu_stub.rom.
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
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
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
    // (Storage init comes first so the flash device is ready before policy read.)
    FlashDevice& flash = FlashDevice::hardware();

    // 3. Init storage substrate (must complete before bus start per spec §6.5).
    KvStore kv_store;
    {
        DiagStatus s = kv_store.init(flash, FLASH_SYSTEM_KV_OFS, FLASH_SYSTEM_KV_SIZE);
        if (!s.ok()) {
            log_warn("SYSTEM_KV init failed — storage may be empty");
        } else {
            log_info("SYSTEM_KV ready");
        }
    }
    AppendLog event_log;
    {
        DiagStatus s = event_log.init(flash, FLASH_EVENT_LOG_OFS, FLASH_EVENT_LOG_SIZE);
        if (!s.ok()) {
            log_warn("EVENT_LOG init failed");
        } else {
            log_info("EVENT_LOG ready");
        }
    }

    const OtpReader& otp = get_hardware_otp_reader();
    SecurityPosture posture = SecurityPosture::read(otp);
    {
        char buf[128];
        posture.describe(buf, sizeof(buf));
        log_info(buf);
    }

    // 4. Load and verify policy.
    PolicyStore policy_store;
    DiagStatus ps = policy_store.load(posture);
    if (!ps.ok()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "policy load failed: %s (safe defaults applied)",
                 diag_code_to_string(ps.code));
        log_warn(buf);
    }

    // 5. Build capability registry (declared → allowed).
    CapabilityRegistry registry;
    registry.init(BoardDescriptor::for_current_board(),
                  kDriverDescriptors, kDriverDescriptorCount,
                  policy_store.info());

    // 6. Init API window (16KB buffer; bus mapping deferred to bus-layer stage).
    ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    log_info("API window initialised");

    // 7. Init Menu mailbox (16KB page buffer; bus mapping deferred to bus-layer stage).
    static uint8_t menu_page[MENU_PAGE_SIZE];
    MenuMailbox menu_mbx;
    menu_mbx.init(menu_page, MENU_DATA_OFS); // stub_entry = 0x0100 (page-relative)
    log_info("Menu mailbox initialised");

    // 8. Append BOOT record to EVENT_LOG (spec §6.5 boot integration).
    {
        BootRecord boot_rec = {};
        const char* build_id = FW_BUILD_ID;
        size_t id_len = strlen(build_id);
        if (id_len > sizeof(boot_rec.build_id)) id_len = sizeof(boot_rec.build_id);
        memcpy(boot_rec.build_id, build_id, id_len);
        boot_rec.boot_seq = event_log.next_seq();
        event_log.append(ALOG_TYPE_BOOT,
                         reinterpret_cast<const uint8_t*>(&boot_rec),
                         sizeof(boot_rec));
    }

    // 9. Print boot banner to log (flushed to UART/OLED in later stages).
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

    // 10. Service loop — poll the API request ring and tick the menu mailbox.
    // TODO(bus-layer): replace with interrupt-driven or Core1 handler once bus is wired.
    while (true) {
        api_win.service_once();
        menu_mbx.tick();
        tight_loop_contents();
    }
}
