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

set(SRC_API_WINDOW   "${FW_SRC}/msx/api/api_window.cc")
set(SRC_CORE_SERVICE "${FW_SRC}/msx/api/services/core_service.cc")

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
