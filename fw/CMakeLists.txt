cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Self-contained SDK — expects .pico-sdk/ inside fw/ (airgapped, no network)
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/.pico-sdk/sdk/${sdkVersion}")
message(STATUS "Using Pico SDK at ${PICO_SDK_PATH}")
set(PICO_BOARD jlpicart_board CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/src/boards)

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS "${PICO_SDK_PATH}/pico_sdk_init.cmake")
    message(FATAL_ERROR "Pico SDK not found at '${PICO_SDK_PATH}' — populate .pico-sdk/ first")
endif()
set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
include("${PICO_SDK_PATH}/pico_sdk_init.cmake")

project(jlpicart C CXX ASM)

pico_sdk_init()

# Build ID from git — injected as a compile definition.
execute_process(
    COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" describe --always --dirty --tags
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT GIT_DESCRIBE)
    set(GIT_DESCRIBE "unknown")
endif()
message(STATUS "Build ID: ${GIT_DESCRIBE}")

add_executable(jlpicart
    src/main.cc

    # Diagnostics and logging
    src/diag/diag.cc
    src/log/log.cc

    # Security
    src/security/otp_reader_hw.cc
    src/spine/security_posture.cc

    # Crypto
    src/crypto/sha256.cc
    src/crypto/smk.cc

    # Policy
    src/policy/policy_verify.cc
    src/spine/policy_store.cc

    # Capability descriptors and registry
    src/boards/board_descriptor_jlpicart.cc
    src/drivers/driver_descriptor_table.cc
    src/spine/capability_registry.cc

    # API window (Stage 4) + Identity service (Stage 15)
    src/msx/api/api_window.cc
    src/msx/api/services/core_service.cc
    src/msx/api/services/identity_service.cc

    # Menu Host ABI (Stage 5)
    src/msx/menu/menu_host_abi.cc

    # Menu Application (Stage 16)
    src/menu/menu_app.cc

    # System Settings (Stage 17)
    src/settings/system_settings_store.cc

    # Save store and Storage service (Stage 19)
    src/storage/save_store.cc
    src/msx/api/services/storage_service.cc

    # Stats store and UserStats service (Stage 21)
    src/stats/stats_store.cc
    src/msx/api/services/userstats_service.cc

    # ESP32 AT transport and Network service (Stage 32)
    src/net/transport_esp_at.cc
    src/net/network_service.cc

    # Device Identity Key and GET_DEVICE_ID (Stage 22/24)
    src/identity/device_identity.cc

    # Monocypher — vendored ed25519 / Argon2 (Stage 24)
    src/crypto/monocypher/monocypher.c

    # Storage substrate (Stage 6)
    src/storage/flash_device.cc
    src/storage/append_log.cc

    # Allocator (Stage 8 + gap #2 HW probes)
    src/allocator/allocator.cc
    src/allocator/hw_probe.cc
    src/allocator/resource_model.cc

    # Activation (Stage 8)
    src/spine/activation.cc

    # Mappers (Stage 9)
    src/mappers/mappers.cc

    # Bus loop (Stage 9)
    src/bus/bus.cc
    src/bus/bus_state.cc
    src/bus/mapping_plan.cc

    # Platform HAL (RP2350 implementation)
    src/platform/platform_rp2350.cc

    # Collections (Stage 7 + Stage 31 signature verification)
    src/content/manifest_parser.cc
    src/content/installer.cc
    src/content/receipts.cc
    src/content/content_store.cc
    src/content/bundle_sig_verify.cc

    # Bus mapping (Stage 13)
    src/bus/bus_map.cc

    # Peripheral bus wiring (Stage 9 + Stage 13)
    src/peripherals/peripheral_manager.cc

    # PSG audio emulation (Stage 27)
    src/peripherals/psg.cc

    # SCC audio emulation (Stage 29)
    src/peripherals/scc.cc

    # OPL4 (YMF278B) audio emulation (Stage 30)
    src/peripherals/opl3fm.cc
    src/peripherals/opl4.cc

    # Sunrise ATA-IDE emulation (Stage 34)
    src/peripherals/sunrise_ide.cc

    # User Profiles (Stage 15)
    src/profiles/profile_store.cc

    # USB host (Stage 14)
    src/usb/usb_host.cc
    src/usb/usb_install_reader.cc
    src/usb/usb_install_scanner.cc
    src/usb/diskio_tuh.cc

    # USB device / config mode
    src/usb/usb_device.cc

    # FAT volume on internal flash
    src/storage/diskio_flash.cc
    src/storage/diskio.cc
    src/storage/fat_volume.cc

    # FatFs (Stage 14) — from bundled tinyusb lib
    ext/tinyusb/lib/fatfs/source/ff.c
    ext/tinyusb/lib/fatfs/source/ffsystem.c
    ext/tinyusb/lib/fatfs/source/ffunicode.c
)

pico_set_program_name(jlpicart "jlpicart")
pico_set_program_version(jlpicart "0.1")

pico_enable_stdio_uart(jlpicart 0)
pico_enable_stdio_usb(jlpicart 0)

target_include_directories(jlpicart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/lib/fatfs/source
)

# tinyusb config target: exposes tusb_config.h via fw/src/usb/
add_library(tinyusb_config INTERFACE)
target_include_directories(tinyusb_config INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/usb
)

# tinyusb host + device + MSC + rp2040 HCD/DCD sources (firmware only, not host tests).
# Both stacks are compiled in; the application selects one at runtime via
# Platform::msx_clock_present() — calling tuh_init() or tud_init() as appropriate.
target_sources(jlpicart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/tusb.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/common/tusb_fifo.c
    # Host stack
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/host/usbh.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/host/hub.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/class/msc/msc_host.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/portable/raspberrypi/rp2040/hcd_rp2040.c
    # Device stack
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/device/usbd.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/device/usbd_control.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/class/msc/msc_device.c
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/portable/raspberrypi/rp2040/dcd_rp2040.c
    # Shared RP2040 USB core (used by both HCD and DCD)
    ${CMAKE_CURRENT_LIST_DIR}/ext/tinyusb/src/portable/raspberrypi/rp2040/rp2040_usb.c
)
target_link_libraries(jlpicart tinyusb_config)

target_compile_definitions(jlpicart PRIVATE
    FW_BUILD_ID="${GIT_DESCRIBE}"
)

target_link_libraries(jlpicart
    pico_stdlib
    pico_multicore
    hardware_flash
    hardware_i2c
    hardware_pwm
    pico_unique_id
    pico_rand
)

set_target_properties(jlpicart PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/src/boards/memmap_jlpicart.ld
)

pico_add_extra_outputs(jlpicart)

target_compile_options(jlpicart PRIVATE
    -Wall -Wextra -Wpedantic -Wnull-dereference
    -Wformat=2 -Winit-self -Wpacked -Wpointer-arith
    -Wmissing-declarations -Wmissing-noreturn
    -Wredundant-decls -Wsign-compare -Wswitch-enum
    -Wundef -Wunreachable-code -Wwrite-strings
)

# Guard: old_src must never appear as a source file.
get_target_property(_srcs jlpicart SOURCES)
foreach(_src ${_srcs})
    if(_src MATCHES "old_src")
        message(FATAL_ERROR "old_src path found in target sources: ${_src}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Stage 11: Z80 menu stub build target
#
# Compiles fw/src/msx/menu/stub/ with SDCC and regenerates menu_stub_bin.h.
# Requires SDCC: run  bash fw/ext/get_sdcc.sh  first.
#
# Usage:  cmake --build <build_dir> --target build_menu_stub
# ---------------------------------------------------------------------------

set(STUB_DIR    "${CMAKE_CURRENT_LIST_DIR}/src/msx/menu/stub")
set(STUB_BIN    "${STUB_DIR}/stub.bin")
set(STUB_BIN_H  "${CMAKE_CURRENT_LIST_DIR}/src/msx/menu/menu_stub_bin.h")
set(SDCC_BIN    "${CMAKE_CURRENT_LIST_DIR}/ext/sdcc/bin/sdcc")

if(EXISTS "${SDCC_BIN}")
    add_custom_command(
        OUTPUT  "${STUB_BIN}"
        COMMAND make -C "${STUB_DIR}" SDCC="${SDCC_BIN}" stub.bin
        DEPENDS "${STUB_DIR}/stub.c"
                "${STUB_DIR}/crt0.s"
                "${STUB_DIR}/stub_bios.h"
        COMMENT "Building Z80 menu stub with SDCC"
    )
    add_custom_command(
        OUTPUT  "${STUB_BIN_H}"
        COMMAND bash -c
            "{ printf '#pragma once\\n#include <cstdint>\\n// clang-format off\\n' ; \
               xxd -i '${STUB_BIN}' | sed \
                 -e 's/unsigned char stub_bin\\[\\]/static const uint8_t kMenuStubBin[]/' \
                 -e 's/unsigned int stub_bin_len/static const uint16_t kMenuStubBin_SIZE/' \
                 -e 's/= \\([0-9]*\\);/= \\1u;/' ; \
               printf '// clang-format on\\n' ; \
             } > '${STUB_BIN_H}'"
        DEPENDS "${STUB_BIN}"
        COMMENT "Generating menu_stub_bin.h from stub.bin"
    )
    add_custom_target(build_menu_stub
        DEPENDS "${STUB_BIN_H}"
        COMMENT "Z80 menu stub built and embedded"
    )
    message(STATUS "SDCC found — build_menu_stub target available")
else()
    file(WRITE "${STUB_BIN_H}"
        "#pragma once\n"
        "#include <cstdint>\n"
        "#pragma message(\"SDCC not available — kMenuStubBin is an empty placeholder\")\n"
        "static const uint8_t kMenuStubBin[] = { 0x00u };\n"
        "static const uint16_t kMenuStubBin_SIZE = 0u;\n"
    )
    add_custom_target(build_menu_stub
        COMMAND ${CMAKE_COMMAND} -E echo
            "SDCC not found at ${SDCC_BIN} — run: bash fw/ext/get_sdcc.sh"
        COMMENT "SDCC not installed; stub will be placeholder"
    )
    message(STATUS "SDCC not found at ${SDCC_BIN} — stub is placeholder. Run: bash fw/ext/get_sdcc.sh")
endif()
