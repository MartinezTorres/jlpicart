#include <board.h>
#include <bus/bus.h>

//#define LOCAL_DATA 
#define LOCAL_DATA __attribute__ ((section (".scratch_y")))
#pragma GCC push_options
#pragma GCC optimize ("-Os")


namespace BUS {

    const uint8_t *memory_read_addresses[4][8] LOCAL_DATA = {0};
    uint8_t *memory_write_addresses[4][8] LOCAL_DATA = {0};

    MemCallback memory_read_callbacks[4][8] LOCAL_DATA = {0};
    MemCallback memory_write_callbacks[4][8] LOCAL_DATA = {0};

    IOCallback io_read_callbacks[256] = {0};
    IOCallback io_write_callbacks[256] = {0};

    SubslotIndex subslots[4] LOCAL_DATA = {SUBSLOT0, SUBSLOT0, SUBSLOT0, SUBSLOT0};
    bool is_expanded LOCAL_DATA = false;

    size_t tick_last_irq LOCAL_DATA = 0;

    ResetCallback reset_callback LOCAL_DATA = nullptr;

    [[noreturn]] void __no_inline_not_in_flash_func(start)() {

        auto get_bus     = [](){return sio_hw->gpio_in;};
        auto get_bus_hi  = [](){return sio_hw->gpio_hi_in;};
        auto set_bus     = [](uint32_t values){sio_hw->gpio_out = values;};
        auto set_bus_dir = [](uint32_t values){sio_hw->gpio_oe = values;};

        DBG::msg<DEBUG_INFO>("Bus initialization starts");

        save_and_disable_interrupts();

        set_bus_dir(BIT_WAIT);
        if (reset_callback) reset_callback();
        set_bus_dir(0);
        while ( ( get_bus_hi() & (BIT64_RESET>>32) ) == 0 );

        DBG::msg<DEBUG_INFO>("Bus is initialized");
        
        uint32_t bus_old = 0xFFFFFFFFU;
        while (true) {

            // READ BUS
            uint32_t start_tick = systick_hw->cvr;
            uint32_t bus = get_bus();

            // NORMAL BUS OPERATION (MEMORY AND IO READS AND WRITES)
            constexpr uint32_t memory_read_mask  = BIT_MERQ |            BIT_RD |          BIT_SLTSL;
            constexpr uint32_t memory_write_mask = BIT_MERQ |                     BIT_WR | BIT_SLTSL;
            constexpr uint32_t io_read_mask      =            BIT_IORQ | BIT_RD;
            constexpr uint32_t io_write_mask     =            BIT_IORQ |          BIT_WR ; 

            if        ( (bus & memory_read_mask)  == 0 ) { 
                
                set_bus_dir(BIT_WAIT);
                uint32_t elapsed_tick_a = start_tick - systick_hw->cvr;

                //uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;
                uint32_t page         = (bus >> GPIO_A14) & 0x03;
                uint32_t data         = 0;
                uint32_t segment8k    = (bus >> GPIO_A13) & 0x07;
                uint32_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
                uint32_t address      = (bus >> GPIO_A0)  & 0xFFFF;

                SubslotIndex subslot_idx = SUBSLOT0;
                if (is_expanded) subslot_idx = subslots[page];
    
                const uint8_t *segment_base_address = memory_read_addresses[subslot_idx][ segment8k ];
                if ( segment_base_address != nullptr ) {
                    data = segment_base_address[ displacement ];
                }

                if ( memory_read_callbacks[subslot_idx][segment8k] != nullptr ) {
                    auto [active, d] = memory_read_callbacks[subslot_idx][segment8k](subslot_idx, bus);
                    if (active) data = d;
                }

                uint32_t elapsed_tick_b = start_tick - systick_hw->cvr;

                if (is_expanded and address == 0xFFFF) {

                    SubslotRegister r;
                    r.subslots.subslot0 = subslots[0];
                    r.subslots.subslot1 = subslots[1];
                    r.subslots.subslot2 = subslots[2];
                    r.subslots.subslot3 = subslots[3];
                    data = (~r.reg) & 0xFF;
                    DBG::msg<DEBUG_INFO>("SUBSLOT READ:", uint8_t(data));
                }
                

                if (elapsed_tick_b > 0x70 and elapsed_tick_b < 100000) { 

                    DBG::msg<DEBUG_INFO>("LARGE READ DELAY: ", uint16_t(elapsed_tick_a), ":", uint16_t(elapsed_tick_b), " SUBSLOT_IDX:", uint32_t(subslot_idx), " SEGMENT:", segment8k, "  ADDRESS:", uint16_t(address), " DATA:", uint8_t(data));
                }
                
                set_bus(data << GPIO_D0);
                asm volatile("nop\n\t");
                asm volatile("nop\n\t");
                asm volatile("nop\n\t");
                asm volatile("nop\n\t");
                set_bus_dir((0xFFU << GPIO_D0) + BIT_BUSDIR);
                while ( ( get_bus() & BIT_RD ) == false );   
                set_bus_dir(0);

            } else if ( (bus & memory_write_mask) == 0 ) { 

                set_bus_dir(BIT_WAIT);

                //uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;
                uint32_t page         = (bus >> GPIO_A14) & 0x03;
                uint32_t data         = (bus >> GPIO_D0)  & 0xFF;
                uint32_t segment8k    = (bus >> GPIO_A13) & 0x07;
                uint32_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
                uint32_t address      = (bus >> GPIO_A0)  & 0xFFFF;

                SubslotIndex subslot_idx = SUBSLOT0;
                if (is_expanded) {
                    if (address == 0xFFFF) {
                        SubslotRegister r;
                        r.subslots.subslot0 = subslots[0];
                        r.subslots.subslot1 = subslots[1];
                        r.subslots.subslot2 = subslots[2];
                        r.subslots.subslot3 = subslots[3];
                        DBG::msg<DEBUG_INFO>("SUBSLOT WRITE:", uint8_t(r.reg), "->", uint8_t(data));
                        r.reg = data; 
                        subslots[0] = r.subslots.subslot0;
                        subslots[1] = r.subslots.subslot1;
                        subslots[2] = r.subslots.subslot2;
                        subslots[3] = r.subslots.subslot3;
                        DBG::msg<DEBUG_INFO>("PAGE: 0 is assigned to SUBSLOT: ", uint8_t(subslots[0]));
                        DBG::msg<DEBUG_INFO>("PAGE: 1 is assigned to SUBSLOT: ", uint8_t(subslots[1]));
                        DBG::msg<DEBUG_INFO>("PAGE: 2 is assigned to SUBSLOT: ", uint8_t(subslots[2]));
                        DBG::msg<DEBUG_INFO>("PAGE: 3 is assigned to SUBSLOT: ", uint8_t(subslots[3]));
                    }
                    subslot_idx = subslots[page];
                }                
    
                if (memory_write_callbacks[subslot_idx][segment8k] != nullptr) {
                    auto [active, d] = memory_write_callbacks[subslot_idx][segment8k](subslot_idx, bus);
                    if (active) data = d;
                }

                uint8_t *segment_base_address = memory_write_addresses[subslot_idx][ segment8k ];
                if ( segment_base_address != nullptr ) {
                    segment_base_address[ displacement ] = data;
                }

                set_bus_dir(0);
                while ( ( get_bus() & BIT_WR ) == false );     

            } else if ( (bus & io_read_mask)      == 0 ) { 

                uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;

                if (io_read_callbacks[io_port] != nullptr) {
                    set_bus_dir(BIT_WAIT);
                    auto [active, data] = io_read_callbacks[io_port](bus);
                    if (active) {
                        set_bus( uint32_t(data) << GPIO_D0);
                        set_bus_dir((0xFFU << GPIO_D0) + BIT_BUSDIR);
                    } else {
                        set_bus_dir(0);
                    }
                }
                while ( ( get_bus() & BIT_RD ) == false );   
                set_bus_dir(0);

            } else if ( (bus & io_write_mask)     == 0 ) { 

                uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;

                if (io_write_callbacks[io_port] != nullptr) {
                    set_bus_dir(BIT_WAIT);
                    io_write_callbacks[io_port](bus);
                    set_bus_dir(0);
                }

                while ( ( get_bus() & BIT_WR ) == false );     
            } else {

                // RESET CALLBACK
                if ( ( get_bus_hi() & (BIT64_RESET>>32) ) == 0 ) {
                    set_bus_dir(BIT_WAIT);
                    if (reset_callback) reset_callback();
                    set_bus_dir(0);
                    while ( ( get_bus_hi() & (BIT64_RESET>>32) ) == 0 );
                    continue;
                }
                
                // UPDATE IRQ REQUEST (useful to sync with screen)
                if ( (bus_old & BIT_INT) and not (bus & BIT_INT) ) tick_last_irq = start_tick;
                bus_old = bus;
            }      
        }
    }    
}
#pragma GCC pop_options
