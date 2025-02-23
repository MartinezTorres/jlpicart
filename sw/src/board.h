#pragma once

#include <pico.h>
#include <pico/time.h>
#include <pico/multicore.h>
#include <pico/util/queue.h>

#include <hardware/adc.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/sync.h>
#include <hardware/uart.h>
#include <hardware/vreg.h>

#include <hardware/regs/addressmap.h>
#include <hardware/regs/qmi.h>

#include <hardware/structs/systick.h>
#include <hardware/structs/qmi.h>

#include <functional>
#include <initializer_list>

typedef enum {
    GPIO0_A0 = 0,
    GPIO1_A1 = 1,
    GPIO2_A2 = 2,
    GPIO3_A3 = 3,
    GPIO4_A4 = 4,
    GPIO5_A5 = 5,
    GPIO6_A6 = 6,
    GPIO7_A7 = 7,
    GPIO8_A8 = 8,
    GPIO9_A9 = 9,
    GPIO10_A10 = 10,
    GPIO11_A11 = 11,
    GPIO12_A12 = 12,
    GPIO13_A13 = 13,
    GPIO14_A14 = 14,
    GPIO15_A15 = 15,
    GPIO16_D0 = 16,
    GPIO17_D1 = 17,
    GPIO18_D2 = 18,
    GPIO19_D3 = 19,
    GPIO20_D4 = 20,
    GPIO21_D5 = 21,
    GPIO22_D6 = 22,
    GPIO23_D7 = 23,
    GPIO24_WR = 24,
    GPIO25_IORQ = 25,
    GPIO26_MERQ = 26,
    GPIO27_RD = 27,
    GPIO28_BUSDIR = 28,
    GPIO29_INT = 29,
    GPIO30_WAIT = 30,
    GPIO31_SLTSL = 31,
    GPIO32_CLK = 32,
    GPIO33_SND = 33,
    GPIO34_RESET = 34,
    GPIO35_M1 = 35,
    GPIO36_RFSH = 36,
    GPIO37_OLEDSDA = 37,
    GPIO38_OLEDSCK = 38,
    GPIO39_V_VSYNC = 39,
    GPIO40_V_HSYNC = 40,
    GPIO41_V_BLUE = 41,
    GPIO42_V_GREEN = 42,
    GPIO43_V_RED = 43,
    GPIO44_UART_TX = 44,
    GPIO45_UART_RX = 45,
    GPIO46_VUSB_EN = 46,
    GPIO47_BATSENS = 47
} PinMapping;

typedef enum {
    BIT0_A0 = 1U << 0,
    BIT1_A1 = 1U << 1,
    BIT2_A2 = 1U << 2,
    BIT3_A3 = 1U << 3,
    BIT4_A4 = 1U << 4,
    BIT5_A5 = 1U << 5,
    BIT6_A6 = 1U << 6,
    BIT7_A7 = 1U << 7,
    BIT8_A8 = 1U << 8,
    BIT9_A9 = 1U << 9,
    BIT10_A10 = 1U << 10,
    BIT11_A11 = 1U << 11,
    BIT12_A12 = 1U << 12,
    BIT13_A13 = 1U << 13,
    BIT14_A14 = 1U << 14,
    BIT15_A15 = 1U << 15,
    BIT16_D0 = 1U << 16,
    BIT17_D1 = 1U << 17,
    BIT18_D2 = 1U << 18,
    BIT19_D3 = 1U << 19,
    BIT20_D4 = 1U << 20,
    BIT21_D5 = 1U << 21,
    BIT22_D6 = 1U << 22,
    BIT23_D7 = 1U << 23,
    BIT24_WR = 1U << 24,
    BIT25_IORQ = 1U << 25,
    BIT26_MERQ = 1U << 26,
    BIT27_RD = 1U << 27,
    BIT28_BUSDIR = 1U << 28,
    BIT29_INT = 1U << 29,
    BIT30_WAIT = 1U << 30,
    BIT31_SLTSL = 1U << 31,
} BitMapping;