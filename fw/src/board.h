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
#include <hardware/regs/busctrl.h>

#include <hardware/structs/bus_ctrl.h>
#include <hardware/structs/systick.h>
#include <hardware/structs/qmi.h>

#include <functional>
#include <initializer_list>
#include <bitset>

enum PinMapping :int {
    GPIO_A0  = 0,
    GPIO_A1  = 1,
    GPIO_A2  = 2,
    GPIO_A3  = 3,
    GPIO_A4  = 4,
    GPIO_A5  = 5,
    GPIO_A6  = 6,
    GPIO_A7  = 7,
    GPIO_A8  = 8,
    GPIO_A9  = 9,
    GPIO_A10 = 10,
    GPIO_A11 = 11,
    GPIO_A12 = 12,
    GPIO_A13 = 13,
    GPIO_A14 = 14,
    GPIO_A15 = 15,

    GPIO_D0 = 16,
    GPIO_D1 = 17,
    GPIO_D2 = 18,
    GPIO_D3 = 19,
    GPIO_D4 = 20,
    GPIO_D5 = 21,
    GPIO_D6 = 22,
    GPIO_D7 = 23,
    
    GPIO_WR     = 24,
    GPIO_IORQ   = 25,
    GPIO_MERQ   = 26,
    GPIO_RD     = 27,
    GPIO_BUSDIR = 28,
    GPIO_INT    = 29,
    GPIO_WAIT   = 30,
    GPIO_SLTSL  = 31,

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

enum PinBitMapping : uint64_t {
    BIT_A0 = 1U << 0,
    BIT_A1 = 1U << 1,
    BIT_A2 = 1U << 2,
    BIT_A3 = 1U << 3,
    BIT_A4 = 1U << 4,
    BIT_A5 = 1U << 5,
    BIT_A6 = 1U << 6,
    BIT_A7 = 1U << 7,
    BIT_A8 = 1U << 8,
    BIT_A9 = 1U << 9,
    BIT_A10 = 1U << 10,
    BIT_A11 = 1U << 11,
    BIT_A12 = 1U << 12,
    BIT_A13 = 1U << 13,
    BIT_A14 = 1U << 14,
    BIT_A15 = 1U << 15,

    BIT_D0 = 1U << 16,
    BIT_D1 = 1U << 17,
    BIT_D2 = 1U << 18,
    BIT_D3 = 1U << 19,
    BIT_D4 = 1U << 20,
    BIT_D5 = 1U << 21,
    BIT_D6 = 1U << 22,
    BIT_D7 = 1U << 23,


    BIT_WR     = 1U << 24,
    BIT_IORQ   = 1U << 25,
    BIT_MERQ   = 1U << 26,
    BIT_RD     = 1U << 27,
    BIT_BUSDIR = 1U << 28,
    BIT_INT    = 1U << 29,
    BIT_WAIT   = 1U << 30,
    BIT_SLTSL  = 1U << 31,

    BIT64_CLK     = 1ULL << 32,
    BIT64_SND     = 1ULL << 33,
    BIT64_RESET   = 1ULL << 34,
    BIT64_M1      = 1ULL << 35,
    BIT64_RFSH    = 1ULL << 36,
    BIT64_OLEDSDA = 1ULL << 37,
    BIT64_OLEDSCK = 1ULL << 38,
    BIT64_V_VSYNC = 1ULL << 39,
    BIT64_V_HSYNC = 1ULL << 40,
    BIT64_V_BLUE  = 1ULL << 41,
    BIT64_V_GREEN = 1ULL << 42,
    BIT64_V_RED   = 1ULL << 43,
    BIT64_UART_TX = 1ULL << 44,
    BIT64_UART_RX = 1ULL << 45,
    BIT64_VUSB_EN = 1ULL << 46,
    BIT64_BATSENS = 1ULL << 47,
};

///////////////////////////////////////////////////////////////////////////////
//
// FIXED MEMORY LOCATIONS.
//
// ******* YOU MUST ALSO UPDATE THE MEMORY LAYOUT IN memmap_jlpicart.d ********
// MEMORY
// {
//    FLASH(rx) :           ORIGIN = 0x10000000, LENGTH = 192k
//
//    RAM(rwx) :            ORIGIN = 0x20000000, LENGTH = 128k
//    SCRATCH_X(rwx) :      ORIGIN = 0x20080000, LENGTH = 4k
//    SCRATCH_Y(rwx) :      ORIGIN = 0x20081000, LENGTH = 4k
// }
//

constexpr uint32_t FLASH_BIOS      = 0x10030000U; //  32K 
constexpr uint32_t FLASH_CONF_A    = 0x10038000U; //  16K
constexpr uint32_t FLASH_CONF_B    = 0x1003C000U; //  16K
constexpr uint32_t FLASH_SAVES     = 0x10040000U; // 256K
constexpr uint32_t FLASH_PAYLOAD_A = 0x10080000U; // 512K
constexpr uint32_t FLASH_PAYLOAD_B = 0x10100000U; //  15M

constexpr uint32_t RAM_EXT         = 0x20020000U; // 384K



#include <debug/debug_device.h>
//using DBG = DebugDevice<DEBUG_WARNING, DEBUG_DEVICE_GPIO38_OLEDSCK>;
//using DBG = DebugDevice<DEBUG_INFO, DEBUG_DEVICE_GPIO38_OLEDSCK>;
using DBG = DebugDevice<DEBUG_ERROR, DEBUG_DEVICE_DISABLED>;


inline void set_speed(size_t vco_mhz, size_t system_divider, size_t flash_divider, enum vreg_voltage voltage) {
    
    // SET SAFE 150MHz SPEED
    {
        
        // SET SYS_CLK to 150MHz
        set_sys_clock_pll(750*1000*1000, 5, 1);
        busy_wait_us(10 * 1000);
        busy_wait_us(10 * 1000);

        // SET VOLTAGE TO 1.1V
        vreg_set_voltage(VREG_VOLTAGE_1_10);
        busy_wait_us(10 * 1000);

        // SET FLASH DIVIDER TO 3
        {
            uint32_t m0 = qmi_hw->m[0].timing;
            m0 = m0 & ~QMI_M0_TIMING_CLKDIV_BITS;
            m0 += 3;
            qmi_hw->m[0].timing = m0;
        }
    }

    // SET REQUESTED SPEED
    {
        // SET FLASH DIVIDER
        {
            uint32_t m0 = qmi_hw->m[0].timing;
            m0 = m0 & ~QMI_M0_TIMING_CLKDIV_BITS;
            m0 += flash_divider;
            qmi_hw->m[0].timing = m0;
        }

        // SET VOLTAGE
        vreg_set_voltage(voltage);
        busy_wait_us(50 * 1000);

        // SET PLL
        set_sys_clock_pll(vco_mhz * 1000 * 1000, system_divider, 1);
        busy_wait_us(50 * 1000);

    }

    DBG::init();
}