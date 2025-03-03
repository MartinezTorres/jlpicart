#include <board.h>
#include <bus/bus.h>

namespace BUS {

    const uint8_t *memory_read_addresses[4][8]  __attribute__ ((section (".scratch_y"))) = {0};
    uint8_t *memory_write_addresses[4][8] __attribute__ ((section (".scratch_y"))) = {0};

    MemCallback memory_read_callbacks[4][8]  __attribute__ ((section (".scratch_y"))) = {0};
    MemCallback memory_write_callbacks[4][8] __attribute__ ((section (".scratch_y"))) = {0};

    IOCallback io_read_callbacks[256] = {0};
    IOCallback io_write_callbacks[256] = {0};

    SubslotIndex subslots[4] __attribute__ ((section (".scratch_y"))) = {SUBSLOT0, SUBSLOT0, SUBSLOT0, SUBSLOT0};
    bool is_expanded = false;

    size_t tick_last_irq = 0;

    ResetCallback reset_callback = nullptr;

    void __no_inline_not_in_flash_func(start)() {

        DBG::msg<DEBUG_INFO>("Bus initialization starts");

        save_and_disable_interrupts();

        gpio_put(GPIO_WAIT, false); 

        gpio_set_dir_all_bits(BIT_WAIT);
        if (reset_callback) reset_callback();
        gpio_set_dir_all_bits(0);
        while (( gpio_get_all64() & BIT64_RESET ) == 0 );

        DBG::msg<DEBUG_INFO>("Bus is initialized");
        
        uint32_t ios_old = 0xFFFFFFFFU;
        while (true) {

            // READ BUS
            uint32_t start_tick = systick_hw->cvr;
            uint64_t ios64 = gpio_get_all64();
            uint32_t ios = uint32_t(ios64);

            // RESET CALLBACK
            if ( ( ios64 & BIT64_RESET ) == 0 ) {
                gpio_set_dir_all_bits(BIT_WAIT);
                if (reset_callback) reset_callback();
                gpio_set_dir_all_bits(0);
                do { ios64 = gpio_get_all64(); } while (( ios64 & BIT64_RESET ) == 0 );
                continue;
            }
            
            // UPDATE IRQ REQUEST (useful to sync with screen)
            if ( (ios_old & BIT_INT) and not (ios & BIT_INT) ) tick_last_irq = start_tick;
            ios_old = ios;

            // NORMAL BUS OPERATION (MEMORY AND IO READS AND WRITES)
            constexpr uint32_t operation_mask         = BIT_IORQ + BIT_MERQ + BIT_RD + BIT_WR + BIT_SLTSL;
            constexpr uint32_t operation_memory_read  = operation_mask - BIT_MERQ - BIT_RD - BIT_SLTSL;
            constexpr uint32_t operation_memory_write = operation_mask - BIT_MERQ - BIT_WR - BIT_SLTSL;
            constexpr uint32_t operation_io_read      = operation_mask - BIT_IORQ - BIT_RD;
            constexpr uint32_t operation_io_write     = operation_mask - BIT_IORQ - BIT_WR ; 
            uint32_t operation = ios & operation_mask; 
            
            uint32_t io_port      = (ios >> GPIO_A0)  & 0xFF;
            uint32_t page         = (ios >> GPIO_A14) & 0x03;
            uint32_t data         = (ios >> GPIO_D0)  & 0xFF;
            uint32_t segment8k    = (ios >> GPIO_A13) & 0x07;
            uint32_t displacement = (ios >> GPIO_A0)  & 0x1FFF;
            uint32_t address      = (ios >> GPIO_A0)  & 0xFFFF;

            if ( operation == operation_memory_read ) { 
                
                gpio_set_dir_all_bits(BIT_WAIT);

                uint32_t elapsed_tick_a = start_tick - systick_hw->cvr;

                SubslotIndex subslot_idx = SUBSLOT0;
                if (is_expanded) subslot_idx = subslots[page];
    
                const uint8_t *segment_base_address = memory_read_addresses[subslot_idx][ segment8k ];
                if ( segment_base_address != nullptr ) {
                    data = segment_base_address[ displacement ];
                }

                if ( memory_read_callbacks[segment8k][segment8k] != nullptr ) {
                    auto [active, d] = memory_read_callbacks[subslot_idx][segment8k](subslot_idx, ios);
                    if (active) data = d;
                }

                uint32_t elapsed_tick_b = start_tick - systick_hw->cvr;

                if (is_expanded and address == 0xFFFF) {

                    SubslotRegister r;
                    r.subslot0 = subslots[0];
                    r.subslot1 = subslots[1];
                    r.subslot2 = subslots[2];
                    r.subslot3 = subslots[3];
                    data = (~r.reg) & 0xFF;
                    DBG::msg<DEBUG_INFO>("SUBSLOT READ:", uint16_t(elapsed_tick_a), ":", uint16_t(elapsed_tick_b), ":", uint8_t(data));
                }
                

                if (elapsed_tick_b > 31 and elapsed_tick_b < 100000) { 

                    uint32_t elapsed_tick = start_tick - systick_hw->cvr;
                    DBG::msg<DEBUG_INFO>("R:", uint16_t(elapsed_tick_a), ":", uint16_t(elapsed_tick_b), ":", uint16_t(address), ":", uint8_t(data));
                }
                
                gpio_put_all(data << GPIO_D0);
                gpio_set_dir_all_bits((0xFFU << GPIO_D0) + BIT_BUSDIR);
                while ( gpio_get(GPIO_RD) == false );   
                gpio_set_dir_all_bits(0);

            } else if (operation == operation_memory_write) {

                gpio_set_dir_all_bits(BIT_WAIT);

                SubslotIndex subslot_idx = SUBSLOT0;
                if (is_expanded) {
                    if (address == 0xFFFF) {
                        SubslotRegister r;
                        r.reg = data; 
                        subslots[0] = r.subslot0;
                        subslots[1] = r.subslot1;
                        subslots[2] = r.subslot2;
                        subslots[3] = r.subslot3;
                    }
                    subslot_idx = subslots[page];
                }                
    
                if (memory_write_callbacks[subslot_idx][segment8k] != nullptr) {
                    auto [active, d] = memory_write_callbacks[subslot_idx][segment8k](subslot_idx, ios);
                    if (active) data = d;
                }

                uint8_t *segment_base_address = memory_write_addresses[subslot_idx][ segment8k ];
                if ( segment_base_address != nullptr ) {
                    segment_base_address[ displacement ] = data;
                }

                gpio_set_dir_all_bits(0);
                while ( gpio_get(GPIO_WR) == false );   

            } else if (operation == operation_io_read) { continue;

                if (io_read_callbacks[io_port] != nullptr) {
                    gpio_set_dir_all_bits(BIT_WAIT);
                    auto [active, data] = io_read_callbacks[io_port](ios);
                    if (active) {
                        gpio_put_all(data << GPIO_D0);
                        gpio_set_dir_all_bits((0xFFU << GPIO_D0) + BIT_BUSDIR);
                    }
                }
                while ( gpio_get(GPIO_RD) == false );   
                gpio_set_dir_all_bits(0);

            } else if (operation == operation_io_write) { continue;

                if (io_write_callbacks[io_port] != nullptr) {
                    gpio_set_dir_all_bits(BIT_WAIT);
                    io_write_callbacks[io_port](ios);
                    gpio_set_dir_all_bits(0);
                }

                while ( gpio_get(GPIO_WR) == false );   
            }        
        }
    }    
}
