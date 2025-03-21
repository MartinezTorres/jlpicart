#include <board.h>
#include "cartridge_type.h"
#include "bus.h"

static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(KONAMI_Callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = ((bus >> GPIO_A0) / 0x2000) % 8; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[page] = &cartridge.rom_base[segment * 8 * 1024];
    return {false, 0};
}

static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(ASCII8_Callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = (bus >> GPIO_A0) / 0x800 % 4; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[2 + page] = &cartridge.rom_base[segment * 8 * 1024];
    return {false, 0};
}

static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(ASCII16_Callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = (bus >> GPIO_A0) / 0x1000 % 2; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[2 + 2 * page + 0] = &cartridge.rom_base[ (2 * segment + 0) * 8 * 1024];
    cartridge.memory_read_addresses[2 + 2 * page + 1] = &cartridge.rom_base[ (2 * segment + 1) * 8 * 1024];
    return {false, 0};
}

static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(Z80_Stub_Read_Callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {

    uint16_t address      = (bus >> GPIO_A0)  & 0x1FFF;
    if (cartridge.ram_base[0x10] or (address < 0x30) ) return {true, cartridge.ram_base[address]};
    return {true, 0xe9}; //z80::jp_hl        
}

static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(Z80_Stub_Write_Callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {

    uint16_t address = (bus >> GPIO_A0)  & 0x1FFF;
    uint8_t data     = (bus >> GPIO_D0)  & 0xFF;
    if (address == 0x1FFF) {
        cartridge.ram_base[0x11] = data;
        cartridge.ram_base[0x10] = 0;
    }
    return {false, 0};
}

Cartridge::Cartridge(CartridgeType cartridge_type, const uint8_t *rom_base = nullptr, uint8_t *ram_base = nullptr) : rom_base(rom_base), ram_base(ram_base) {

    if ( cartridge_type == SUBSLOT_DISABLED ) {
    
    } else if ( cartridge_type == SUBSLOT_RAM ) {
        for (int i = 0; i < 8; i++) {
            memory_read_addresses[i] = &ram_base[i * 8 * 1024];
            memory_write_addresses[i] = &ram_base[i * 8 * 1024];
        }

    } else if ( cartridge_type == Z80_STUB ) {
        memory_read_addresses[2] = &rom_base[0];
        memory_write_addresses[2] = &ram_base[0];
        memory_read_callbacks[2] = Z80_Stub_Read_Callback;
        memory_write_callbacks[3] = Z80_Stub_Write_Callback;

    } else if ( cartridge_type == MAPPER_LINEAR ) {
        for (int i = 0; i < 8; i++) {
            memory_read_addresses[i] = &rom_base[i * 8 * 1024];
        }

    } else if ( cartridge_type == MAPPER_32K_MIRRORED ) {
        for (int i = 0; i < 8; i++) {
            memory_read_addresses[i] = &rom_base[((i+2)%4) * 8 * 1024];
        }

    } else if ( cartridge_type == MAPPER_KONAMI ) {    
        for (int i = 0; i < 4; i++) {
            memory_read_addresses[2+i] = &rom_base[i * 8 * 1024];
            memory_write_callbacks[2+i] = KONAMI_Callback;
        }

    } else if ( cartridge_type == MAPPER_KONAMI_Z ) {  
        for (int i = 0; i < 4; i++) {
            memory_read_addresses[2+i] = &rom_base[i * 8 * 1024];
        }
        for (int i = 2; i < 4; i++) {
            memory_write_callbacks[2+i] = KONAMI_Callback;
        }

    } else if ( cartridge_type == MAPPER_ASCII8 ) {    
        for (int i = 0; i < 8; i++) 
            memory_read_addresses[i] = &rom_base[0 * 8 * 1024];
        memory_write_callbacks[3] = ASCII8_Callback;

    } else if ( cartridge_type == MAPPER_ASCII16 ) {
        for (int i = 0; i < 8; i++) 
            memory_read_addresses[i] = &rom_base[(i%2) * 8 * 1024];
        memory_write_callbacks[3] = ASCII16_Callback;
    }
}
