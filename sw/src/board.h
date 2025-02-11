#pragma once

#include "pico.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/multicore.h"
#include "printf/printf.h"


#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/vreg.h>
#include <hardware/pio.h>
#include <hardware/regs/addressmap.h>
#include <hardware/regs/qmi.h>
#include <hardware/structs/qmi.h>


#include <functional>


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

static inline void putstring( const char *str ) { while (*str) { _putchar(*str++); } }
