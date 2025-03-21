#pragma once

#include <board.h>

struct RAM : CartridgeBase { RAM(uint8_t *ram_base_ ) : CartridgeBase("RAM") { 
    ram_base = ram_base_; 
    for (int i = 0; i < 8; i++) {
        memory_read_addresses[i]  = &ram_base[i * 8 * 1024];
        memory_write_addresses[i] = &ram_base[i * 8 * 1024];
    }
} };

struct Z80_STUB : CartridgeBase { 
    
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
Z80_STUB() : CartridgeBase("Z80_STUB") { 
    memory_read_callbacks[2] = Z80_Stub_Read_Callback;
    memory_write_callbacks[3] = Z80_Stub_Write_Callback;
} };

struct ROM : CartridgeBase { ROM(uint8_t *rom_base_ ) : CartridgeBase("ROM") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 8; i++) {
        memory_read_addresses[i]  = &rom_base[i * 8 * 1024];
    }
} };


struct ROM_32K_MIRRORED : CartridgeBase { ROM_32K_MIRRORED(uint8_t *rom_base_ ) : CartridgeBase("ROM_32K_MIRRORED") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 8; i++) {
        memory_read_addresses[i] = &rom_base[((i+2)%4) * 8 * 1024];
    }
} };


struct ROM_KONAMI : CartridgeBase { ROM_KONAMI(uint8_t *rom_base_ ) : CartridgeBase("ROM_KONAMI") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 4; i++) {
        memory_read_addresses[2+i] = &rom_base[i * 8 * 1024];
        memory_write_callbacks[2+i] = callback;
    }
} 
static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = ((bus >> GPIO_A0) / 0x2000) % 8; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[page] = &cartridge.rom_base[segment * 8 * 1024];
    return {false, 0};
}
};

struct ROM_KONAMI_Z : CartridgeBase { ROM_KONAMI_Z(uint8_t *rom_base_ ) : CartridgeBase("ROM_KONAMI_Z") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 4; i++) {
        memory_read_addresses[2+i] = &rom_base[i * 8 * 1024];
    }
    for (int i = 2; i < 4; i++) {
        memory_write_callbacks[2+i] = ROM_KONAMI::callback;
    }
} };

struct ROM_ASCII8 : CartridgeBase { ROM_ASCII8(uint8_t *rom_base_ ) : CartridgeBase("ROM_ASCII8") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 8; i++) 
        memory_read_addresses[i] = &rom_base[0 * 8 * 1024];
    memory_write_callbacks[3] = callback;
} 
static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = (bus >> GPIO_A0) / 0x800 % 4; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[2 + page] = &cartridge.rom_base[segment * 8 * 1024];
    return {false, 0};
}
};


struct ROM_ASCII16 : CartridgeBase { ROM_ASCII16(uint8_t *rom_base_ ) : CartridgeBase("ROM_ASCII16") { 
    rom_base = rom_base_; 
    for (int i = 0; i < 8; i++) 
        memory_read_addresses[i] = &rom_base[(i%2) * 8 * 1024];
    memory_write_callbacks[3] = callback;
} 
static std::pair<bool, uint8_t> __no_inline_not_in_flash_func(callback)(Cartridge &cartridge, BUS::GPIOBus32 bus) {
    uint32_t page = (bus >> GPIO_A0) / 0x1000 % 2; 
    uint32_t segment = (bus >> GPIO_D0)  & 0xFF;
    cartridge.memory_read_addresses[2 + 2 * page + 0] = &cartridge.rom_base[ (2 * segment + 0) * 8 * 1024];
    cartridge.memory_read_addresses[2 + 2 * page + 1] = &cartridge.rom_base[ (2 * segment + 1) * 8 * 1024];
    return {false, 0};
}
};


