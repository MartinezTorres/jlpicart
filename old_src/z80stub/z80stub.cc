#include <board.h>
#include <bus/bus.h>
#include "z80stub.h"

#include <initializer_list>

namespace Z80Stub {

    static constexpr const uint8_t rom[] = {
        0x41, 0x42, 0x18, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2a, 0x02, 0x40, 0x7c, 0xb7, 0x28, 0xf9, 0xe9, 0xf3, 0x11, 0x00, 0xe0, 0x21, 0x10, 0x40, 0x01,
        0x08, 0x00, 0xed, 0xb0, 0x11, 0xff, 0x7f, 0x21, 0x30, 0x40, 0xe9, 0xff, 0xff, 0xff, 0xff, 0xff 
    };

    const uint16_t WRITE_MAPPED_ADDRESS = 0x7FFF;
    const uint16_t EXEC_START = 0x4030;

    namespace z80 {
        const uint8_t ld_a_imm     = 0x3e;
        const uint8_t ld_a_address = 0x3a;
        const uint8_t ld_address_a = 0x32;
        const uint8_t ld_hl_imm16  = 0x21;
        const uint8_t jp_hl        = 0xe9;
        const uint8_t ld_de_a      = 0x12;
        const uint8_t in_a_port    = 0xdb;
        const uint8_t out_port_a   = 0xd3;
        const uint8_t ei           = 0xfb;
        const uint8_t di           = 0xf3;
        const uint8_t halt         = 0x76;
        const uint8_t ret          = 0xc9;
        const uint8_t push_hl      = 0xe5;
        const uint8_t pop_hl       = 0xe1;
    };

    const uint8_t *z80stub_bios() { return rom; }

    struct Interface {
        volatile const uint8_t *instructions;
        volatile uint8_t  output;
        volatile bool     active;

        inline uint8_t trigger( std::initializer_list<const uint8_t> inst ) {
            instructions = inst.begin();
            active = true;
            while (active);
            return output;
        }
    };

    static Interface interface;
    static std::pair<bool, uint8_t> Read_Callback(BUS::SubslotIndex, uint32_t bus) {

        uint16_t address      = (bus >> GPIO_A0)  & 0xFFFF;

        if (address < 0x4000) return {true, 0};
        if (address < EXEC_START) return {true, rom[address - 0x4000]};
        if (not interface.active) return {true, z80::jp_hl};
        return {true, interface.instructions[address - EXEC_START]};
    }

    static std::pair<bool, uint8_t> Write_Callback(BUS::SubslotIndex, uint32_t bus) {

        uint16_t address = (bus >> GPIO_A0)  & 0xFFFF;
        uint8_t data     = (bus >> GPIO_D0)  & 0xFF;
        if (address == WRITE_MAPPED_ADDRESS) {
            interface.output = data;
            interface.active = false;
        }
        return {false, 0};
    }

    uint8_t  readb( uint16_t address ) { return interface.trigger({
        z80::ld_a_address, uint8_t(address & 0xFF), uint8_t(address >> 8),
        z80::ld_de_a
    });}

    void     writeb( uint16_t address, uint8_t data ) { interface.trigger({
        z80::ld_a_imm, data,
        z80::ld_address_a, uint8_t(address & 0xFF), uint8_t(address >> 8),
        z80::ld_de_a
    });}

    uint8_t  readio( uint8_t port ) { return interface.trigger({
        z80::in_a_port, port,
        z80::ld_de_a
    });}
    
    void     writeio( uint8_t port, uint8_t data ) { interface.trigger({
        z80::ld_a_imm, data,
        z80::out_port_a, port,
        z80::ld_de_a
    });}

    void     wait_for_frame() { interface.trigger({
        z80::ei, 
        z80::halt, 
        z80::di,
        z80::ld_de_a
    });}

    void     jump_to_trampoline() { interface.trigger({
        z80::ld_hl_imm16, 0x00, 0xe0,
        z80::ld_de_a
    });}

    void     return_control() { interface.trigger({ 
        z80::pop_hl,
        z80::ld_de_a
    });}


    uint8_t keyboard[16], keyboard_trigger[16];
    void     update_keyboard_and_joystick() {

        uint8_t aa = readio(0xAA) & 0xF0;
        for (uint8_t i = 0; i < 9; i++) {
            writeio(0xAA, aa + i); uint8_t k = readio(0xA9);
            keyboard_trigger[i] |= (k & ~keyboard[i]);
            keyboard[i] = k; 
        }
        
        for (uint8_t i = 0; i < 2; i++) {
            writeio(0xA0, 15); writeio(0xA1, (readio(0xA2) & 0x80) | (i==0?0x0F:0x4F) ); 
            writeio(0xA0, 14); uint8_t j = ~readio(0xA2);
            keyboard_trigger[14+i] |= (j & ~keyboard[14+i]);
            keyboard[14+i] = j;
        } 
    }

    uint8_t  getch() {
        return 0;
    }

    bool     check_key_status(uint8_t key_index) {
        return false;
    } 
}
