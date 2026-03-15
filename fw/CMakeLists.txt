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
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/sdk)

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

    # Policy
    src/crypto/sha256.cc
    src/policy/policy_verify.cc
    src/spine/policy_store.cc

    # Capability descriptors and registry
    src/boards/board_descriptor_jlpicart.cc
    src/drivers/driver_descriptor_table.cc
    src/spine/capability_registry.cc

    # API window (Stage 4)
    src/msx/api/api_window.cc
    src/msx/api/services/core_service.cc

    # Menu Host ABI (Stage 5)
    src/msx/menu/menu_host_abi.cc

    # Storage substrate (Stage 6)
    src/storage/flash_device.cc
    src/storage/kv_store.cc
    src/storage/append_log.cc
    src/storage/storage_health.cc

    # Collections (Stage 7)
    src/content/manifest_parser.cc
    src/content/installer.cc
    src/content/receipts.cc
)

pico_set_program_name(jlpicart "jlpicart")
pico_set_program_version(jlpicart "0.1")

pico_enable_stdio_uart(jlpicart 0)
pico_enable_stdio_usb(jlpicart 0)

target_include_directories(jlpicart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_compile_definitions(jlpicart PRIVATE
    FW_BUILD_ID="${GIT_DESCRIBE}"
)

target_link_libraries(jlpicart
    pico_stdlib
    hardware_flash
)

set_target_properties(jlpicart PROPERTIES
    PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/sdk/memmap_jlpicart.ld
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
# Requires SDCC: run  bash fw/tools/get_sdcc.sh  first.
#
# Usage:  cmake --build <build_dir> --target build_menu_stub
# ---------------------------------------------------------------------------

set(STUB_DIR    "${CMAKE_CURRENT_LIST_DIR}/src/msx/menu/stub")
set(STUB_BIN    "${STUB_DIR}/stub.bin")
set(STUB_BIN_H  "${CMAKE_CURRENT_LIST_DIR}/src/msx/menu/menu_stub_bin.h")
set(SDCC_BIN    "${CMAKE_CURRENT_LIST_DIR}/tools/sdcc/bin/sdcc")

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
        COMMAND ${CMAKE_COMMAND} -E env
                python3 "${CMAKE_CURRENT_LIST_DIR}/tools/bin_to_c_array.py"
                        "${STUB_BIN}" kMenuStubBin > "${STUB_BIN_H}"
        DEPENDS "${STUB_BIN}"
        COMMENT "Generating menu_stub_bin.h from stub.bin"
    )
    add_custom_target(build_menu_stub
        DEPENDS "${STUB_BIN_H}"
        COMMENT "Z80 menu stub built and embedded"
    )
    message(STATUS "SDCC found — build_menu_stub target available")
else()
    add_custom_target(build_menu_stub
        COMMAND ${CMAKE_COMMAND} -E echo
            "SDCC not found at ${SDCC_BIN} — run: bash fw/tools/get_sdcc.sh"
        COMMENT "SDCC not installed; stub will be placeholder"
    )
    message(STATUS "SDCC not found at ${SDCC_BIN} — stub is placeholder. Run: bash fw/tools/get_sdcc.sh")
endif()
