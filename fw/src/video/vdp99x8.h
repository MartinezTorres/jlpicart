#pragma once 

#include "crt.h"


namespace VDP99X8 {

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wpedantic"
    
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

    #pragma GCC diagnostic pop

    extern RegisterSet register_set;
    extern State state;
    extern uint8_t vram[0x4000];
    extern uint32_t vram_ptr;
    
    //void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::SCART_HI_240p_60Hz );
    //void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::SCART_240p_60Hz );
    void init(uint8_t port0_ = 0x98, uint8_t port1_ = 0x99, const CRT::CRT_Type &type = CRT::VGA320x480_60Hz );
    

}

