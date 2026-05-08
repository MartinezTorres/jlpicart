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
set(FW_EXT "${CMAKE_CURRENT_LIST_DIR}/../ext")
set(COMMON_DEFS JLPICART_HOST_TEST)
set(COMMON_INC
    "${FW_SRC}"
    "${FW_EXT}/tinyusb/lib/fatfs/source"
)

# Shared portable sources (no SDK, no flash, no OTP hardware).
set(SRC_DIAG        "${FW_SRC}/diag/diag.cc")
set(SRC_LOG         "${FW_SRC}/diag/log.cc")
set(SRC_SHA256      "${FW_SRC}/crypto/sha256.cc")
set(SRC_POSTURE     "${FW_SRC}/spine/security_posture.cc")
set(SRC_POLICY  "${FW_SRC}/spine/policy.cc")
set(SRC_CAP_REG     "${FW_SRC}/spine/capability_registry.cc")
set(SRC_BOARD_DESC  "${FW_SRC}/platform/board_descriptor_jlpicart.cc")
set(SRC_DRV_TABLE   "${FW_SRC}/spine/driver_descriptor_table.cc")

# ---------------------------------------------------------------------------
# FAT infrastructure (used by all FAT-backed stores in host tests)
# ---------------------------------------------------------------------------

# FatFs sources from the bundled tinyusb library.
set(SRC_FATFS
    "${FW_EXT}/tinyusb/lib/fatfs/source/ff.c"
    "${FW_EXT}/tinyusb/lib/fatfs/source/ffsystem.c"
    "${FW_EXT}/tinyusb/lib/fatfs/source/ffunicode.c"
)

# RAM disk (pdrv=1) for host tests — replaces diskio_flash.cc.
set(SRC_DISKIO_RAM "${CMAKE_CURRENT_LIST_DIR}/host/diskio_ram.cc")

# FatVolume lifecycle (mount/unmount/format).
set(SRC_FAT_VOLUME "${FW_SRC}/filesystem/fat_volume.cc")

# Aggregate: every test that uses any FAT-backed store needs all four.
# fat_volume.cc calls log_info/log_warn, so SRC_LOG is always required.
set(SRC_FAT_DEPS
    ${SRC_FAT_VOLUME}
    ${SRC_DISKIO_RAM}
    ${SRC_FATFS}
    ${SRC_LOG}
)

# ---------------------------------------------------------------------------
# Store sources (all FAT-backed now; no KvStore dependency)
# ---------------------------------------------------------------------------

set(SRC_USER_DATA_STORE "${FW_SRC}/store/user_data_store.cc")
set(SRC_CONTENT_STORE  "${FW_SRC}/content/content_store.cc")

# FlashDevice: used by test_storage standalone tests.
set(SRC_FLASH_DEVICE  "${FW_SRC}/filesystem/flash_device.cc")

# ---------------------------------------------------------------------------
# API / service sources
# ---------------------------------------------------------------------------

set(SRC_API_WINDOW    "${FW_SRC}/msx/api/api_window.cc")
set(SRC_CORE_SERVICE  "${FW_SRC}/msx/api/services/core_service.cc")
set(SRC_MENU_HOST_ABI "${FW_SRC}/msx/menu/menu_host_abi.cc")
set(SRC_STORAGE_SVC   "${FW_SRC}/msx/api/services/storage_service.cc")
set(SRC_NET_TRANSPORT  "${FW_SRC}/net/transport_esp_at.cc")
set(SRC_NET_SERVICE    "${FW_SRC}/net/network_service.cc")
# Any test that links api_window.cc must also include these.
set(SRC_NET_DEPS ${SRC_NET_TRANSPORT} ${SRC_NET_SERVICE})

# Monocypher: used by device_identity.cc for AEAD wrapping and ed25519.
set(SRC_MONOCYPHER "${FW_SRC}/crypto/monocypher/monocypher.c")

# device_identity.cc requires smk.cc and monocypher.c; bundle them.
set(SRC_DEVICE_IDENTITY
    "${FW_SRC}/spine/device_identity.cc"
    "${FW_SRC}/crypto/smk.cc"
    "${FW_SRC}/crypto/monocypher/monocypher.c"
)
# SRC_SMK alone (for tests that only need HKDF / SMK, no DeviceIdentity).
set(SRC_SMK "${FW_SRC}/crypto/smk.cc")

# OpenSSL is used by device_identity.cc in host test mode for ed25519 operations.
find_package(OpenSSL REQUIRED)

# Helper macro to reduce boilerplate.
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
    ${SRC_POLICY}
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

add_host_test(test_api_window
    host/test_api_window.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_USER_DATA_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 32 tests — Network service
# ---------------------------------------------------------------------------

add_host_test(test_network_service
    host/test_network_service.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_USER_DATA_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
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
# Stage 6 tests — storage substrate
# ---------------------------------------------------------------------------

# test_storage covers FlashDevice (standalone).
add_host_test(test_storage
    host/test_storage.cc
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 7 tests
# ---------------------------------------------------------------------------

set(SRC_MANIFEST_PARSER    "${FW_SRC}/content/manifest_parser.cc")
set(SRC_INSTALLER          "${FW_SRC}/content/installer.cc")
set(SRC_BUNDLE_SIG_VERIFY  "${FW_SRC}/content/bundle_sig_verify.cc")
set(SRC_RESOURCE_MODEL  "${FW_SRC}/spine/resource_model.cc")
set(SRC_ALLOCATOR       "${FW_SRC}/spine/allocator.cc"
                        "${FW_SRC}/spine/hw_probe.cc")
set(SRC_ACTIVATION      "${FW_SRC}/spine/activation.cc")
set(SRC_BUS_MAP         "${FW_SRC}/bus/bus_map.cc"
                        "${FW_SRC}/bus/bus_state.cc")
set(SRC_PLATFORM_HOST   "${FW_SRC}/platform/platform_host.cc")
set(SRC_PERIPH_MGR      "${FW_SRC}/peripherals/peripheral_manager.cc")
set(SRC_PSG             "${FW_SRC}/peripherals/psg.cc")
set(SRC_SCC             "${FW_SRC}/peripherals/scc.cc")
set(SRC_OPL3FM          "${FW_SRC}/peripherals/opl3fm.cc")
# opl4.cc references opl3fm symbols; always bundle together.
set(SRC_OPL4            "${FW_SRC}/peripherals/opl3fm.cc"
                        "${FW_SRC}/peripherals/opl4.cc")
set(SRC_SUNRISE_IDE     "${FW_SRC}/peripherals/sunrise_ide.cc")
set(SRC_PERIPH_DESC     "${FW_SRC}/peripherals/peripheral_descriptor.cc")
set(SRC_MAPPERS         "${FW_SRC}/bus/mappers.cc"
                        "${FW_SRC}/peripherals/peripheral_descriptor.cc")
set(SRC_DEVICE_CHECK    "${FW_SRC}/spine/device_check.cc")

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
    ${SRC_PLATFORM_HOST}
    ${SRC_CONTENT_STORE}
    ${SRC_PSG}
    ${SRC_SCC}
    ${SRC_OPL4}
    ${SRC_SUNRISE_IDE}
    ${SRC_MAPPERS}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_MANIFEST_PARSER}
    ${SRC_LOG}
    ${SRC_DIAG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 9 tests
# ---------------------------------------------------------------------------

add_host_test(test_bus_mapper
    host/test_bus_mapper.cc
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_DIAG}
    # Note: bus.cc (Core 0 hardware loop) is intentionally excluded here.
    # It only compiles for the RP2350 target.
)

add_host_test(test_collections
    host/test_collections.cc
    ${SRC_MANIFEST_PARSER}
    ${SRC_PERIPH_DESC}
    ${SRC_INSTALLER}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_CONTENT_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_LOG}
    ${SRC_FAT_DEPS}
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
    ${SRC_PERIPH_DESC}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_CONTENT_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_LOG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 16 tests
# ---------------------------------------------------------------------------

set(SRC_MENU_APP          "${FW_SRC}/msx/menu/menu_app.cc")

add_host_test(test_menu_app
    host/test_menu_app.cc
    ${SRC_MENU_APP}
    ${SRC_MENU_HOST_ABI}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CONTENT_STORE}
    ${SRC_USER_DATA_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 17 tests
# ---------------------------------------------------------------------------

add_host_test(test_system_settings
    host/test_system_settings.cc
    ${SRC_USER_DATA_STORE}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_SHA256}
    ${SRC_DIAG}
    ${SRC_FAT_DEPS}
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
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_USER_DATA_STORE}
    ${SRC_MENU_HOST_ABI}
    ${SRC_MENU_APP}
    ${SRC_CONTENT_STORE}
    ${SRC_NET_DEPS}
)

add_host_test(test_core_service
    host/test_core_service.cc
    ${SRC_API_WINDOW}
    ${SRC_STORAGE_SVC}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CORE_SVC_FULL}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 19 tests
# ---------------------------------------------------------------------------

add_host_test(test_save_store
    host/test_save_store.cc
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_USER_DATA_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
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
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_CONTENT_STORE}
    ${SRC_USER_DATA_STORE}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 22 tests
# ---------------------------------------------------------------------------

add_host_test(test_device_identity
    host/test_device_identity.cc
    ${SRC_DEVICE_IDENTITY}
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_USER_DATA_STORE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 25 tests
# ---------------------------------------------------------------------------

add_host_test(test_smk
    host/test_smk.cc
    ${SRC_DEVICE_IDENTITY}
    ${SRC_SHA256}
    ${SRC_DIAG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 26 tests
# ---------------------------------------------------------------------------

add_host_test(test_policy_enforcement
    host/test_policy_enforcement.cc
    ${SRC_API_WINDOW}
    ${SRC_CORE_SERVICE}
    ${SRC_STORAGE_SVC}
    ${SRC_NET_DEPS}
    ${SRC_DEVICE_IDENTITY}
    ${SRC_USB_HOST}
    ${SRC_USB_SCANNER}
    ${SRC_PERIPH_DESC}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_BUNDLE_SIG_VERIFY}
    # Monocypher already pulled in by SRC_DEVICE_IDENTITY above.
    ${SRC_CAP_REG}
    ${SRC_BOARD_DESC}
    ${SRC_DRV_TABLE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_CONTENT_STORE}
    ${SRC_USER_DATA_STORE}
    ${SRC_LOG}
    ${SRC_FAT_DEPS}
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
    ${SRC_MANIFEST_PARSER}
    ${SRC_DIAG}
)

# ---------------------------------------------------------------------------
# Stage 28 tests
# ---------------------------------------------------------------------------

add_host_test(test_rom_loading
    host/test_rom_loading.cc
    ${SRC_INSTALLER}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_CONTENT_STORE}
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_LOG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 10 tests
# ---------------------------------------------------------------------------

add_host_test(test_content_store
    host/test_content_store.cc
    ${SRC_CONTENT_STORE}
    ${SRC_SCC}
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POLICY}
    ${SRC_POSTURE}
    ${SRC_LOG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 31 tests — bundle signature verification
# ---------------------------------------------------------------------------

add_host_test(test_bundle_sig
    host/test_bundle_sig.cc
    ${SRC_BUNDLE_SIG_VERIFY}
    ${SRC_MONOCYPHER}
    ${SRC_DIAG}
    ${SRC_FAT_DEPS}
)

# ---------------------------------------------------------------------------
# Stage 35 tests — collection device model
# ---------------------------------------------------------------------------

add_host_test(test_device_check
    host/test_device_check.cc
    ${SRC_PERIPH_DESC}
    ${SRC_DEVICE_CHECK}
)
