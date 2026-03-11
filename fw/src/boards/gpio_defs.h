#pragma once
// gpio_defs.h — GPIO pin and bit constants for jlpicart_board (RP2350B).
//
// Describes the physical wiring between the RP2350B and the MSX cartridge slot.
// No SDK includes — pure constants safe to use in host tests and bus.cc alike.

#include <cstdint>

// GPIO number → RP2350 GPIO index (0-based).
enum PinMapping : int {
    GPIO_A0  = 0,  GPIO_A1  = 1,  GPIO_A2  = 2,  GPIO_A3  = 3,
    GPIO_A4  = 4,  GPIO_A5  = 5,  GPIO_A6  = 6,  GPIO_A7  = 7,
    GPIO_A8  = 8,  GPIO_A9  = 9,  GPIO_A10 = 10, GPIO_A11 = 11,
    GPIO_A12 = 12, GPIO_A13 = 13, GPIO_A14 = 14, GPIO_A15 = 15,

    GPIO_D0     = 16, GPIO_D1 = 17, GPIO_D2 = 18, GPIO_D3 = 19,
    GPIO_D4     = 20, GPIO_D5 = 21, GPIO_D6 = 22, GPIO_D7 = 23,

    GPIO_WR     = 24,
    GPIO_IORQ   = 25,
    GPIO_MERQ   = 26,
    GPIO_RD     = 27,
    GPIO_BUSDIR = 28,
    GPIO_INT    = 29,
    GPIO_WAIT   = 30,
    GPIO_SLTSL  = 31,

    // GPIOs 32-47 — accessed via sio_hw->gpio_hi_in / gpio_hi_out on RP2350.
    GPIO64_CLK     = 32,
    GPIO64_SND     = 33,
    GPIO64_RESET   = 34,
    GPIO64_M1      = 35,
    GPIO64_RFSH    = 36,
    GPIO64_OLEDSDA = 37,
    GPIO64_OLEDSCK = 38,
    GPIO64_V_VSYNC = 39,
    GPIO64_V_HSYNC = 40,
    GPIO64_V_BLUE  = 41,
    GPIO64_V_GREEN = 42,
    GPIO64_V_RED   = 43,
    GPIO64_UART_TX = 44,
    GPIO64_UART_RX = 45,
    GPIO64_VUSB_EN = 46,
    GPIO64_BATSENS = 47,
};

// Single 32-bit bitmask for GPIOs 0–31 (sio_hw->gpio_in / gpio_out / gpio_oe).
enum PinBitMapping : uint32_t {
    BIT_A0  = 1U << 0,  BIT_A1  = 1U << 1,  BIT_A2  = 1U << 2,  BIT_A3  = 1U << 3,
    BIT_A4  = 1U << 4,  BIT_A5  = 1U << 5,  BIT_A6  = 1U << 6,  BIT_A7  = 1U << 7,
    BIT_A8  = 1U << 8,  BIT_A9  = 1U << 9,  BIT_A10 = 1U << 10, BIT_A11 = 1U << 11,
    BIT_A12 = 1U << 12, BIT_A13 = 1U << 13, BIT_A14 = 1U << 14, BIT_A15 = 1U << 15,

    BIT_D0     = 1U << 16, BIT_D1 = 1U << 17, BIT_D2 = 1U << 18, BIT_D3 = 1U << 19,
    BIT_D4     = 1U << 20, BIT_D5 = 1U << 21, BIT_D6 = 1U << 22, BIT_D7 = 1U << 23,

    BIT_WR     = 1U << 24,
    BIT_IORQ   = 1U << 25,
    BIT_MERQ   = 1U << 26,
    BIT_RD     = 1U << 27,
    BIT_BUSDIR = 1U << 28,
    BIT_INT    = 1U << 29,
    BIT_WAIT   = 1U << 30,
    BIT_SLTSL  = 1U << 31,
};

// High-word bitmask for GPIOs 32–63 (sio_hw->gpio_hi_in on RP2350).
// BIT64_X_HI is bit (GPIO64_X - 32) of the high register.
enum PinBitMappingHi : uint32_t {
    BIT64_CLK_HI     = 1U << (GPIO64_CLK     - 32),  // bit 0
    BIT64_SND_HI     = 1U << (GPIO64_SND     - 32),  // bit 1
    BIT64_RESET_HI   = 1U << (GPIO64_RESET   - 32),  // bit 2
    BIT64_M1_HI      = 1U << (GPIO64_M1      - 32),  // bit 3
    BIT64_RFSH_HI    = 1U << (GPIO64_RFSH    - 32),  // bit 4
    BIT64_OLEDSDA_HI = 1U << (GPIO64_OLEDSDA - 32),  // bit 5
    BIT64_OLEDSCK_HI = 1U << (GPIO64_OLEDSCK - 32),  // bit 6
};
