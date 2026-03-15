// JLPiCart firmware — Stage 13: Menu and API window bus wiring.
//
// Boot order (spec.md §4.4, bootstrapping.md Stage 13):
//   1. diag/log init
//   2. Storage init: KvStore (SYSTEM_KV) + AppendLog (EVENT_LOG)
//   3. SecurityPosture (OTP read — once, never again)
//   4. PolicyStore (flash read + HMAC verify)
//   5. CapabilityRegistry (declared → allowed)
//   6. Activation preflight: Allocator → LaunchPlan → PeripheralManager
//   7. MappingPlan: ContentStore lookup → mapping_plan_from_payload_record → apply_mapping()
//   8. ApiWindow init (header + rings)
//   9. MenuMailbox init (menu page header + mailbox registers)
//  10. Bus wiring: map_menu_page() + map_api_window() → BUS::cartridges[]
//  11. Append BOOT record to EVENT_LOG
//  12. Print boot banner
//  13. Collection install from USB (TODO: deferred to usb-host stage)
//  [hardware only]
//  14. Launch Core 1 (service loop: API window + menu mailbox)
//  15. Core 0 enters BUS::start() — never returns
//
// See fw/spec.md and fw/bootstrapping.md for context.

#include "diag/diag.h"
#include "log/log.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "spine/activation.h"
#include "allocator/allocator.h"
#include "allocator/resource_model.h"
#include "peripherals/peripheral_manager.h"
#include "bus/mapping_plan.h"
#include "content/content_store.h"
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

#ifndef JLPICART_HOST_TEST
#  include "bus/bus.h"
#  include <pico/multicore.h>
#endif

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

    // 6. Activation preflight: compute Launch Plan and activate capabilities.
    // No Collection loaded yet — use all_allowed=true (menu/standby mode),
    // which activates everything in declared ∩ allowed, subject to resource budgets.
    {
        ResourceModel resource_model;
        RequestedCapabilities requested = {};
        requested.all_allowed = true;

        Allocator allocator;
        LaunchPlan plan = allocator.compute(registry, requested, resource_model);

        PeripheralManager periph_mgr;
        periph_mgr.apply(plan, registry);
        periph_mgr.log_report(plan);
    }

    // 7. MappingPlan: look up the default payload from ContentStore.
    //    If there is no active collection or data_size == 0, fall back to
    //    an empty plan (standby/menu mode).  apply_mapping() logs the result.
    PeripheralManager map_mgr;
    {
        ContentStore content_store(kv_store);
        MappingPlan mapping_plan = {};

        if (content_store.has_active_collection()) {
            PayloadRecord pr = {};
            DiagStatus s = content_store.load_default_payload(pr);
            if (!s.ok()) {
                log_info("MappingPlan: no default payload in collection — standby");
            } else if (pr.data_size == 0) {
                log_info("MappingPlan: payload data_size=0 (ROM not loaded) — standby");
            } else {
                mapping_plan = mapping_plan_from_payload_record(pr);
                log_info("MappingPlan: active payload found — bus wiring requested");
            }
        } else {
            log_info("MappingPlan: no active collection — standby mode");
        }

        map_mgr.apply_mapping(mapping_plan);
    }

    // 8. Init API window (16KB buffer, writes "JLP1" header).
    static ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    log_info("API window initialised");

    // 9. Init Menu mailbox (16KB page buffer, writes "JLMN" header + stub code).
    static uint8_t menu_page[MENU_PAGE_SIZE];
    static MenuMailbox menu_mbx;
    menu_mbx.init(menu_page, MENU_DATA_OFS);
    log_info("Menu mailbox initialised");

    // 10. Wire bus mappings: menu page at subslot 1 / 0x4000 (RW),
    //     API window at subslot 2 / 0x8000 (RO).
    map_mgr.map_menu_page(menu_page);
    map_mgr.map_api_window(api_win.buf());

    // 11. Append BOOT record to EVENT_LOG (spec §6.5 boot integration).
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

    // 12. Print boot banner to log (flushed to UART/OLED in later stages).
    {
        char buf[128];
        snprintf(buf, sizeof(buf),
            "JLPiCart " FW_BUILD_ID
            " | declared=%zu allowed=%zu activated=%zu"
            " | policy_ok=%d secure_boot=%d",
            registry.declared_count(),
            registry.allowed_count(),
            registry.activated_count(),
            policy_store.loaded_ok(),
            posture.secure_boot_enabled);
        log_info(buf);
    }

    // 13. Collection install from USB (deferred to usb-host stage).
    // TODO(usb-host): wire UsbInstallScanner here once tinyusb is integrated.
    log_info("collection install: USB host not integrated (TODO(usb-host))");

    // 14–15. On hardware: launch Core 1 for the service loop, then Core 0 enters
    //        BUS::start() and never returns.
    //        On host (JLPICART_HOST_TEST): run the service loop on the single thread.

#ifndef JLPICART_HOST_TEST
    // Core 1 service loop — handles API window and menu mailbox.
    // api_win and menu_mbx are static (file-visible from any point in this function)
    // so the function pointer can reach them without capturing.
    static ApiWindow*   g_api_win  = &api_win;
    static MenuMailbox* g_menu_mbx = &menu_mbx;
    multicore_launch_core1([]() {
        while (true) {
            g_api_win->service_once();
            g_menu_mbx->tick();
            tight_loop_contents();
        }
    });

    // BUS::reset_callback: stub — no cartridge re-init needed in standby mode.
    BUS::reset_callback = nullptr;

    // Core 0 enters the MSX bus loop.  [[noreturn]]
    log_info("entering bus loop on Core 0");
    BUS::start();
#else
    while (true) {
        api_win.service_once();
        menu_mbx.tick();
    }
#endif
}
