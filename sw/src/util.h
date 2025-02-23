#pragma once

#include "board.h"
#include "printf/printf.h"

inline void set_speed(size_t vco_mhz, size_t system_divider, size_t flash_divider, enum vreg_voltage voltage) {
    
    // SET SAFE 150MHz SPEED
    {
        
        // SET SYS_CLK to 150MHz
        set_sys_clock_pll(750*1000*1000, 5, 1);
        busy_wait_us(10 * 1000);
        busy_wait_us(10 * 1000);

        // SET VOLTAGE TO 1.V
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

     _putchar_reset();

}

static inline void putstring( const char *str ) { while (*str) { _putchar(*str++); } }

static inline uint16_t get_temp() {

    adc_select_input(8);
    const float conversion_factor = 3.3f / (1 << 12);
    const float voltage = adc_read() * conversion_factor;
    const float temp = 27 - (voltage-0.706) / 0.001721;
    return uint16_t(temp*10.+0.5);
}

static inline uint16_t get_vbat() {

    adc_select_input(7);
    const int conversion_factor = (3300 << 16) / (1 << 12);
    return (adc_read() * conversion_factor) >> 15;   
}

static inline void putchar_uart1_nonblocking(char c) {
    uart_get_hw(uart1)->dr = c;
}