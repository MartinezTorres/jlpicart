#include "board.h"

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