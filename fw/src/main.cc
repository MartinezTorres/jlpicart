#include <board.h>
#include <multitask/multitask.h>

#include <config/config.h>

#include <oled/ssd1306.h>
#include <esp32/esp32.h>
#include <bus/bus.h>
#include <adc/adc.h>
#include <video/vdp99x8.h>

#include "generated_roms/roms.list.h"

///////////////////////////////////////////////////////////////////////////////
// Ideally, after a reset, we would set the following:
// Subslot 0: disk and network routines, if enabled
// Subslot 1: boot and configuration room, if enabled
// Subslot 2: 64KB of RAM, if enabled
// Subslot 3: ROM / GAME Payload, if enabled
static Config::Config config; 

static Multitask::CallAgain temp_and_voltage_callback() {
    static uint32_t old_frame_idx = 0;
    if (CRT::frame_idx != old_frame_idx) {
        old_frame_idx = CRT::frame_idx;

        if (CRT::frame_idx % 2 == 0) {
            int temp = ADC::get_temp();

            if (temp > 1000) temp = 999;

            SSD1306::locate(3,2);
            SSD1306::puts("Temp: ");
            SSD1306::putchar('0' + (temp / 100)%10);
            SSD1306::putchar('0' + (temp / 10)%10);
            SSD1306::putchar('.');
            SSD1306::putchar('0' + (temp / 1)%10);
            SSD1306::putchar('0');
        } else {
            int vbat = ADC::get_vbat();

            if (vbat > 9999) vbat = 9999;

            SSD1306::locate(3,3);
            SSD1306::puts("Vbat: ");
            if (vbat >= 1000) SSD1306::putchar('0' + (vbat / 1000)%10); else SSD1306::putchar(' ');
            SSD1306::putchar('.');
            SSD1306::putchar('0' + (vbat / 100)%10);
            SSD1306::putchar('0' + (vbat /  10)%10);
            SSD1306::putchar('0' + (vbat /   1)%10);
        }
        
    }
    return Multitask::CALL_AGAIN;
}

static void on_reset() {

    static int n_cart = 0;

    DBG::msg<DEBUG_INFO>("Reset routine started");

    Multitask::clear_tasks();

    using namespace Config;
    static Config::Config config;

    config.read_from_flash();

    ///////////////////////////////////////////////////////////////////////////
    // INIT BOARD DEVICES

    if (config.vdp.status == ENABLED) {

        VDP99X8::init();
    }

    if (config.audio.PSG0.status == ENABLED) {
    }

    if (config.audio.PSG1.status == ENABLED) {
    }

    if (config.audio.SCC.status == ENABLED) {
    }

    if (config.oled.status == ENABLED) {
        SSD1306::init();
        SSD1306::enable_display(true);
        SSD1306::puts(cartridge_list[n_cart].name);

        //Multitask::add_task(temp_and_voltage_callback);    
    }

    if (config.eink.status == ENABLED) {
    }


    if (config.usb.status == ENABLED) {
    }

    if (config.esp32.status == ENABLED) {
        ESP32::init();
    }

    ///////////////////////////////////////////////////////////////////////////
    // INIT BUS AND SUBSLOTS

    if (config.slot.expander == ENABLED) {
        BUS::is_expanded = true;
        BUS::subslots[0] = BUS::SUBSLOT0;
        BUS::subslots[1] = BUS::SUBSLOT0;
        BUS::subslots[2] = BUS::SUBSLOT0;
        BUS::subslots[3] = BUS::SUBSLOT0;
        
        BUS::init_subslot( config.slot.subslots[0], BUS::SUBSLOT0 );
        BUS::init_subslot( config.slot.subslots[1], BUS::SUBSLOT1 );
        BUS::init_subslot( config.slot.subslots[2], BUS::SUBSLOT2 );
        BUS::init_subslot( config.slot.subslots[3], BUS::SUBSLOT3 );
        BUS::init_subslot( cartridge_list[n_cart], BUS::SUBSLOT2 );
    } else {
        BUS::is_expanded = false;
        BUS::subslots[0] = BUS::SUBSLOT0;
        //BUS::init_subslot( config.slot.subslots[0], BUS::SUBSLOT0 );
        BUS::init_subslot( cartridge_list[n_cart], BUS::SUBSLOT0 );
        
    }

    n_cart++;
    if (n_cart == sizeof( cartridge_list) / sizeof (cartridge_list[0]) ) n_cart = 0;


    DBG::msg<DEBUG_INFO>("Reset routine finished");
}

int main() {

    // INIT BOARD
    {
        // GENERAL GPIO INITIALIZATION
        auto activate_gpio = [](int i) { 
            gpio_init( i ); 
            gpio_set_dir( i, GPIO_IN );
            gpio_put(i, false); 
            
            gpio_set_input_hysteresis_enabled( i, true );
            gpio_set_slew_rate( i, GPIO_SLEW_RATE_FAST );
            gpio_set_drive_strength( i, GPIO_DRIVE_STRENGTH_8MA );
        };

        for (uint8_t i=0; i < 16; i++) activate_gpio(GPIO_A0 + i);
        
        for (uint8_t i=0; i <  8; i++) activate_gpio(GPIO_D0 + i);

        for (int i : { GPIO_WR, GPIO_IORQ, GPIO_MERQ, GPIO_RD, GPIO_BUSDIR, GPIO_INT, GPIO_WAIT, GPIO_SLTSL, GPIO64_RESET } ) activate_gpio( i );

        // ENABLE WAIT SIGNAL TO PAUSE MSX
        gpio_put(GPIO_WAIT, false); 
        gpio_set_dir(GPIO_WAIT, GPIO_OUT);
        
        // ENABLE USB POWER 
        // TODO: Make it configurable, maybe bring it to USB
        gpio_init(GPIO64_VUSB_EN); 
        gpio_put(GPIO64_VUSB_EN, true); 
        gpio_set_dir(GPIO64_VUSB_EN, GPIO_OUT);

        // SET MAXIMUM PRIORITY TO THE PROC0 BUS
        bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC0_BITS;

        // Init systick
        systick_hw->csr |= 0x00000005U;
        systick_hw->rvr = 0x00FFFFFFU;

        // SET SPEED TO APPROX 266 Mhz
        set_speed(798, 3, 2, VREG_VOLTAGE_1_15);

        // INIT ADC SUBSYSTEM AND TEMPERATURE SENSOR
        adc_init();
        adc_gpio_init(GPIO64_BATSENS);
        adc_set_temp_sensor_enabled(true);
    }


    DBG::init();
    DBG::msg<DEBUG_INFO>("\n\nJLPiCart Initialized");
 
    /*VDP99X8::init();
    
    if (0) CRT::add_hook_line(1, [](){
        int temp = get_temp();
        printf("Temp: %d.%d\n", temp/10, temp%10);
    });
    if (0) CRT::add_hook_line(2, [](){
        int vbat = get_vbat();
        printf("Vbat: %d\n", vbat);
    });

    putstring("CRT Initialized\n");*/

    //do { busy_wait_us(64000000); } while ( true );

    //tests::log_test_usb();

    Multitask::init();

    BUS::reset_callback = &on_reset;
    BUS::start();
}
