/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// GP0-GP22: normal
// GP25 is LED
// GP26, GP27. GP28

// D0-D7 // BIDIR
// A0-A15 // INPUT
// WR, RD, SLTSL, SL, IORQ MERQ
// WAIT

// D0-D7 -> GP0-GP07
// A0-A15 -> GP8-GP23
// SLTSL -> GP26
// RD -> GP27
// SOUNDIN -> GP28?


// Triggers: when SLTSL go from high to low
// RD 0 -> memory read
// RD 1 -> wait 40us, memory write
// When SLTSL==1, Data goes read

#include "printf.h"
#include "pico/stdlib.h"

#define UART_TX_PIN 28
void _putchar(char c) {

    uint8_t character = c;

    static uint8_t init = 0;
    if (init == 0) {

        gpio_init(UART_TX_PIN);
        gpio_set_dir(UART_TX_PIN, GPIO_OUT);
        gpio_put(UART_TX_PIN, true);
        busy_wait_us_32(10000); 

        init = 1;
    }

    busy_wait_us_32(26); gpio_put(UART_TX_PIN, false);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x01);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x02);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x04);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x08);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x10);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x20);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x40);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, character & 0x80);
    busy_wait_us_32(26); gpio_put(UART_TX_PIN, true);
    busy_wait_us_32(26);
}


#include "pico/stdlib.h"
#include <hardware/clocks.h>
#include <hardware/sync.h>
#include <hardware/vreg.h>

#include <hardware/regs/addressmap.h>
#include <hardware/regs/qmi.h>
#include <hardware/structs/qmi.h>

extern uint8_t __in_flash("games") spelunk_en_rom[];

void init_sys() {

    save_and_disable_interrupts();

   if (true) {
		/* The value for VCO set here is meant for least power
		 * consumption. */
		const unsigned vco = 532000000; /* 266MHz/133MHz */
		const unsigned div1 = 2, div2 = 1;

		vreg_set_voltage(VREG_VOLTAGE_1_15);
		sleep_ms(2);
		set_sys_clock_pll(vco, div1, div2);
		sleep_ms(5);
	}
    uint32_t m0 = qmi_hw->m[0].timing;
    m0 = m0 & ~QMI_M0_TIMING_CLKDIV_BITS;
    m0 += 2;
    qmi_hw->m[0].timing = m0;

    printf("Initialized\n");
}

int main() {

    init_sys();


    uint32_t address = (uint32_t) &spelunk_en_rom[0];
    address += 0x04000000;
    
    //uint32_t address = 0x14000000;
    
    gpio_init(0);
    gpio_set_dir(0, GPIO_OUT);
    
    while (true) {
    
        gpio_put(0, *(volatile const uint8_t *)address & 0);
        //__dmb();
        //gpio_put(1, value);
        gpio_put(0, 37 | *(volatile const uint8_t *)address);
        address += 0x12763;
        address = address & 0x143fffff;
        //sleep_ms(1);
    }
}
