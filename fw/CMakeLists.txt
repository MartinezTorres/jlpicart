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

add_executable(jlpicart
    src/main.cc
)

pico_set_program_name(jlpicart "jlpicart")
pico_set_program_version(jlpicart "0.1")

pico_enable_stdio_uart(jlpicart 0)
pico_enable_stdio_usb(jlpicart 0)

target_include_directories(jlpicart PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(jlpicart
    pico_stdlib
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

# Guard: old_src must never appear as a source file or include path.
# This is enforced here and must also be checked in CI (Stage 2) via
# compile_commands.json: grep -r old_src build/compile_commands.json must be empty.
get_target_property(_srcs jlpicart SOURCES)
foreach(_src ${_srcs})
    if(_src MATCHES "old_src")
        message(FATAL_ERROR "old_src path found in target sources: ${_src}")
    endif()
endforeach()
