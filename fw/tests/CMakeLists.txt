cmake_minimum_required(VERSION 3.13)
project(jlpicart_tests CXX)

set(CMAKE_CXX_STANDARD 20)
enable_testing()

# Host tests run on the build machine (Linux amd64), not on the RP2350.
# They test firmware logic that is portable: framing, parsing, crypto,
# state machines. No Pico SDK involved.

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# smoke_test — validates the harness itself
add_executable(smoke_test host/smoke_test.cc)
add_test(NAME smoke_test COMMAND smoke_test)

# Future tests are added here following the same pattern:
#   add_executable(<name> host/<name>.cc [host/<shared>.cc ...])
#   add_test(NAME <name> COMMAND <name>)
