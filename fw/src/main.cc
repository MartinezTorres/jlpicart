// JLPiCart firmware — Stage 9: MSX bus layer and sw.mapper.
//
// Boot order (spec.md §4.4, bootstrapping.md Stage 9):
//   1. diag/log init
//   2. Storage init: KvStore (SYSTEM_KV) + AppendLog (EVENT_LOG)
//   3. SecurityPosture (OTP read — once, never again)
//   4. PolicyStore (flash read + HMAC verify)
//   5. CapabilityRegistry (declared → allowed)
//   6. Activation preflight: Allocator → LaunchPlan → PeripheralManager
//   7. MappingPlan: compute from active payload manifest; apply_mapping()
//   8. ApiWindow init (header + rings)
//   9. MenuMailbox init (menu page header + mailbox registers)
//  10. Append BOOT record to EVENT_LOG
//  11. Print boot banner
//  12. Collection install from USB (TODO: deferred to usb-host stage)
//  [hardware only]
//  13. Launch Core 1 (service loop: API window + menu mailbox)
//  14. Core 0 enters BUS::start() — never returns
//
// TODO(api-bus): map api_win.buf() into MSX page 2 subslot 2 via bus layer.
// TODO(menu-bus): map menu_page into MSX page 1 subslot 1 and load menu_stub.rom.
// TODO(content-load): set rom_data in MappingPlan once USB host + content store land.
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

    // 7. MappingPlan: in standby/menu mode there is no active payload, so the
    //    plan has zero entries.  apply_mapping() logs that and returns.
    //    When a collection is launched, this block re-runs with a real payload.
    {
        // No active payload on first boot — produce an empty plan.
        CollectionManifest empty_manifest = {};
        MappingPlan mapping_plan = mapper_plan_from_manifest(empty_manifest, 0);
        PeripheralManager map_mgr;
        map_mgr.apply_mapping(mapping_plan);
    }

    // 8. Init API window (16KB buffer; bus mapping deferred to api-bus stage).
    static ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    log_info("API window initialised");

    // 9. Init Menu mailbox (16KB page buffer; bus mapping deferred to menu-bus stage).
    static uint8_t menu_page[MENU_PAGE_SIZE];
    static MenuMailbox menu_mbx;
    menu_mbx.init(menu_page, MENU_DATA_OFS); // stub_entry = 0x0100 (page-relative)
    log_info("Menu mailbox initialised");

    // 10. Append BOOT record to EVENT_LOG (spec §6.5 boot integration).
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

    // 11. Print boot banner to log (flushed to UART/OLED in later stages).
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

    // 12. Collection install from USB (deferred to usb-host stage).
    // TODO(usb-host): wire UsbInstallScanner here once tinyusb is integrated.
    log_info("collection install: USB host not integrated (TODO(usb-host))");

    // 13–14. On hardware: launch Core 1 for the service loop, then Core 0 enters
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
