#include "board.h"
#include "util.h"

#include "bus.pio.h"
#include "roms/roms.h"
#include "oled/oled.h"
#include "crt/crt.h"
#include "vdp99x8/vdp99x8.h"




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

    void __no_inline_not_in_flash_func(log_test_game)() {

        const uint8_t *rom_ = (const uint8_t *)&spelunk_en_rom[0];
        static uint8_t rom[32*1024];
        for (int i=0; i<32*1024; i++) rom[i] = rom_[i];

        gpio_put(GPIO30_WAIT, false); 

        systick_hw->csr |= 0x00000005U;
        systick_hw->rvr = 0x00FFFFFFU;
        while (true) {

            auto gpio_get_all_int = [](){ return sio_hw->gpio_in; };
            auto gpio_wait_zero = [](uint32_t neg_bitmask){ uint32_t io; do { io = sio_hw->gpio_in; } while ( (io & neg_bitmask) == 0 ); };

            uint32_t start_tick = systick_hw->cvr;
            uint32_t ios = gpio_get_all_int();
            
            constexpr uint32_t operation_mask         = BIT25_IORQ + BIT26_MERQ + BIT27_RD + BIT24_WR + BIT31_SLTSL;
            constexpr uint32_t operation_memory_read  = operation_mask - BIT26_MERQ - BIT27_RD - BIT31_SLTSL;
            constexpr uint32_t operation_memory_write = operation_mask - BIT26_MERQ - BIT24_WR - BIT31_SLTSL;
            constexpr uint32_t operation_io_read      = operation_mask - BIT25_IORQ - BIT27_RD;
            constexpr uint32_t operation_io_write     = operation_mask - BIT25_IORQ - BIT24_WR ; 
            uint32_t operation = ios & operation_mask; 
            
            if ( operation == operation_memory_read ) {
                uint32_t rom_32k_address = ( ios + 0x4000 ) & 0x7FFF;

                gpio_set_dir_all_bits(BIT30_WAIT);            
                uint8_t v = rom[rom_32k_address];
                
                uint32_t elapsed_tick = start_tick - systick_hw->cvr;
                if(0) if (elapsed_tick > 31) {

                    uint32_t bus_address = ios & 0xFFFF;
                    uint32_t elapsed_tick = start_tick - systick_hw->cvr;
                    _putchar('R');
                    _putchar(':');                
                    _putchar("0123456789ABCDEF"[(elapsed_tick >> 12)&0xF]);
                    _putchar("0123456789ABCDEF"[(elapsed_tick >>  8)&0xF]);
                    _putchar("0123456789ABCDEF"[(elapsed_tick >>  4)&0xF]);
                    _putchar("0123456789ABCDEF"[(elapsed_tick      )&0xF]);
                    _putchar(':');                
                    _putchar("0123456789ABCDEF"[(bus_address >> 12)&0xF]);
                    _putchar("0123456789ABCDEF"[(bus_address >>  8)&0xF]);
                    _putchar("0123456789ABCDEF"[(bus_address >>  4)&0xF]);
                    _putchar("0123456789ABCDEF"[(bus_address      )&0xF]);
                    _putchar(':');                
                    _putchar("0123456789ABCDEF"[(v >> 4 )&0xF]);
                    _putchar("0123456789ABCDEF"[(v      )&0xF]);
                    _putchar('\n');
                }
                
                gpio_put_all(uint32_t(v) << GPIO16_D0);
                gpio_set_dir_all_bits(0xFFU << GPIO16_D0);
                gpio_wait_zero( BIT27_RD );   
                gpio_set_dir_all_bits(0);

            } else if (operation == operation_memory_write) {

                gpio_wait_zero( BIT24_WR );   

            } else if (operation == operation_io_read) {

                uint8_t port = ios & 0xFF;
                VDP99X8::io_read( port );
                gpio_wait_zero( BIT27_RD );   

            } else if (operation == operation_io_write) {

                uint8_t port = ios & 0xFF;
                uint8_t data = (ios >> 16) & 0xFF;
                VDP99X8::io_write( port, data );
                gpio_wait_zero( BIT24_WR );   
            }            
        }
    }    
}


int main() {

    SSD1306::init();
    SSD1306::enable_display(true);
    SSD1306::puts("MEGA PI 2");

    _putchar_reset();
    putstring("SSD1306 Initialized\n");

    // INIT BOARD
    {
        set_speed(798, 3, 2, VREG_VOLTAGE_1_15);

        auto activate_gpio = [](int i) { 
            gpio_init( i ); 
            gpio_set_dir( i, GPIO_IN );
            gpio_put(i, false); 
            //gpio_set_input_hysteresis_enabled( i, true );
            //gpio_set_slew_rate( i, GPIO_SLEW_RATE_FAST );
            //gpio_set_drive_strength( i, GPIO_DRIVE_STRENGTH_12MA );
        };

        for (uint8_t i=0; i < 16; i++) activate_gpio(GPIO0_A0 + i);
        
        for (uint8_t i=0; i <  8; i++) activate_gpio(GPIO16_D0 + i);

        for (int i : { GPIO24_WR, GPIO25_IORQ, GPIO26_MERQ, GPIO27_RD, GPIO28_BUSDIR, GPIO29_INT, GPIO30_WAIT, GPIO31_SLTSL } ) activate_gpio( i );

        gpio_init(GPIO46_VUSB_EN); 
        gpio_put(GPIO46_VUSB_EN, true); 
        gpio_set_dir(GPIO46_VUSB_EN, GPIO_OUT);

        adc_init();
        adc_gpio_init(GPIO47_BATSENS);
        adc_set_temp_sensor_enabled(true);
    }
    _putchar_reset();
    putstring("\n\nInitialized\n\n");

    VDP99X8::init();

    //CRT::init(CRT::SCART_HI_240p_60Hz);
    
    if (0) CRT::add_hook_line(1, [](){
        int temp = get_temp();
        printf("Temp: %d.%d\n", temp/10, temp%10);
    });
    if (0) CRT::add_hook_line(2, [](){
        int vbat = get_vbat();
        printf("Vbat: %d\n", vbat);
    });

    putstring("CRT Initialized\n");

    //do { busy_wait_us(64000000); } while ( true );

    //tests::log_test_usb();
    save_and_disable_interrupts();
    tests::log_test_game();
    //while (true);
}
