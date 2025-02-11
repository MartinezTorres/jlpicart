#include "board.h"
#include "printf/printf.h"

//#include "pico/stdlib.h"

#include "pico.h"
//#include "pico/stdio.h"
#include "pico/time.h"
#include "hardware/gpio.h"
//#include "hardware/uart.h"


#include "bus.pio.h"


#include "roms/roms.h"

#include "oled/oled.h"

#include "vga/vga.h"

#include <initializer_list>


static inline void init_msx_pio(
    PIO pio, 
    uint8_t data_pin_0)
{

    uint data_bus_size = 8;

    //data_pin_0 = 25; data_bus_size = 1;

    // Claim two state machines
    uint msx_bus_write_sm = 0; pio_sm_claim(pio, 0);

    // Init msx_bus_write config
	uint          msx_bus_write_offset = pio_add_program(pio, &msx_bus_write_program);
	pio_sm_config msx_bus_write_config = msx_bus_write_program_get_default_config( msx_bus_write_offset );

    sm_config_set_out_pins(&msx_bus_write_config, data_pin_0, data_bus_size);

	// Init pin directions
	pio_sm_set_consecutive_pindirs(pio, msx_bus_write_sm, data_pin_0, data_bus_size, false);

    // Init pins
	for(uint8_t pin = data_pin_0; pin < data_pin_0 + data_bus_size; pin++) {

		//gpio_set_input_hysteresis_enabled(pin, false);
		//gpio_set_slew_rate(pin, GPIO_SLEW_RATE_FAST);
		pio_gpio_init(pio, pin);
	}

	// Init state machine
    pio_sm_init(pio, 
         msx_bus_write_sm, 
         msx_bus_write_offset, 
        &msx_bus_write_config);

    pio_sm_set_enabled(pio, msx_bus_write_sm, true);
}

extern bool exit_usb;

namespace init {

    void set_flash_divider(int div) {

        uint32_t m0 = qmi_hw->m[0].timing;
        m0 = m0 & ~QMI_M0_TIMING_CLKDIV_BITS;
        m0 += div;
        qmi_hw->m[0].timing = m0;
    }

    void set_clock_266Mhz() {

        const unsigned vco = 532000000; // 266MHz/133MHz
        const unsigned div1 = 2, div2 = 1;

        set_flash_divider(2);

        vreg_set_voltage(VREG_VOLTAGE_1_15);
        sleep_ms(2);
        set_sys_clock_pll(vco, div1, div2);
        sleep_ms(5);
    }

    //https://forums.raspberrypi.com/viewtopic.php?t=375975
    void set_clock_302Mhz() { // VGA 640x480 @ 60Hz // 25.166Mhz pixel clock, 12 bits x pixel

        const unsigned vco = 906000000;   // (12/2*151) 1206 MHz 
        const unsigned div1 = 3, div2 = 1; // 302 MHz

        set_flash_divider(3);

        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(20);
        set_sys_clock_pll(vco, div1, div2);
        sleep_ms(5);
    }


    void set_clock_297Mhz() {

        const unsigned vco = 1188000000;   // (12*99) 1188 MHz 
        const unsigned div1 = 4, div2 = 1; // 297 MHz

        set_flash_divider(3);

        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(20);
        set_sys_clock_pll(vco, div1, div2);
        sleep_ms(5);
    }
    void set_clock_286_8Mhz() {

        const unsigned vco = 1434000000;   // (6 * 239) 1434 MHz 
        const unsigned div1 = 5, div2 = 1; // 286.8 MHz

        set_flash_divider(3);

        vreg_set_voltage(VREG_VOLTAGE_1_20);
        sleep_ms(20);
        set_sys_clock_pll(vco, div1, div2);
        sleep_ms(5);
    }


    void set_clock_324Mhz() { //1296 / 4 = 324 MHz 

        const unsigned vco = 1296000000;   // (12*108) 1296 MHz 
        const unsigned div1 = 4, div2 = 1; // 324 MHz

        set_flash_divider(3);

        vreg_set_voltage(VREG_VOLTAGE_1_40);
        sleep_ms(20);
        set_sys_clock_pll(vco, div1, div2);
        sleep_ms(5);
    }

    void init() {

        //save_and_disable_interrupts();
        //set_clock_266Mhz();
        set_clock_302Mhz();
        //set_clock_297Mhz();
        //set_clock_324Mhz();
        sleep_ms(50);
        for (uint8_t i=0; i < 16; i++) { gpio_init( GPIO0_A0  + i); gpio_set_dir( GPIO0_A0  + i, GPIO_IN ); }
        for (uint8_t i=0; i <  8; i++) { gpio_init( GPIO16_D0 + i); gpio_set_dir( GPIO16_D0 + i, GPIO_IN ); }


        for (int i : { GPIO24_WR, GPIO25_IORQ, GPIO26_MERQ, GPIO27_RD, GPIO28_BUSDIR, GPIO29_INT, GPIO30_WAIT, GPIO31_SLTSL } ) {
            gpio_init(i); 
            gpio_put(i, false); 
            gpio_set_dir(i, GPIO_IN);
        }
    }
}

#include "bsp/board_api.h"
#include "tusb.h"
struct USBDriveInterface {

    USBDriveInterface() {
        board_init();

        // init host stack on configured roothub port
        tuh_init(BOARD_TUH_RHPORT);

        if (board_init_after_tusb) {
            board_init_after_tusb();
        }

    }

    void run() {
        exit_usb = false;
        while (true) {
            tuh_task();
            if (exit_usb) break;
        }
    }
};


extern uint8_t read_buffer[32*2048];

namespace tests_old {

    void flash_speed_test() {

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

    void log_mem_writes_test() {

        for (uint8_t i=0; i <  8; i++) { gpio_init( 0 + i); gpio_set_dir( 0 + i, GPIO_IN ); }
        for (uint8_t i=0; i < 16; i++) { gpio_init( 8 + i); gpio_set_dir( 8 + i, GPIO_IN ); }

        static constexpr const int IORQ = 24;
        static constexpr const int RD = 26;
        static constexpr const int SLTSL = 27;
        gpio_init( IORQ ); gpio_set_dir( IORQ, GPIO_IN );
        gpio_init( RD ); gpio_set_dir( RD, GPIO_IN );
        gpio_init( SLTSL ); gpio_set_dir( SLTSL, GPIO_IN );

        while (true) {
            uint32_t ios = gpio_get_all();
            if (ios & (1<<SLTSL)) continue;
            bool is_mem_read = !(ios & (1<<RD));
            if (!is_mem_read) { int t = 16; while (t-- && !is_mem_read) is_mem_read = !(gpio_get_all() & (1<<RD)); }

            if (is_mem_read) {
                _putchar('R');                
                _putchar(':');                
                _putchar("0123456789ABCDEF"[(ios>>20)&7]);
                _putchar("0123456789ABCDEF"[(ios>>16)&15]);
                _putchar("0123456789ABCDEF"[(ios>>12)&15]);
                _putchar("0123456789ABCDEF"[(ios>>8)&15]);
                _putchar('\n');  

            } else { //is mem write
                _putchar('W');                
                _putchar(':');                
                _putchar("0123456789ABCDEF"[(ios>>4)&15]);
                _putchar("0123456789ABCDEF"[ios&15]);
                _putchar(':');                
                _putchar("0123456789ABCDEF"[(ios>>20)&7]);
                _putchar("0123456789ABCDEF"[(ios>>16)&15]);
                _putchar("0123456789ABCDEF"[(ios>>12)&15]);
                _putchar("0123456789ABCDEF"[(ios>>8)&15]);
                _putchar('\n');                
            }
        }
    }

    void log_test_game() {

        for (uint8_t i=0; i <  8; i++) { gpio_init( 0 + i); gpio_set_dir( 0 + i, GPIO_IN ); }
        for (uint8_t i=0; i < 16; i++) { gpio_init( 8 + i); gpio_set_dir( 8 + i, GPIO_IN ); }

        static constexpr const int IORQ = 24;
        static constexpr const int RD = 26;
        static constexpr const int SLTSL = 27;
        gpio_init( IORQ ); gpio_set_dir( IORQ, GPIO_IN );
        gpio_init( RD ); gpio_set_dir( RD, GPIO_IN );
        gpio_init( SLTSL ); gpio_set_dir( SLTSL, GPIO_IN );
        
            printf("O");
        sleep_ms(500);
            printf("A");
        init_msx_pio( pio0, 0);

            printf("AA");

        uint32_t address = (uint32_t) &spelunk_en_rom[0];
        //uint32_t address = (uint32_t) &pwnd_rom[0];
        address += 0x04000000;

        uint8_t *rom = (uint8_t *)address;
        uint8_t ram[32*1024];
        for (int x=0; x<32*1024; x++) {
            //ram[x] = rom[x];
            ram[x] = read_buffer[x];
        }

        //for (int x=0; x<16*1024; x++) {
        //    ram[x+16*1024] = rom[x];
        //}

        while (true) {
            uint32_t ios = gpio_get_all();
                
            if (ios & (1<<SLTSL)) continue;
            uint32_t address = ( (ios>>8) + 0x4000 ) & 0x7FFF;
            //uint32_t address = ( (ios>>8) + 0x0000 ) & 0x7FFF;
            uint8_t v = ram[address];

            //ios = gpio_get_all();
            bool is_mem_read = !(ios & (1<<RD));
            if (!is_mem_read) { int t = 16; while (t-- && !is_mem_read) is_mem_read = !(gpio_get_all() & (1<<RD)); }

            if (is_mem_read) {

                pio0->txf[0] = v;
                if (false) {
                    _putchar('R');                
                    ios = gpio_get_all();
                    _putchar("0123456789ABCDEF"[(ios >> 4) & 15]);
                    _putchar("0123456789ABCDEF"[(ios >> 0) & 15]);
                    _putchar('[');                
                    _putchar("0123456789ABCDEF"[(address >> 12) & 15]);
                    _putchar("0123456789ABCDEF"[(address >> 8) & 15]);
                    _putchar("0123456789ABCDEF"[(address >> 4) & 15]);
                    _putchar("0123456789ABCDEF"[address & 15]);
                    _putchar(']');
                    _putchar(':');
                    _putchar("0123456789ABCDEF"[(v >> 4) & 15]);
                    _putchar("0123456789ABCDEF"[(v >> 0) & 15]);
                    _putchar('\n');  
                }

            } else {               
            }
            
            while (!(gpio_get_all() & (1<<SLTSL)));
        }
    }


    void log_test_pio() {

        for (uint8_t i=0; i <  8; i++) { gpio_init( 0 + i); gpio_set_dir( 0 + i, GPIO_IN ); }
        for (uint8_t i=0; i < 16; i++) { gpio_init( 8 + i); gpio_set_dir( 8 + i, GPIO_IN ); }

        static constexpr const int IORQ = 24;
        static constexpr const int RD = 26;
        static constexpr const int SLTSL = 27;
        gpio_init( IORQ ); gpio_set_dir( IORQ, GPIO_IN );
        gpio_init( RD ); gpio_set_dir( RD, GPIO_IN );
        gpio_init( SLTSL ); gpio_set_dir( SLTSL, GPIO_IN );
        
        gpio_pull_up(7);
        gpio_pull_up(6);
        gpio_pull_up(5);
        gpio_pull_up(4);

        gpio_pull_up(3);
        gpio_pull_up(2);
        gpio_pull_up(1);
        gpio_pull_up(0);

            printf("O");
        sleep_ms(500);
            printf("A");
        init_msx_pio( pio0, 0);

        gpio_init( RD ); 
        gpio_set_dir( RD, GPIO_OUT );
        gpio_put(RD, true);
        

            printf("AA\n");

            printf("pre: %08X\n", gpio_get_all());
            sleep_ms(10);

            printf("pre: %08X\n", gpio_get_all());

            gpio_put(RD, false);
            pio0->txf[0] = 0x35;
            sleep_ms(10);

            printf("pre: %08X\n", gpio_get_all());

            gpio_put(RD, true);
            pio0->txf[0] = 0x39;
            sleep_ms(10);

            printf("pre: %08X\n", gpio_get_all());

            gpio_put(RD, false);
            pio0->txf[0] = 0x36;
            sleep_ms(10);

            printf("pre: %08X\n", gpio_get_all());

            gpio_put(RD, true);
            sleep_ms(10);
            printf("pre: %08X\n", gpio_get_all());


    }    

    void log_test_usb() {
        USBDriveInterface di;
        di.run();
    }    
}

namespace tests {

    void log_test_game() {

        init_msx_pio( pio0, GPIO16_D0);

        uint32_t address = (uint32_t) &spelunk_en_rom[0];
        //address += 0x04000000;

        uint8_t *rom = (uint8_t *)address;
        uint8_t ram[32*1024];
        for (int x=0; x<32*1024; x++) {
            ram[x] = rom[x];
        }

        while (true) {
            uint32_t ios = gpio_get_all();
                
            if (ios & (1U<<GPIO31_SLTSL)) continue;

            bool is_mem_read  = !(ios & (1U<<GPIO27_RD));
            bool is_mem_write = !(ios & (1U<<GPIO24_WR));
            while ( (is_mem_read or is_mem_write) == false) {
                is_mem_read  = !(ios & (1U<<GPIO27_RD));
                is_mem_write = !(ios & (1U<<GPIO24_WR));
                ios = gpio_get_all();
            }

            if (is_mem_read) {

                if(false) {
                    _putchar('R');                
                    ios = gpio_get_all();
                    _putchar('[');                
                    _putchar("0123456789ABCDEF"[(address >> 12) & 15]);
                    _putchar("0123456789ABCDEF"[(address >> 8) & 15]);
                    _putchar("0123456789ABCDEF"[(address >> 4) & 15]);
                    _putchar("0123456789ABCDEF"[address & 15]);
                    _putchar(']');
                    _putchar('\n');  
                }

                gpio_set_dir(GPIO30_WAIT, GPIO_OUT);
                uint32_t address = ( ios + 0x4000 ) & 0x7FFF;
                uint8_t v = ram[address];
                pio0->txf[0] = v;
                gpio_set_dir(GPIO30_WAIT, GPIO_IN);


            } else {

            }
            
            while ( !(gpio_get_all() & (1U<<GPIO31_SLTSL)) );
        }
    }    
}


int main() {

    init::init();
    putstring("\n\nInitialized\n\n");


    SSD1306::init();
    SSD1306::enable_display(true);
    SSD1306::puts("SPELUNKER");

    putstring("SSD1306 Initialized\n");

    vga320_init();

    putstring("VGA Initialized\n");

    //tests::log_test_usb();
    //__disable_irq();
    tests::log_test_game();
}
