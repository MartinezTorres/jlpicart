#pragma once

namespace Z80Stub {

    constexpr static size_t EXEC_START = 0x30;
        
    std::pair<bool, uint8_t> Z80_Stub_Read_Callback(BUS::SubslotIndex, uint32_t bus) {

        uint16_t address      = (bus >> GPIO_A0)  & 0x3FFF;
        if (z80_stub_active or (address < 0x30) ) return {true, stub[address]};
        return {true, z80::jp_hl};        
    }

    static std::pair<bool, uint8_t> Z80_Stub_Write_Callback(BUS::SubslotIndex, uint32_t bus) {

        uint16_t address = (bus >> GPIO_A0)  & 0xFFFF;
        uint8_t data     = (bus >> GPIO_D0)  & 0xFF;
        if (address == WRITE_MAPPED_ADDRESS) {
            interface.output = data;
            interface.active = false;
        }
        return {false, 0};
    }


    const uint8_t *z80stub_bios();

    uint8_t  readb( uint16_t address );

    void     writeb( uint16_t address, uint8_t data );

    uint8_t  readio( uint8_t port );
    void     writeio( uint8_t port, uint8_t data );

    void     wait_for_frame();
    void     jump_to_trampoline();
    void     return_control();
    
    uint8_t  getch();

    void     update_keyboard_and_joystick();
    bool     check_key_status(uint8_t key_index);  
}