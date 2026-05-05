// JLPiCart firmware — main boot sequence.
//
// Boot order:
//   1.  Log init
//   2.  FAT volume mount on internal flash
//   3.  FAT-backed stores: profiles, settings, save, stats
//   4.  Security posture (OTP — read once, never again)
//   5.  USB config mode check (no MSX clock + no secure boot → expose FAT via USB)
//   6.  Device identity key (derived from OTP; must follow step 4)
//   7.  Policy store (flash read + HMAC verify)
//   8.  Capability registry (declared → allowed)
//   9.  Activation preflight: Allocator → LaunchPlan → PeripheralManager
//  10.  Mapping plan: ContentStore → mapping_plan_from_payload_record → apply_mapping()
//  11.  API window init + bind stores
//  12.  Network transport (ESP32 AT)
//  13.  Menu mailbox + MenuApp state machine
//  14.  Bus wiring: map_menu_page() + map_api_window()
//  15.  Boot banner
//  16.  USB host init
//  17.  Core 1 service loop (API window, menu, USB poll, USB install scan on mount)
//       Core 0 enters BUS::start() — never returns
//
// USB collection install happens in the Core 1 service loop (step 17) on the
// first MSC mount event, which covers both boot-time and hot-plug cases.
// Starting the bus (step 17) must not be delayed by USB enumeration.

#include "diag/diag.h"
#include "diag/log.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "spine/activation.h"
#include "allocator/allocator.h"
#include "allocator/resource_model.h"
#include "peripherals/peripheral_manager.h"
#include "spine/driver_descriptor.h"
#include "bus/mapping_plan.h"
#include "content/content_store.h"
#include "boards/board_descriptor.h"
#include "msx/api/api_window.h"
#include "msx/menu/menu_host_abi.h"
#include "menu/menu_app.h"
#include "profiles/profile_store.h"
#include "settings/system_settings_store.h"
#include "storage/save_store.h"
#include "stats/stats_store.h"
#include "identity/device_identity.h"
#include "crypto/smk.h"
#include "usb/usb_host.h"
#include "usb/usb_device.h"
#include "net/transport_esp_at.h"
#include "usb/usb_install_scanner.h"
#include "storage/flash_layout.h"
#include "storage/fat_volume.h"
#include "storage/store.h"
#include "platform/platform.h"
#include <cstdio>
#include <cstring>

#ifndef FW_BUILD_ID
#define FW_BUILD_ID "dev"
#endif

int main() {
    // 1. Init diagnostics and logging.
    log_init();
    log_info("JLPiCart boot start build=" FW_BUILD_ID);

    // 2. Mount FAT volume on internal flash.
    static FatVolume fat_vol;
    fat_vol.mount();

    // 3. FAT-backed stores.
    static Store store;
    {
        DiagStatus s = store.init();
        if (s.ok()) log_info("event store ready");
        else        log_warn("event store unavailable — sync disabled");
    }

    static ProfileStore profile_store;
    {
        profile_store.bind_store(store);
        DiagStatus s = profile_store.init();
        if (!s.ok()) log_warn("profiles unavailable");
        else         log_info("profiles ready");
    }

    static SystemSettingsStore settings_store;
    settings_store.init();
    log_info("system settings loaded");

    static SaveStore save_store;
    save_store.bind_store(store);
    save_store.init(profile_store);

    static StatsStore stats_store;
    stats_store.init(profile_store);

    // 4. Read security posture from OTP (exactly once).
    const OtpReader& otp = get_hardware_otp_reader();
    SecurityPosture posture = SecurityPosture::read(otp);
    {
        char buf[128];
        posture.describe(buf, sizeof(buf));
        log_info(buf);
    }

    // 5. Config mode: if the MSX clock is absent and secure boot is not
    //    enforced, expose the FAT volume as a USB mass-storage device so the
    //    user can install collections without special tools.  Never returns.
    if (!Platform::msx_clock_present() && !posture.secure_boot_enabled) {
        log_info("no MSX clock detected — entering USB config mode");
        fat_vol.unmount();
        UsbDevice::run();
    }

    // 6. Device identity key (derived from OTP device secret via HKDF-SHA256).
    //    On unprovisioned units smk_derive(nullptr, ...) uses all-zeros IKM.
    static DeviceIdentity device_identity;
    {
        uint8_t smk[SMK_LEN] = {};
        uint8_t dik_wrap[SMK_NS_KEY_LEN] = {};

        if (posture.otp_device_secret_present) {
            uint8_t otp_sec[otp_offsets::DEVICE_SECRET_LEN] = {};
            otp.read_bytes(otp_offsets::DEVICE_SECRET, otp_sec, sizeof(otp_sec));
            smk_derive(otp_sec, smk);
            volatile uint8_t* vp = otp_sec;
            for (size_t i = 0; i < sizeof(otp_sec); ++i) vp[i] = 0u;
        } else {
            smk_derive(nullptr, smk);
        }
        smk_derive_ns_key(smk, "dik.priv", dik_wrap);
        volatile uint8_t* vs = smk;
        for (size_t i = 0; i < sizeof(smk); ++i) vs[i] = 0u;

        DiagStatus s = device_identity.init_or_load(dik_wrap);

        volatile uint8_t* vd = dik_wrap;
        for (size_t i = 0; i < sizeof(dik_wrap); ++i) vd[i] = 0u;

        if (s.ok()) {
            log_info(device_identity.is_provisioned()
                     ? "DIK ready (provisioned)"
                     : "DIK ready (seed-only, unprovisioned)");
        } else {
            log_warn("DIK init failed — GET_DEVICE_ID unavailable");
        }
    }

    // 7. Load and verify policy.
    PolicyStore policy_store;
    DiagStatus ps = policy_store.load(posture);
    if (!ps.ok()) {
        char buf[64];
        snprintf(buf, sizeof(buf), "policy load failed: %s (safe defaults applied)",
                 diag_code_to_string(ps.code));
        log_warn(buf);
    }

    // 8. Build capability registry (declared → allowed).
    CapabilityRegistry registry;
    registry.init(BoardDescriptor::for_current_board(),
                  kDriverDescriptors, kDriverDescriptorCount,
                  policy_store.info());

    // 9. Activation preflight: compute Launch Plan and activate capabilities.
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

    // 10. Mapping plan: look up the default payload and wire the bus.
    //     Falls back to empty plan (standby/menu mode) if no active collection
    //     or the payload has no ROM data yet.
    static PeripheralManager map_mgr;
    {
        ContentStore content_store;
        MappingPlan mapping_plan = {};

        if (content_store.has_active_collection()) {
            PayloadRecord pr = {};
            DiagStatus s = content_store.load_default_payload(pr);
            if (!s.ok()) {
                log_info("mapping plan: no default payload — standby");
            } else if (pr.data_size == 0) {
                log_info("mapping plan: payload ROM not loaded — standby");
            } else {
                mapping_plan = mapping_plan_from_payload_record(pr);
                log_info("mapping plan: active payload found");
            }
        } else {
            log_info("mapping plan: no active collection — standby");
        }

        map_mgr.apply_mapping(mapping_plan);
    }

    // 11. API window.
    static ApiWindow api_win;
    api_win.init(posture, policy_store, registry);
    if (profile_store.initialized())    api_win.bind_profile_store(profile_store);
    if (save_store.initialized())       api_win.bind_save_store(save_store);
    if (stats_store.initialized())      api_win.bind_stats_store(stats_store);
    if (device_identity.initialized())  api_win.bind_device_identity(device_identity);

    // 12. Network transport: init ESP32 UART and join WiFi if configured.
    static TransportEspAt net_transport;
    {
        const SystemSettings& ss = settings_store.get();
        if (ss.network_enabled) {
            net_transport.init();
            if (ss.wifi_ssid[0] != '\0') {
                log_info("WiFi: issuing AT+CWJAP...");
                net_transport.connect(ss.wifi_ssid, ss.wifi_pass);
            }
            api_win.bind_network_transport(net_transport);
            log_info("network transport bound");
        } else {
            log_info("network disabled by system settings");
        }
    }
    log_info("API window initialised");

    // 13. Menu mailbox + MenuApp state machine.
    static uint8_t menu_page[MENU_PAGE_SIZE];
    static MenuMailbox menu_mbx;
    menu_mbx.init(menu_page, MENU_DATA_OFS);

    static MenuApp menu_app;
    menu_app.init(menu_mbx, profile_store);
    menu_app.bind_settings_store(settings_store);

    api_win.set_reset_menu_fn([]() { menu_app.request_reset_to_menu(); });
    menu_app.bind_api_window(api_win);

    // Launch callback: remap the bus when the user selects a payload in the menu.
    menu_app.set_launch_fn(nullptr, [](void*, const char* payload_id) {
        ContentStore cs;
        if (!cs.has_active_collection()) return;
        PayloadRecord pr = {};
        if (!cs.load_payload(payload_id, pr).ok()) return;
        map_mgr.apply_mapping(mapping_plan_from_payload_record(pr));
    });
    log_info("menu mailbox initialised");

    // 14. Wire bus mappings.
    map_mgr.map_menu_page(menu_page);
    map_mgr.map_api_window(api_win.buf());

    // 15. Boot banner.
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

    // 17. USB host init.
    //     Enumeration is asynchronous — collection install is handled in the
    //     Core 1 service loop on the first MSC mount event (step 18).
    static UsbHost usb_host;
    usb_host.init();
    static UsbInstallScanner usb_scanner(usb_host);

    // 18. Service loop on Core 1; Core 0 enters BUS::start().
    //     All state is static so the captureless lambda can reach them.
    static ApiWindow*           g_api_win    = &api_win;
    static MenuApp*             g_menu_app   = &menu_app;
    static UsbHost*             g_usb_host   = &usb_host;
    static PeripheralManager*   g_map_mgr    = &map_mgr;
    static UsbInstallScanner*   g_usb_scan   = &usb_scanner;
    static const PolicyStore*   g_policy     = &policy_store;

    Platform::start([]() {
        g_api_win->service_once();
        g_menu_app->tick();
        g_usb_host->poll();
        g_map_mgr->service_all();

        // USB install scan: run once on each new MSC mount (covers boot-time
        // and hot-plug).  scan() returns quickly when nothing is mounted.
        static bool s_msc_prev = false;
        const bool  msc_now    = g_usb_host->is_msc_mounted();
        if (msc_now && !s_msc_prev)
            g_usb_scan->scan(*g_policy);
        s_msc_prev = msc_now;
    });
}
