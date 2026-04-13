cmake_minimum_required(VERSION 3.13)
# Both C and CXX needed: Monocypher is a C library (monocypher.c).
project(jlpicart_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
enable_testing()

# Host tests run on the build machine (Linux amd64), not on the RP2350.
# They test firmware logic that is portable: framing, parsing, crypto,
# state machines. No Pico SDK involved.

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# All Stage 3 tests share this include path and compile-time flag.
set(FW_SRC "${CMAKE_CURRENT_LIST_DIR}/../src")
set(COMMON_DEFS JLPICART_HOST_TEST)
set(COMMON_INC "${FW_SRC}")

# Shared portable sources (no SDK, no flash, no OTP hardware).
set(SRC_DIAG        "${FW_SRC}/diag/diag.cc")
set(SRC_LOG         "${FW_SRC}/log/log.cc")
set(SRC_SHA256      "${FW_SRC}/crypto/sha256.cc")
set(SRC_POSTURE     "${FW_SRC}/spine/security_posture.cc")
set(SRC_POL_VERIFY  "${FW_SRC}/policy/policy_verify.cc")
set(SRC_POL_STORE   "${FW_SRC}/spine/policy_store.cc")
set(SRC_CAP_REG     "${FW_SRC}/spine/capability_registry.cc")
set(SRC_BOARD_DESC  "${FW_SRC}/boards/board_descriptor_jlpicart.cc")
set(SRC_DRV_TABLE   "${FW_SRC}/drivers/driver_descriptor_table.cc")

# Helper macro to reduce boilerplate.
# Tests that include SRC_DEVICE_IDENTITY automatically pick up OpenSSL linkage
# via the target_link_libraries call below.
function(add_host_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${COMMON_INC})
    target_compile_definitions(${name} PRIVATE ${COMMON_DEFS})
    # Link OpenSSL for device_identity.cc (ed25519 in host test mode).
    target_link_libraries(${name} PRIVATE OpenSSL::Crypto)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# ---------------------------------------------------------------------------
# smoke_test — validates the harness itself
# ---------------------------------------------------------------------------
add_host_test(smoke_test host/smoke_test.cc)

# ---------------------------------------------------------------------------
# Stage 3 tests
# ---------------------------------------------------------------------------

add_host_test(test_diag
    host/test_diag.cc
    ${SRC_DIAG}
)

add_host_test(test_log
    host/test_log.cc
    ${SRC_LOG}
)

add_host_test(test_otp
    host/test_otp.cc
    # FakeOtpReader is header-only — no extra sources needed.
)

add_host_test(test_security_posture
    host/test_security_posture.cc
    ${SRC_POSTURE}
)

add_host_test(test_policy
    host/test_policy.cc
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

add_host_test(test_capability_registry
    host/test_capability_registry.cc
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
)

# ---------------------------------------------------------------------------
# Stage 4 tests
# ---------------------------------------------------------------------------

set(SRC_API_WINDOW    "${FW_SRC}/msx/api/api_window.cc")
set(SRC_CORE_SERVICE  "${FW_SRC}/msx/api/services/core_service.cc")
set(SRC_IDENTITY_SVC  "${FW_SRC}/msx/api/services/identity_service.cc")
set(SRC_MENU_HOST_ABI "${FW_SRC}/msx/menu/menu_host_abi.cc")
set(SRC_PROFILE_STORE "${FW_SRC}/profiles/profile_store.cc")
set(SRC_FLASH_DEVICE  "${FW_SRC}/storage/flash_device.cc")
set(SRC_KV_STORE      "${FW_SRC}/storage/kv_store.cc")
set(SRC_SAVE_STORE    "${FW_SRC}/storage/save_store.cc")
set(SRC_STORAGE_SVC   "${FW_SRC}/msx/api/services/storage_service.cc")
set(SRC_STATS_STORE    "${FW_SRC}/stats/stats_store.cc")
set(SRC_USERSTATS_SVC  "${FW_SRC}/msx/api/services/userstats_service.cc")
set(SRC_NET_TRANSPORT  "${FW_SRC}/net/transport_esp_at.cc")
set(SRC_NET_SERVICE    "${FW_SRC}/net/network_service.cc")
# Any test that links api_window.cc must also include these (api_window.cc
# references network_service_handle and transport_esp_at).
set(SRC_NET_DEPS ${SRC_NET_TRANSPORT} ${SRC_NET_SERVICE})
# Monocypher: used by device_identity.cc for AEAD wrapping and ed25519.
set(SRC_MONOCYPHER "${FW_SRC}/crypto/monocypher/monocypher.c")

# device_identity.cc requires smk.cc and monocypher.c; bundle them so every
# consumer of SRC_DEVICE_IDENTITY automatically gets the full dependency set.
set(SRC_DEVICE_IDENTITY
    "${FW_SRC}/identity/device_identity.cc"
    "${FW_SRC}/crypto/smk.cc"
    "${FW_SRC}/crypto/monocypher/monocypher.c"
)
# SRC_SMK alone (for tests that only need HKDF / SMK, no DeviceIdentity).
set(SRC_SMK "${FW_SRC}/crypto/smk.cc")

# OpenSSL is used by device_identity.cc in host test mode for ed25519 operations.
find_package(OpenSSL REQUIRED)

add_host_test(test_api_window
    host/test_api_window.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_TRANSPORT}
    ${SRC_NET_SERVICE}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_PROFILE_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
)

# ---------------------------------------------------------------------------
# Stage 32 tests — Network service
# ---------------------------------------------------------------------------

add_host_test(test_network_service
    host/test_network_service.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_TRANSPORT}
    ${SRC_NET_SERVICE}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_PROFILE_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
)

# ---------------------------------------------------------------------------
# Stage 5 tests
# ---------------------------------------------------------------------------

add_host_test(test_menu_mailbox
    host/test_menu_mailbox.cc
    ${SRC_MENU_HOST_ABI}
)

# ---------------------------------------------------------------------------
# Stage 11 tests
# ---------------------------------------------------------------------------

add_host_test(test_menu_stub
    host/test_menu_stub.cc
    ${SRC_MENU_HOST_ABI}
)

# ---------------------------------------------------------------------------
# Stage 6 tests
# ---------------------------------------------------------------------------

set(SRC_APPEND_LOG     "${FW_SRC}/storage/append_log.cc")
set(SRC_STORAGE_HEALTH "${FW_SRC}/storage/storage_health.cc")

add_host_test(test_storage
    host/test_storage.cc
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_STORAGE_HEALTH}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 7 tests
# ---------------------------------------------------------------------------

set(SRC_MANIFEST_PARSER    "${FW_SRC}/content/manifest_parser.cc")
set(SRC_INSTALLER          "${FW_SRC}/content/installer.cc")
set(SRC_RECEIPTS           "${FW_SRC}/content/receipts.cc")
set(SRC_CONTENT_STORE      "${FW_SRC}/content/content_store.cc")
set(SRC_BUNDLE_SIG_VERIFY  "${FW_SRC}/content/bundle_sig_verify.cc")
set(SRC_RESOURCE_MODEL  "${FW_SRC}/allocator/resource_model.cc")
set(SRC_ALLOCATOR       "${FW_SRC}/allocator/allocator.cc"
                        "${FW_SRC}/allocator/hw_probe.cc")
set(SRC_ACTIVATION      "${FW_SRC}/spine/activation.cc")
set(SRC_BUS_MAP         "${FW_SRC}/bus/bus_map.cc")
set(SRC_PERIPH_MGR      "${FW_SRC}/peripherals/peripheral_manager.cc")
set(SRC_PSG             "${FW_SRC}/peripherals/psg.cc")
set(SRC_SCC             "${FW_SRC}/peripherals/scc.cc")
set(SRC_OPL3FM          "${FW_SRC}/peripherals/opl3fm.cc")
# opl4.cc references opl3fm symbols; always bundle together.
set(SRC_OPL4            "${FW_SRC}/peripherals/opl3fm.cc"
                        "${FW_SRC}/peripherals/opl4.cc")
set(SRC_SUNRISE_IDE     "${FW_SRC}/peripherals/sunrise_ide.cc")
set(SRC_MAPPERS         "${FW_SRC}/mappers/mappers.cc")
set(SRC_MAPPING_PLAN    "${FW_SRC}/bus/mapping_plan.cc")

# ---------------------------------------------------------------------------
# Stage 8 tests
# ---------------------------------------------------------------------------

add_host_test(test_allocator
    host/test_allocator.cc
    ${SRC_RESOURCE_MODEL}
    ${SRC_ALLOCATOR}
    ${SRC_ACTIVATION}
    ${SRC_BUS_MAP}
    ${SRC_PERIPH_MGR}
    ${SRC_PSG}
    ${SRC_SCC}
    ${SRC_OPL4}
    ${SRC_SUNRISE_IDE}
    ${SRC_MAPPERS}
    ${SRC_MAPPING_PLAN}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_MANIFEST_PARSER}
    ${SRC_LOG}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 9 tests
# ---------------------------------------------------------------------------

add_host_test(test_bus_mapper
    host/test_bus_mapper.cc
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MAPPING_PLAN}
    ${SRC_MANIFEST_PARSER}
    ${SRC_DIAG}
    # Note: bus.cc (Core 0 hardware loop) is intentionally excluded here.
    # It only compiles for the RP2350 target.
)

add_host_test(test_collections
    host/test_collections.cc
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Stage 15 tests
# ---------------------------------------------------------------------------

add_host_test(test_profiles
    host/test_profiles.cc
    ${SRC_PROFILE_STORE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
)

# ---------------------------------------------------------------------------
# Stage 14 tests
# ---------------------------------------------------------------------------

set(SRC_USB_HOST    "${FW_SRC}/usb/usb_host.cc")
set(SRC_USB_SCANNER "${FW_SRC}/usb/usb_install_scanner.cc")

add_host_test(test_usb_scanner
    host/test_usb_scanner.cc
    ${SRC_USB_HOST}
    ${SRC_USB_SCANNER}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Stage 16 tests
# ---------------------------------------------------------------------------

set(SRC_MENU_APP          "${FW_SRC}/menu/menu_app.cc")
set(SRC_SYSTEM_SETTINGS   "${FW_SRC}/settings/system_settings_store.cc")
set(SRC_SAVE_STORE        "${FW_SRC}/storage/save_store.cc")
set(SRC_STORAGE_SVC       "${FW_SRC}/msx/api/services/storage_service.cc")

add_host_test(test_menu_app
    host/test_menu_app.cc
    ${SRC_MENU_APP}
    ${SRC_MENU_HOST_ABI}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CONTENT_STORE}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_SYSTEM_SETTINGS}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

# ---------------------------------------------------------------------------
# Stage 17 tests
# ---------------------------------------------------------------------------

add_host_test(test_system_settings
    host/test_system_settings.cc
    ${SRC_SYSTEM_SETTINGS}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 18 tests
# ---------------------------------------------------------------------------

set(SRC_CORE_SVC_FULL
    ${SRC_CORE_SERVICE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
    ${SRC_PROFILE_STORE}
    ${SRC_MENU_HOST_ABI}
    ${SRC_MENU_APP}
    ${SRC_SYSTEM_SETTINGS}
    ${SRC_CONTENT_STORE}
    ${SRC_NET_DEPS}
)

add_host_test(test_core_service
    host/test_core_service.cc
    ${SRC_API_WINDOW}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CORE_SVC_FULL}
)

# ---------------------------------------------------------------------------
# Stage 19 tests
# ---------------------------------------------------------------------------

add_host_test(test_save_store
    host/test_save_store.cc
    ${SRC_SAVE_STORE}
    ${SRC_STORAGE_SVC}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_IDENTITY_SVC}
    ${SRC_CORE_SERVICE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

# ---------------------------------------------------------------------------
# Stage 20 tests
# ---------------------------------------------------------------------------

add_host_test(test_boot_mode
    host/test_boot_mode.cc
    ${SRC_MENU_APP}
    ${SRC_MENU_HOST_ABI}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CONTENT_STORE}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_SYSTEM_SETTINGS}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

# ---------------------------------------------------------------------------
# Stage 21 tests
# ---------------------------------------------------------------------------

add_host_test(test_stats_store
    host/test_stats_store.cc
    ${SRC_STATS_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

# ---------------------------------------------------------------------------
# Stage 22 tests
# ---------------------------------------------------------------------------

add_host_test(test_device_identity
    host/test_device_identity.cc
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_PROFILE_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)

# ---------------------------------------------------------------------------
# Stage 25 tests
# ---------------------------------------------------------------------------

add_host_test(test_smk
    host/test_smk.cc
    ${SRC_DEVICE_IDENTITY}
    ${SRC_SHA256}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 26 tests
# ---------------------------------------------------------------------------

add_host_test(test_policy_enforcement
    host/test_policy_enforcement.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_IDENTITY_SVC}
    ${SRC_STORAGE_SVC}
    ${SRC_SAVE_STORE}
    ${SRC_USERSTATS_SVC}
    ${SRC_STATS_STORE}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_USB_HOST}
    ${SRC_USB_SCANNER}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_BUNDLE_SIG_VERIFY}
    # Monocypher already pulled in by SRC_DEVICE_IDENTITY above.
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_FLASH_DEVICE}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_PROFILE_STORE}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Stage 27 tests
# ---------------------------------------------------------------------------

add_host_test(test_psg
    host/test_psg.cc
    ${SRC_PSG}
)

# ---------------------------------------------------------------------------
# Stage 29 tests
# ---------------------------------------------------------------------------

add_host_test(test_opl4
    host/test_opl4.cc
    ${SRC_OPL4}
    ${SRC_DIAG}
)

add_host_test(test_sunrise_ide
    host/test_sunrise_ide.cc
    ${SRC_SUNRISE_IDE}
    ${SRC_DIAG}
)

add_host_test(test_scc
    host/test_scc.cc
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MAPPING_PLAN}
    ${SRC_MANIFEST_PARSER}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 28 tests
# ---------------------------------------------------------------------------

add_host_test(test_rom_loading
    host/test_rom_loading.cc
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_CONTENT_STORE}
    ${SRC_MAPPING_PLAN}
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Stage 10 tests
# ---------------------------------------------------------------------------

add_host_test(test_content_store
    host/test_content_store.cc
    ${SRC_CONTENT_STORE}
    ${SRC_MAPPING_PLAN}
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Stage 31 tests — bundle signature verification
# ---------------------------------------------------------------------------

add_host_test(test_bundle_sig
    host/test_bundle_sig.cc
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_KV_STORE}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
)
