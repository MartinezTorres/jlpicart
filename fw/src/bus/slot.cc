#include <board.h>
#include <config/config.h>
#include <bus/bus.h>
#include <bus/slot.h>


namespace BUS {

    static Cartridge cartridges[4];

    static std::pair<bool, uint8_t> ASCII8_Callback(SubslotIndex subslot_idx, uint32_t bus) {
        uint32_t page = (bus >> GPIO_A0) / 0x800 % 4; 
        uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
        auto rom = cartridges[subslot_idx].rom_base;
        memory_read_addresses[subslot_idx][2 + page] = &rom[segment * 8 * 1024];
        return {false, 0};
    }

    static std::pair<bool, uint8_t> ASCII16_Callback(SubslotIndex subslot_idx, uint32_t bus) {
        uint32_t page = (bus >> GPIO_A0) / 0x1000 % 2; 
        uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
        auto rom = cartridges[subslot_idx].rom_base;
        memory_read_addresses[subslot_idx][2 + 2 * page + 0] = &rom[ (2 * segment + 0) * 8 * 1024];
        memory_read_addresses[subslot_idx][2 + 2 * page + 1] = &rom[ (2 * segment + 1) * 8 * 1024];
        return {false, 0};
    }

    static std::pair<bool, uint8_t> RAM_DEBUG_Read_Callback(SubslotIndex subslot_idx, uint32_t bus) {

        uint8_t segment8k    = (bus >> GPIO_A13) & 0x07;
        uint16_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
        uint16_t address      = (bus >> GPIO_A0)  & 0xFFFF;

        const uint8_t *segment_base_address = memory_read_addresses[subslot_idx][ segment8k ];
        uint8_t data = segment_base_address[ displacement ];
        
        DBG::msg<DEBUG_INFO>("MR:", uint8_t(subslot_idx),":", address, ":",segment8k, ":", data);
        return {false, 0};
    }

    static std::pair<bool, uint8_t> RAM_DEBUG_Write_Callback(SubslotIndex subslot_idx, uint32_t bus) {
        uint8_t segment8k    = (bus >> GPIO_A13) & 0x07;
        uint16_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
        uint16_t address      = (bus >> GPIO_A0)  & 0xFFFF;
        uint8_t data     = (bus >> GPIO_D0)  & 0xFF;

        const uint8_t *segment_base_address = memory_write_addresses[subslot_idx][ segment8k ];
        uint8_t old_data = segment_base_address[ displacement ];      
        DBG::msg<DEBUG_INFO>("MW:", uint8_t(subslot_idx),":",uint16_t(address), ":", old_data, "->", data);
        return {false, 0};
    }


    void init_subslot(const Cartridge &cartridge, SubslotIndex subslot_idx) {

        DBG::msg<DEBUG_INFO>("Init Subslot: ", uint8_t(subslot_idx), " ", uint32_t(cartridge.cartridge_type), " \"", to_cstring(cartridge.cartridge_type),  " \"" );

        cartridges[subslot_idx] = cartridge;

        for (int i = 0; i < 8; i++) {
            memory_read_addresses[subslot_idx][i] = nullptr;
            memory_write_addresses[subslot_idx][i] = nullptr;
            memory_read_callbacks[subslot_idx][i] = nullptr;
            memory_write_callbacks[subslot_idx][i] = nullptr;
        }

        switch (cartridge.cartridge_type) {
            case SUBSLOT_DISABLED:
                break;
            case SUBSLOT_RAM:
                for (int i = 0; i < 8; i++) 
                    memory_read_addresses[subslot_idx][i] = 
                    memory_write_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];                
                break;

            case SUBSLOT_RAM_DEBUG:
                for (int i = 0; i < 8; i++) {
                    memory_read_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];
                    memory_write_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];                
                    memory_read_callbacks[subslot_idx][i] = RAM_DEBUG_Read_Callback;
                    memory_write_callbacks[subslot_idx][i] = RAM_DEBUG_Write_Callback;
                }
                break;
            case MAPPER_LINEAR:
                for (int i = 0; i < 8; i++) 
                    memory_read_addresses[subslot_idx][i] = &cartridge.rom_base[i * 8 * 1024];
                break;
            case MAPPER_32K_MIRRORED:
                memory_read_addresses[subslot_idx][2] = &cartridge.rom_base[2 * 8 * 1024];
                memory_read_addresses[subslot_idx][3] = &cartridge.rom_base[3 * 8 * 1024];
                memory_read_addresses[subslot_idx][4] = &cartridge.rom_base[0 * 8 * 1024];
                memory_read_addresses[subslot_idx][5] = &cartridge.rom_base[1 * 8 * 1024];
                break;
            case MAPPER_ASCII8:
                for (int i = 0; i < 8; i++) 
                    memory_read_addresses[subslot_idx][i] = &cartridge.rom_base[0 * 8 * 1024];
                    memory_write_callbacks[subslot_idx][3] = ASCII8_Callback;
                break;
            case MAPPER_ASCII16:
                for (int i = 0; i < 8; i++) 
                    memory_read_addresses[subslot_idx][i] = &cartridge.rom_base[(i%2) * 8 * 1024];
                memory_write_callbacks[subslot_idx][3] = ASCII16_Callback;
                break;
        }
    }
}