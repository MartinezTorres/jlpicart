cmake_minimum_required(VERSION 3.13)
project(jlpicart_tests CXX)

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
function(add_host_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE ${COMMON_INC})
    target_compile_definitions(${name} PRIVATE ${COMMON_DEFS})
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
set(SRC_MENU_HOST_ABI "${FW_SRC}/msx/menu/menu_host_abi.cc")

add_host_test(test_api_window
    host/test_api_window.cc
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

set(SRC_FLASH_DEVICE   "${FW_SRC}/storage/flash_device.cc")
set(SRC_KV_STORE       "${FW_SRC}/storage/kv_store.cc")
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

set(SRC_MANIFEST_PARSER "${FW_SRC}/content/manifest_parser.cc")
set(SRC_INSTALLER       "${FW_SRC}/content/installer.cc")
set(SRC_RECEIPTS        "${FW_SRC}/content/receipts.cc")
set(SRC_CONTENT_STORE   "${FW_SRC}/content/content_store.cc")
set(SRC_RESOURCE_MODEL  "${FW_SRC}/allocator/resource_model.cc")
set(SRC_ALLOCATOR       "${FW_SRC}/allocator/allocator.cc")
set(SRC_ACTIVATION      "${FW_SRC}/spine/activation.cc")
set(SRC_BUS_MAP         "${FW_SRC}/bus/bus_map.cc")
set(SRC_PERIPH_MGR      "${FW_SRC}/peripherals/peripheral_manager.cc")
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
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
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
    ${SRC_MAPPERS}
    ${SRC_MANIFEST_PARSER}
    ${SRC_INSTALLER}
    ${SRC_RECEIPTS}
    ${SRC_KV_STORE}
    ${SRC_APPEND_LOG}
    ${SRC_FLASH_DEVICE}
    ${SRC_DIAG}
    ${SRC_SHA256}
    ${SRC_POL_VERIFY}
    ${SRC_POL_STORE}
    ${SRC_POSTURE}
)
