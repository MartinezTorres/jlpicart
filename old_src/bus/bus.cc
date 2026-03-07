#include <board.h>
#include <bus/bus.h>

//#define LOCAL_DATA 
#define LOCAL_DATA __attribute__ ((section (".scratch_y")))
#pragma GCC push_options
#pragma GCC optimize ("-Os")


namespace BUS {

    union SubslotRegister {
        uint8_t reg; 
        struct {
            SubslotIndex subslot0 : 2;
            SubslotIndex subslot1 : 2;
            SubslotIndex subslot2 : 2;
            SubslotIndex subslot3 : 2;
        } subslots;

    };

    Cartridge cartridges[8]; // cartridge slots 4-7 are only valid for IO
    SubslotIndex subslot_indexes[4] LOCAL_DATA = {SUBSLOT0, SUBSLOT0, SUBSLOT0, SUBSLOT0};
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
        

        // NORMAL BUS OPERATION (MEMORY AND IO READS AND WRITES)
        constexpr uint32_t memory_read_mask  = BIT_MERQ |            BIT_RD |          BIT_SLTSL;
        constexpr uint32_t memory_write_mask = BIT_MERQ |                     BIT_WR | BIT_SLTSL;
        constexpr uint32_t io_read_mask      =            BIT_IORQ | BIT_RD;
        constexpr uint32_t io_write_mask     =            BIT_IORQ |          BIT_WR ; 

        uint32_t start_tick = systick_hw->cvr;
        uint32_t bus_old = 0xFFFFFFFFU;
        uint32_t bus = get_bus();

        auto memory_read = [&](){

            set_bus_dir(BIT_WAIT);

            uint32_t elapsed_tick_a = start_tick - systick_hw->cvr;

            //uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;
            uint32_t page         = (bus >> GPIO_A14) & 0x03;
            uint32_t data         = 0;
            uint32_t segment8k    = (bus >> GPIO_A13) & 0x07;
            uint32_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
            uint32_t address      = (bus >> GPIO_A0)  & 0xFFFF;

            Cartridge &cartridge = cartridges[is_expanded?subslot_indexes[page]:SUBSLOT0];

            if (is_expanded and address == 0xFFFF) {

                SubslotRegister r;
                r.subslots.subslot0 = subslot_indexes[0];
                r.subslots.subslot1 = subslot_indexes[1];
                r.subslots.subslot2 = subslot_indexes[2];
                r.subslots.subslot3 = subslot_indexes[3];
                data = (~r.reg) & 0xFF;
                DBG::msg<DEBUG_INFO>("SUBSLOT READ:", uint8_t(data));

            } else {

                const uint8_t *segment_memory_read_address = cartridge.memory_read_addresses[ segment8k ];
                if ( segment_memory_read_address != nullptr ) {
                    data = segment_memory_read_address[ displacement ];
                }

                auto segment_read_callback = cartridge.memory_read_callbacks[segment8k];
                if ( segment_read_callback != nullptr ) {
                    auto [active, d] = segment_read_callback(cartridge, bus);
                    if (active) data = d;
                }
            }

            uint32_t elapsed_tick_b = start_tick - systick_hw->cvr;

            if (elapsed_tick_b > 300 and elapsed_tick_b < 100000) { 

                DBG::msg<DEBUG_INFO>("LARGE READ DELAY: ", int(elapsed_tick_a), " ", int(elapsed_tick_b), " SUBSLOT_IDX:", int(subslot_indexes[page]), " SEGMENT:", segment8k, "  ADDRESS:", uint16_t(address), " DATA:", uint8_t(data));
            }

            set_bus(data << GPIO_D0);
            set_bus_dir((0xFFU << GPIO_D0) + BIT_BUSDIR);
            while ( ( get_bus() & BIT_RD ) == false );   
            set_bus_dir(BIT_WAIT);
        };

        auto memory_write = [&](){

            set_bus_dir(BIT_WAIT);

            uint32_t page         = (bus >> GPIO_A14) & 0x03;
            uint32_t data         = (bus >> GPIO_D0)  & 0xFF;
            uint32_t segment8k    = (bus >> GPIO_A13) & 0x07;
            uint32_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
            uint32_t address      = (bus >> GPIO_A0)  & 0xFFFF;

            Cartridge &cartridge = cartridges[is_expanded?subslot_indexes[page]:SUBSLOT0];

            if (is_expanded and address == 0xFFFF) {

                SubslotRegister r;
                r.subslots.subslot0 = subslot_indexes[0];
                r.subslots.subslot1 = subslot_indexes[1];
                r.subslots.subslot2 = subslot_indexes[2];
                r.subslots.subslot3 = subslot_indexes[3];
                DBG::msg<DEBUG_INFO>("SUBSLOT WRITE:", uint8_t(r.reg), "->", uint8_t(data));
                r.reg = data; 
                subslot_indexes[0] = r.subslots.subslot0;
                subslot_indexes[1] = r.subslots.subslot1;
                subslot_indexes[2] = r.subslots.subslot2;
                subslot_indexes[3] = r.subslots.subslot3;
                DBG::msg<DEBUG_INFO>("PAGE: 0 is assigned to SUBSLOT: ", uint8_t(subslot_indexes[0]));
                DBG::msg<DEBUG_INFO>("PAGE: 1 is assigned to SUBSLOT: ", uint8_t(subslot_indexes[1]));
                DBG::msg<DEBUG_INFO>("PAGE: 2 is assigned to SUBSLOT: ", uint8_t(subslot_indexes[2]));
                DBG::msg<DEBUG_INFO>("PAGE: 3 is assigned to SUBSLOT: ", uint8_t(subslot_indexes[3]));

            } else {
                
                auto segment_memory_write_callback = cartridge.memory_write_callbacks[segment8k];
                if (segment_memory_write_callback != nullptr) {
                    auto [active, d] = segment_memory_write_callback(cartridge, bus);
                    if (active) data = d;
                }

                uint8_t *segment_memory_write_address = cartridge.memory_write_addresses[ segment8k ];
                if ( segment_memory_write_address != nullptr ) {
                    segment_memory_write_address[ displacement ] = data;
                }
            }

            while ( ( get_bus() & BIT_WR ) == false );     
        };

        auto io_read = [&](){

            set_bus_dir(BIT_WAIT);

            uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;

            bool write_enable = false;
            uint8_t data = 0x00;
            for (size_t i = 0; i < sizeof(cartridges)/sizeof(cartridges[0]); i++ ) {
                auto io_read_callback = cartridges[i].io_read_callbacks[io_port];
                if (io_read_callback != nullptr) {
                    auto [active, d] = io_read_callback(cartridges[i], bus);
                    if (active) {
                        write_enable = true;
                        data = d;
                    }
                }
            }

            if (write_enable) {
                set_bus( uint32_t(data) << GPIO_D0);
                set_bus_dir((0xFFU << GPIO_D0) + BIT_BUSDIR);
            } else {
                set_bus_dir(0);
            }

            while ( ( get_bus() & BIT_RD ) == false );   
            set_bus_dir(0);
        };

        auto io_write = [&](){

            set_bus_dir(BIT_WAIT);

            uint32_t io_port      = (bus >> GPIO_A0)  & 0xFF;

            for (size_t i = 0; i < sizeof(cartridges)/sizeof(cartridges[0]); i++ ) {
                auto io_write_callback = cartridges[i].io_write_callbacks[io_port];
                if (io_write_callback != nullptr) {
                    io_write_callback(cartridges[i], bus);
                }
            }

            set_bus_dir(0);
            while ( ( get_bus() & BIT_WR ) == false );
        };

        auto irq_request = [&](){
            if ( (bus_old & BIT_INT) and not (bus & BIT_INT) ) tick_last_irq = start_tick;
            bus_old = bus;
        };

        auto reset_request = [&](){
            if ( ( get_bus_hi() & (BIT64_RESET>>32) ) == 0 ) {
                set_bus_dir(BIT_WAIT);
                if (reset_callback) reset_callback();
                set_bus_dir(0);
                while ( ( get_bus_hi() & (BIT64_RESET>>32) ) == 0 );
            }
        };

        while (true) {

            start_tick = systick_hw->cvr;
            bus = get_bus();

            if        ( (bus & memory_read_mask)  == 0 ) { memory_read();  // Trigger before WAIT, should always leave without WAIT
            } else if ( (bus & memory_write_mask) == 0 ) { memory_write(); // Trigger AFTER WAIT, should always leave WITH WAIT
            } else if ( (bus & io_read_mask)      == 0 ) { io_read();      // Trigger before WAIT, should always leave without WAIT              
            } else if ( (bus & io_write_mask)     == 0 ) { io_write();     // Trigger before WAIT, should always leave without WAIT 
            } else { 
                reset_request(); 
                irq_request(); 
                set_bus_dir(0);
            }
        }
    }    
}
#pragma GCC pop_options
