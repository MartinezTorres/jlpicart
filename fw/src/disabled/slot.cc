#include <board.h>
#include <config/config.h>
#include <bus/bus.h>
#include <bus/slot.h>

using namespace Config;

namespace BUS {

    static Cartridge cartridges[4];

    static std::pair<bool, uint8_t> KONAMI_Callback(SubslotIndex subslot_idx, uint32_t bus) {
        uint32_t page = ((bus >> GPIO_A0) / 0x2000) % 8; 
        uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
        auto rom = cartridges[subslot_idx].rom_base;
        memory_read_addresses[subslot_idx][page] = &rom[segment * 8 * 1024];
        return {false, 0};
    }

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

    static std::pair<bool, uint8_t> DEBUG_Read_Callback(SubslotIndex subslot_idx, uint32_t bus) {

        uint8_t segment8k    = (bus >> GPIO_A13) & 0x07;
        uint16_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
        uint16_t address      = (bus >> GPIO_A0)  & 0xFFFF;

        const uint8_t *segment_base_address = memory_read_addresses[subslot_idx][ segment8k ];

        if (address&0xFFF) return {false, 0};
        if (segment_base_address == nullptr) return {false, 0};

        uint8_t data = segment_base_address[ displacement ]; 
        
        DBG::msg<DEBUG_INFO>("DEBUG READ   SI:", uint8_t(subslot_idx)," A:", address, " D:", data);
        return {false, 0};
    }

    static std::pair<bool, uint8_t> DEBUG_Write_Callback(SubslotIndex subslot_idx, uint32_t bus) {

        uint8_t segment8k    = (bus >> GPIO_A13) & 0x07;
        uint16_t displacement = (bus >> GPIO_A0)  & 0x1FFF;
        uint16_t address      = (bus >> GPIO_A0)  & 0xFFFF;
        uint8_t data     = (bus >> GPIO_D0)  & 0xFF;

        const uint8_t *segment_base_address = memory_write_addresses[subslot_idx][ segment8k ];

        if (address&0xFFF) return {false, 0};
        if (segment_base_address == nullptr) return {false, 0};

        uint8_t old_data = segment_base_address[ displacement ];      
        DBG::msg<DEBUG_INFO>("DEBUG WRITE  SI:", uint8_t(subslot_idx)," A:",uint16_t(address), " D:", old_data, "->", data);
        return {false, 0};
    }


    void init_subslot(const Cartridge &cartridge, SubslotIndex subslot_idx) {

        DBG::msg<DEBUG_INFO>("Init Subslot: ", uint8_t(subslot_idx), " ", uint32_t(cartridge.cartridge_type), " \"", to_cstring(cartridge.cartridge_type),  " \"", " ROM:", uint32_t(cartridge.rom_base)," RAM:", uint32_t(cartridge.ram_base)  );

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
                for (int i = 0; i < 8; i++) {
                    memory_read_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];
                    memory_write_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];
                }
                break;

            case SUBSLOT_RAM_DEBUG:
                for (int i = 0; i < 8; i++) {
                    memory_read_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];
                    memory_write_addresses[subslot_idx][i] = &cartridge.ram_base[i * 8 * 1024];                
                    memory_read_callbacks[subslot_idx][i] = DEBUG_Read_Callback;
                    memory_write_callbacks[subslot_idx][i] = DEBUG_Write_Callback;
                }
                break;
            case Z80_STUB:
                memory_read_addresses[subslot_idx][2] = &cartridge.rom_base[0];
                memory_write_addresses[subslot_idx][2] = &cartridge.ram_base[0];
                memory_read_callbacks[subslot_idx][2] = Z80_Stub_Read_Callback;
                memory_write_callbacks[subslot_idx][2] = Z80_Stub_Write_Callback;
                break;
                
            case MAPPER_LINEAR:
                for (int i = 0; i < 8; i++) {
                    memory_read_addresses[subslot_idx][i] = &cartridge.rom_base[i * 8 * 1024];
                    memory_read_callbacks[subslot_idx][i] = DEBUG_Read_Callback;
                }
                break;
            case MAPPER_32K_MIRRORED:
                for (int i = 2; i < 6; i++) {
                    memory_read_addresses[subslot_idx][i] = &cartridge.rom_base[((i+2)%4) * 8 * 1024];
                }
                break;
            case MAPPER_KONAMI:
                for (int i = 0; i < 4; i++) {
                    memory_read_addresses[subslot_idx][2+i] = &cartridge.rom_base[i * 8 * 1024];
                    memory_write_callbacks[subslot_idx][2+i] = KONAMI_Callback;
                }
                break;
            case MAPPER_KONAMI_Z:
                for (int i = 0; i < 4; i++) {
                    memory_read_addresses[subslot_idx][2+i] = &cartridge.rom_base[i * 8 * 1024];
                }
                for (int i = 2; i < 4; i++) {
                    memory_write_callbacks[subslot_idx][2+i] = KONAMI_Callback;
                }
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