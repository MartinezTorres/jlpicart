#include "tms9918i.h"
#include <bus/bus.h>

struct TMS9918I_PIMPL {
   
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

    RegisterSet register_set = {};
    State state = {};
    uint8_t vram[0x4000];
    uint32_t vram_ptr;

    size_t line_buffer_index = 0;
    size_t screen_buffer_index = 0;
    uint8_t frame_buffer[2][192][256];

    size_t visible_area_size;
    uint8_t vdp_linebuffer[CRT::MAX_HORIZONTAL_RESOLUTION];

    uint8_t old_pn10;

    static constexpr size_t MAX_SPRITES_PER_LINE_MODE2 = 4;
    static constexpr size_t N_SPRITES = 32;
    static constexpr size_t TILE_WIDTH = 32;
    static constexpr size_t TILE_HEIGHT = 24;

    using U8x8 = uint8_t[8];

    struct EM2_Sprite {
        uint8_t y,x;
        uint8_t pattern;
        uint8_t color;
    };

    using T_PN = uint8_t[TILE_HEIGHT][TILE_WIDTH]; // Pattern Name Table
    using T_CT = U8x8[3][256]; // Pattern Color Table
    using T_PG = U8x8[3][256]; // Pattern Generator Table
    using T_SA = EM2_Sprite[N_SPRITES]; // Sprite Attribute Table
    using T_SG = U8x8[256]; // Sprite Generator Table


    void drawBlankLine(size_t linebuffer_size, uint8_t *linebuffer) {
        for (size_t i=0; i<linebuffer_size; i++) linebuffer[i] = register_set.backdrop;
    }

    template<bool CLEAR>
    void drawM2_tiles( int active_line_idx ) {
        
        const T_PN &PN = *(const T_PN *)&vram[uint16_t(register_set.pn10) << 10];
        const T_CT &CT = *(const T_CT *)&vram[uint16_t(register_set.ct6&0x80) << 6];
        const T_PG &PG = *(const T_PG *)&vram[uint16_t(register_set.pg11&0xFC) << 11];
        
        uint8_t pnMask = register_set.pg11 & 3;
        uint8_t ctMask1 = (register_set.ct6>>5) & 3;
        uint8_t ctMask2 = ((register_set.ct6&31)<<3)+7;

        // TILES
        {
            int lb = active_line_idx >> 6 ;
            int lt = active_line_idx >> 3 ;
            int l  = active_line_idx & 7;
        
            uint8_t *pixel = &frame_buffer[!screen_buffer_index][active_line_idx][0];
            for (int j=0; j<32; j++) {
                uint8_t p = PG[lb&pnMask][PN[lt][j]][l];
                uint8_t c = CT[lb&ctMask1][PN[lt][j]&ctMask2][l];
                for (int jj=0; jj<8; jj++) {
                    if (p&128) {
                        if (c>>4) {
                            if (CLEAR) {
                                *pixel++ = (c>>4);
                            } else {
                                *pixel++ |= (c & 0xF0);
                            }
                        } else {
                            if (CLEAR) {
                                *pixel++ = register_set.backdrop;
                            } else {
                                *pixel++ |= (register_set.backdrop << 4);
                            }
                        }
                    } else {
                        if (c & 0xF) {
                            if (CLEAR) {
                                *pixel++ = (c&0xF);
                            } else {
                                *pixel++ |= (c<<4);
                            }
                        } else {
                            if (CLEAR) {
                                *pixel++ = register_set.backdrop;
                            } else {
                                *pixel++ |= (register_set.backdrop << 4);
                            }
                        }
                    }
                    p = (p << 1);
                }
            }
        }
    }

    template<bool CLEAR>
    void drawM2_sprites( int active_line_idx ) {

        const T_SA &SA = *(const T_SA *)&vram[(uint16_t)(register_set.sa7 )<< 7];
        const T_SG &SG = *(const T_SG *)&vram[(uint16_t)(register_set.sg11)<<11];

        state._5S = 0;
        state.illegalSprite = 31;
            
        int maxSprite = N_SPRITES;
        if (active_line_idx > 192 - 16) {
            int nShownSprites = 0;

            for (maxSprite = 0; maxSprite < N_SPRITES; maxSprite++) {

                uint8_t spriteLine = (active_line_idx - SA[maxSprite].y - 1 ) >> register_set.magnifySprites;
                
                if (spriteLine >= 8 * (1 + register_set.sprites16)) continue;
                
                if (SA[maxSprite].y == 208) {  break; };
                if (nShownSprites == MAX_SPRITES_PER_LINE_MODE2) { 
                    if (state._5S == 0) {
                        state.illegalSprite = maxSprite; 
                        state._5S = 1; 
                    }
                    break; 
                }
                nShownSprites++;
            }
        }

        for (int j=maxSprite-1; j>=0; j--) {

            uint8_t spriteLine = (active_line_idx - SA[j].y - 1) >> register_set.magnifySprites;
            
            if ( spriteLine >= 8 * ( 1 + register_set.sprites16 ) ) continue;
            
            uint8_t pattern = SA[j].pattern;			
            if ( register_set.sprites16 ) pattern = ( pattern & 252 ) + !!( spriteLine > 7 );

            //int y = active_line_idx;
            int x = SA[j].x - ( 32 * !!( SA[j].color&128 ) );

            for (int k = 0; k <= register_set.sprites16; k++) {

                uint8_t p = SG[ pattern + 2 * k ][ spriteLine % 8 ];
                for (int jj = 0; jj < 8; jj++) {
                    
                    for (size_t m = 0; m <= register_set.magnifySprites; m++) {

                        if ( x >= 0 && x < TILE_WIDTH * 8 && ( p & 128 ) && (SA[j].color&0xF) ) {
                            if (CLEAR) {

                                frame_buffer[!screen_buffer_index][active_line_idx][x] &= 0xF0;
                                frame_buffer[!screen_buffer_index][active_line_idx][x] |= SA[j].color&0xF;
                            
                            } else {
                            
                                frame_buffer[!screen_buffer_index][active_line_idx][x] &= 0x0F;
                                frame_buffer[!screen_buffer_index][active_line_idx][x] |= ((SA[j].color&0xF) << 4);
                            
                            }
                        }
                        x++;
                    }
                    p *= 2;
                }
            }
        }

    }

    template<size_t H_DIV>
    void drawFrameBufferLine(size_t line_idx, size_t linebuffer_size, uint8_t *linebuffer ) {

        constexpr size_t TOP_BORDER = 27; 
        constexpr size_t LEFT_BORDER = 13 * H_DIV;

        if (line_idx < TOP_BORDER) { drawBlankLine(linebuffer_size, linebuffer); return; }
        if (line_idx >= TOP_BORDER + 192) { drawBlankLine(linebuffer_size, linebuffer); return; }

        int active_line_idx = line_idx - TOP_BORDER;
        for (size_t i=0; i<LEFT_BORDER * H_DIV; i++) 
            linebuffer[i] = register_set.backdrop;

        const uint8_t *pixel_in = &frame_buffer[screen_buffer_index][active_line_idx][0];        
        uint8_t *pixel_out = linebuffer + LEFT_BORDER * H_DIV;
        for (size_t j=0; j<256; j++) {
            for (size_t k=0; k<H_DIV; k++) {
                *pixel_out++ = *pixel_in;
            }
            pixel_in++;
        }
        for (size_t i=LEFT_BORDER * H_DIV + 256 * H_DIV; i < linebuffer_size; i++) 
            linebuffer[i] = register_set.backdrop;
    }

    enum EM2_Adresses {
        MODE2_ADDRESS_PN0 = 0x1800,
        MODE2_ADDRESS_PN1 = 0x1C00,
        MODE2_ADDRESS_CT  = 0x2000,
        MODE2_ADDRESS_PG  = 0x0000,
        MODE2_ADDRESS_SA0 = 0x1F00,
        MODE2_ADDRESS_SA1 = 0x1F80,
        MODE2_ADDRESS_SG  = 0x3800,
        MODE4_ADDRESS_SA0 = 0x4200,
        MODE4_ADDRESS_SA1 = 0x4600,
    };
        

    template<size_t V_DIV, size_t H_DIV>
    const uint8_t *drawLineBuffer(size_t line_idx) {

        if (register_set.mode2) {
        
            if (old_pn10 != register_set.pn10) {
                old_pn10 = register_set.pn10;

                line_buffer_index = 0;
                
                if ( register_set.pn10 == (MODE2_ADDRESS_PN1 >> 10) )          
                    screen_buffer_index = !screen_buffer_index;          
            }

            if (V_DIV == 1) {
                if (line_buffer_index < 192) {
                    if ( register_set.pn10 == (MODE2_ADDRESS_PN1 >> 10) ) {
                        drawM2_tiles<true>(line_buffer_index);
                        drawM2_sprites<true>(line_buffer_index);
                    } else {
                        drawM2_tiles<false>(line_buffer_index);
                        drawM2_sprites<false>(line_buffer_index);
                    }
                    line_buffer_index++;
                }
            }

            if (V_DIV == 2) {
                if (line_buffer_index < 192 * 2) {
                    if ( line_buffer_index % 2 == 0 ) {
                        if ( register_set.pn10 == (MODE2_ADDRESS_PN1 >> 10) ) {
                            drawM2_tiles<true>(line_buffer_index/2);
                        } else {
                            drawM2_tiles<false>(line_buffer_index/2);
                        }
                    } else {
                        if ( register_set.pn10 == (MODE2_ADDRESS_PN1 >> 10) ) {
                            drawM2_sprites<true>(line_buffer_index/2);
                        } else {
                            drawM2_sprites<false>(line_buffer_index/2);
                        }
                    }
                    line_buffer_index++;
                }
            }
        }

        line_idx /= V_DIV;
        // BLANK SCREEN
        if (not register_set.blankScreen) { drawBlankLine(visible_area_size, vdp_linebuffer); return &vdp_linebuffer[0]; }

        // DRAW
        if (register_set.mode1) {
            //drawM1(line_idx,linebuffer_size, linebuffer);
        } else if (register_set.mode2) {
            drawFrameBufferLine<H_DIV>(line_idx, visible_area_size, vdp_linebuffer);
            //drawSprites<H_DIV>(line_idx,linebuffer_size, linebuffer);
        } else if (register_set.mode3) {
            //drawM3(line_idx,linebuffer_size, linebuffer);
            //drawSprites(line_idx,linebuffer_size, linebuffer);
        } else {
            //drawM0(line_idx,linebuffer_size, linebuffer);
            //drawSprites(line_idx,linebuffer_size, linebuffer);
        }

        return &vdp_linebuffer[0];
    }



    enum Port1_State { PORT1_STATE0 = 0, PORT1_STATE1 = 1 };
    uint8_t port1_data0;
    Port1_State port1_state;


    static std::pair<bool, uint8_t> port0_read_callback(Cartridge &cartridge, GPIOBus32) {

        TMS9918I &tms9918i = *(TMS9918I *)&cartridge;
        tms9918i.port1_state = PORT1_STATE0;
        return {false, 0};
    }

    static std::pair<bool, uint8_t> port1_read_callback(Cartridge &cartridge, GPIOBus32) {

        TMS9918I &tms9918i = *(TMS9918I *)&cartridge;
        tms9918i.port1_state = PORT1_STATE0;
        return {false, 0};
    }

    static std::pair<bool, uint8_t> port0_write_callback(Cartridge &cartridge, GPIOBus32 bus) {

        TMS9918I &tms9918i = *(TMS9918I *)&cartridge;
        uint32_t data         = (bus >> GPIO_D0)  & 0xFF;

        tms9918i.port1_state = PORT1_STATE0;
        tms9918i.vram_ptr = tms9918i.vram_ptr & 0x3FFF; 
        tms9918i.vram[tms9918i.vram_ptr] = data; 
        tms9918i.vram_ptr++;

        return {false, 0};
    }

    static std::pair<bool, uint8_t> port1_write_callback(Cartridge &cartridge, GPIOBus32 bus) {

        TMS9918I &tms9918i = *(TMS9918I *)&cartridge;
        uint32_t data         = (bus >> GPIO_D0)  & 0xFF;

        if (tms9918i.port1_state == PORT1_STATE0) {
            tms9918i.port1_state = PORT1_STATE1;
            tms9918i.port1_data0 = data;
        } else {
            tms9918i.port1_state = PORT1_STATE0;
            if ( (data&0xC0) == 0x80 ) {

                tms9918i.register_set.reg[data & 0x3F] = tms9918i.port1_data0;
            
            } else if ( (data&0xC0) == 0x40 ) {
            
                tms9918i.vram_ptr = tms9918i.port1_data0 + ( uint(data & 0x3F) << 8 );
            }
        }
        
        return {false, 0};
    }
};


static TMS9918I_PIMPL tms9918i;

namespace Cartridges {
    
    // DISABLE CARTRIDGE COPYING
    Cartridge TMS9918I(uint8_t port0, uint8_t port1, const CRT::CRT_Type &crt_type) {

        Cartridge cartridge;

        tms9918i.init(port0, port1, crt_type);
        
        cartridge.name = "TMS9918I";

        cartridge.io_read_callbacks[port0]  = tms9918i.port0_read_callback;
        cartridge.io_read_callbacks[port1]  = tms9918i.port1_read_callback;
        cartridge.io_write_callbacks[port0] = tms9918i.port0_write_callback;
        cartridge.io_write_callbacks[port1] = tms9918i.port1_write_callback;

        return cartridge;
    }

    void TMS9918I_register_crt(const CRT::CRT_Type &type) {

        visible_area_size = type.h_visible_area;
        if (visible_area_size > 256*2-1) {
            if (type.v_visible_area <  192*2) CRT::init(type, &drawLineBuffer<1,2>);
            if (type.v_visible_area >= 192*2) CRT::init(type, &drawLineBuffer<2,2>);
            
        } else {
            if (type.v_visible_area <  192*2) CRT::init(type, &drawLineBuffer<1,1>);
            if (type.v_visible_area >= 192*2) CRT::init(type, &drawLineBuffer<2,1>);
        }

        static const uint8_t TMS9918_Palette[16][3] = {
            {   0,    0,    0},
            {   0,    0,    0},
            {  33,  200,   66},
            {  94,  220,  120},
            {  84,   85,  237},
            { 125,  118,  252},
            { 212,   82,   77},
            {  66,  235,  245},
            { 252,   85,   84},
            { 255,  121,  120},
            { 212,  193,   84},
            { 230,  206,  128},
            {  33,  176,   59},
            { 201,   91,  186},
            { 204,  204,  204},
            { 255,  255,  255}	
        };

        for (int i=0; i<256; i++) {

            int a[3], b[3];
            a[0] = TMS9918_Palette[i/16][0];
            a[1] = TMS9918_Palette[i/16][1];
            a[2] = TMS9918_Palette[i/16][2];

            b[0] = TMS9918_Palette[i%16][0];
            b[1] = TMS9918_Palette[i%16][1];
            b[2] = TMS9918_Palette[i%16][2];

            CRT::set_palette(i, (a[0]+b[0])/2, (a[1]+b[1])/2, (a[2]+b[2])/2);
        }
    }
}


