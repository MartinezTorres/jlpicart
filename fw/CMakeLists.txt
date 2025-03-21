# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/add_roms.cmake)
add_roms( roms/ generated_roms/roms )

include(cmake/add_binaries.cmake)
add_binaries( bin_data/ generated_binaries/binaries )


# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD jlpicart_board CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform type")
set(PICO_BOARD_HEADER_DIRS ${CMAKE_CURRENT_LIST_DIR}/sdk )

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(jlpicart C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
#add_compile_options(-Wall -Wextra -pedantic -Wnull-dereference)

add_executable(jlpicart
    src/main.cc

    src/config/config.cc
    src/multitask/multitask.cc

    src/bus/bus.cc
#    src/bus/cartridge.cc
#    src/bus/z80stub.cc

    src/adc/adc.cc
    src/oled/ssd1306.cc

    src/esp32/esp32.cc
    src/esp32/esp_jlpicart_port.cc

    src/video/crt.cc
#    src/video/vdp99x8.cc
#    src/video/tms9918i.cc

    
    ${CMAKE_BINARY_DIR}/generated_roms/roms.cc
    ${CMAKE_BINARY_DIR}/generated_binaries/binaries.cc
#    src/usb.c

#    src/printf/printf.c
#    src/printf/putchar.c
    
#    src/roms/spelunk-en.rom.cc
#    src/roms/pwnd.rom.cc


#    src/crt/crt.cc

#    src/vdp99x8/vdp99x8.cc
    )

# Add executable. Default name is the project name, version 0.1
pico_set_program_name(jlpicart "jlpicart")
pico_set_program_version(jlpicart "0.1")

# Generate PIO headers
pico_generate_pio_header(jlpicart ${CMAKE_CURRENT_SOURCE_DIR}/src/video/crt.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(jlpicart 0)
pico_enable_stdio_usb(jlpicart 0)

# Add the standard include files to the build
target_include_directories(jlpicart PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)

#set(PORT PI_PICO)
set(FLASHER_DIR ${CMAKE_CURRENT_LIST_DIR}/ext/esp32/esp-serial-flasher)
add_subdirectory(${FLASHER_DIR} ${CMAKE_BINARY_DIR}/flasher)

# pull in common dependencies
target_link_libraries(jlpicart
#    pico_stdlib
#    hardware_uart
#    pico_stdio
#    tinyusb_host 
#    tinyusb_board

    hardware_gpio
    hardware_flash
    hardware_divider
    pico_time
    pico_util
    pico_platform
    pico_runtime

    hardware_pio
    hardware_dma
    hardware_adc 
    pico_multicore

    flasher
)

set_target_properties(jlpicart PROPERTIES PICO_TARGET_LINKER_SCRIPT ${CMAKE_CURRENT_LIST_DIR}/sdk/memmap_jlpicart.ld)



# create map/bin/hex file etc.
pico_add_extra_outputs(jlpicart)

#set (PICO_COPY_TO_RAM 1)

# add url via pico_set_program_url

# family_configure_host_example(jlpicart noos)

target_compile_options(jlpicart PRIVATE -Wall -Wextra -Wpedantic -Wnull-dereference -Winvalid-pch -Wformat=2 -Winit-self -Winline -Wpacked -Wpointer-arith -Wmissing-declarations -Wmissing-format-attribute -Wmissing-noreturn -Wredundant-decls -Wsign-compare -Wswitch-enum -Wundef -Wunreachable-code -Wwrite-strings)
# -Wstrict-aliasing=2