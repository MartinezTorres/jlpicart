#pragma once 

#include "../crt/crt.h"
#include "../util.h"

namespace VDP99X8 {

    typedef struct {
        union {
            uint8_t reg[0x40];
            uint16_t flags;
            struct {
                union {
                    struct {
                        uint8_t extvid : 1;		
                        uint8_t mode2 : 1;
                        uint8_t reserved1 : 6;
                    };
                    struct {
                        uint8_t EV : 1;		
                        uint8_t M3 : 1;
                        uint8_t M4 : 1;
                        uint8_t M5 : 1;
                        uint8_t IE1 : 1;
                        uint8_t IE2 : 1;
                        uint8_t DG : 1;
                        uint8_t reserved5 : 1;
                    };
                };
                union {
                    struct {
                        uint8_t magnifySprites : 1;
                        uint8_t sprites16 : 1;
                        uint8_t reserved2: 1;
                        uint8_t mode3 : 1;
                        uint8_t mode1 : 1;
                        uint8_t generateInterrupts : 1;
                        uint8_t blankScreen : 1;
                        uint8_t mem416K : 1;
                    };
    
                    struct {
                        uint8_t MAG : 1;
                        uint8_t SI : 1;
                        uint8_t reserved6: 1;
                        uint8_t M1 : 1;
                        uint8_t M2 : 1;
                        uint8_t IE0 : 1;
                        uint8_t BL : 1;
                        uint8_t M416K : 1;
                    };
                };
                uint8_t pn10, ct6, pg11, sa7, sg11;
                struct {
                    uint8_t backdrop : 4;
                    uint8_t textcolor : 4;
                };
            };
        };
    } 
    RegisterSet;
    
    typedef struct {
        union {
            uint8_t state;
            struct {
                uint8_t illegalSprite : 5;
                uint8_t C : 1;
                uint8_t _5S : 1;
                uint8_t _int : 1;		
            };
        };
    }
    State;

    extern RegisterSet register_set;
    extern State state;
    extern uint8_t vram[0x4000];
    extern uint32_t vram_ptr;
    
    enum Port1_State { PORT1_STATE0 = 0, PORT1_STATE1 = 1 };
    extern uint8_t port1_data0;
    extern Port1_State port1_state;

    extern uint8_t port0, port1;   

    inline void write_port1(uint8_t data) {

        /*putchar_uart1_nonblocking("sS"[port1_state == PORT1_STATE0]);
        putchar_uart1_nonblocking("0123456789ABCDEF"[(data >> 4 )&0xF]);
        putchar_uart1_nonblocking("0123456789ABCDEF"[(data      )&0xF]);
        putchar_uart1_nonblocking('\n');*/

        if (port1_state == PORT1_STATE0) {
            port1_state = PORT1_STATE1;
            port1_data0 = data;
        } else {
            port1_state = PORT1_STATE0;
            if ( (data&0xC0) == 0x80 ) {
    
                register_set.reg[data & 0x3F] = port1_data0;
            
            } else if ( (data&0xC0) == 0x40 ) {
            
                vram_ptr = port1_data0 + ( uint(data & 0x3F) << 8 );
            }
        }
    }
    
    inline void write_port0(uint8_t data) {
        port1_state = PORT1_STATE0;
        vram_ptr = vram_ptr & 0x3FFF; 
        vram[vram_ptr] = data; 
        vram_ptr++;
    }

    //void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::SCART_HI_240p_60Hz );
    void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::SCART_240p_60Hz );
//    void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::VGA320x480_60Hz );
    

    inline void io_write(uint8_t port, uint8_t data) {
        if (port == port0) write_port0(data);
        if (port == port1) write_port1(data);
    }

    inline void io_read(uint8_t port) {
        if (port == port0) port1_state = PORT1_STATE0;
        if (port == port1) port1_state = PORT1_STATE0;
    }
}
