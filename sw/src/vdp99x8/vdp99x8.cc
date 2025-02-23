#include "vdp99x8.h"
#include "../printf/printf.h"

namespace VDP99X8 {

RegisterSet register_set = {};
State state = {};
uint8_t vram[0x4000];
uint32_t vram_ptr;

uint8_t port1_data0;
Port1_State port1_state;

uint8_t port0, port1;   

size_t visible_area_size;
uint8_t vdp_linebuffer[CRT::MAX_HORIZONTAL_RESOLUTION];

#define MAX_SPRITES_PER_LINE_MODE2 4
#define N_SPRITES 32
#define TILE_WIDTH 32
#define TILE_HEIGHT 24


typedef uint8_t  U8x8  [8];

typedef struct {
	uint8_t y,x;
	uint8_t pattern;
	uint8_t color;} 
EM2_Sprite;

typedef uint8_t T_PN[TILE_HEIGHT][TILE_WIDTH]; // Pattern Name Table
typedef U8x8 T_CT[3][256]; // Pattern Color Table
typedef U8x8 T_PG[3][256]; // Pattern Generator Table
typedef EM2_Sprite T_SA[N_SPRITES]; // Sprite Attribute Table
typedef U8x8 T_SG[256]; // Sprite Generator Table


static inline void drawBlankLine(size_t linebuffer_size, uint8_t *linebuffer) {
    for (int i=0; i<linebuffer_size; i++) linebuffer[i] = register_set.backdrop;
}

template<size_t H_DIV>
static inline void drawSprites(int active_line_idx, size_t linebuffer_size, uint8_t *linebuffer) {

    // BORDER (TMS9918 datahseet 3-8)
    constexpr int TOP_BORDER = 27; 
    constexpr int BOTTOM_BORDER = 24;
    constexpr int RIGHT_BORDER = 15 * H_DIV; //25 for TEXT
    constexpr int LEFT_BORDER = 13 * H_DIV; //19 for TEXT
    
    const T_SA &SA = *(const T_SA *)&vram[(uint16_t)(register_set.sa7 )<< 7];
    const T_SG &SG = *(const T_SG *)&vram[(uint16_t)(register_set.sg11)<<11];

    state._5S = 0;
    state.illegalSprite = 31;
        
    int maxSprite;
    {
        int nShownSprites = 0;

        for (maxSprite = 0; maxSprite < N_SPRITES; maxSprite++) {

            uint8_t spriteLine = (active_line_idx - SA[maxSprite].y - 1 ) >> register_set.magnifySprites;
            
            if (spriteLine >= 8 * (1 + register_set.sprites16)) continue;
            
            if (SA[maxSprite].y == 208) {  break; };
            if (nShownSprites == MAX_SPRITES_PER_LINE_MODE2) { 
                if (state._5S == 0)
                    state.illegalSprite = maxSprite; 
                    state._5S = 1; 
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

        int y = active_line_idx;
        int x = SA[j].x - ( 32 * !!( SA[j].color&128 ) );

        for (int k = 0; k <= register_set.sprites16; k++) {

            uint8_t p = SG[ pattern + 2 * k ][ spriteLine % 8 ];
            for (int jj = 0; jj < 8; jj++) {
                
                for (int m = 0; m <= register_set.magnifySprites; m++) {

                    if ( x >= 0 && x < TILE_WIDTH * 8 && ( p & 128 ) && (SA[j].color&0xF) )
                        for (int h=0; h<H_DIV; h++)
                            linebuffer[x * H_DIV + LEFT_BORDER + h] = SA[j].color&0xF;
                    x++;
                }
                p *= 2;
            }
        }
    }
}

template<size_t H_DIV>
static inline void drawM2(int line_idx, size_t linebuffer_size, uint8_t *linebuffer ) {

    // BORDER (TMS9918 datahseet 3-8)
    constexpr int TOP_BORDER = 27; 
    constexpr int BOTTOM_BORDER = 24;
    constexpr int RIGHT_BORDER = 15 * H_DIV; //25 for TEXT
    constexpr int LEFT_BORDER = 13 * H_DIV; //19 for TEXT
    
    const T_PN &PN = *(const T_PN *)&vram[(uint16_t)(register_set.pn10)<<10];
    const T_CT &CT = *(const T_CT *)&vram[(uint16_t)(register_set.ct6&0x80)<< 6];
    const T_PG &PG = *(const T_PG *)&vram[(uint16_t)(register_set.pg11&0xFC)<<11];
	
	uint8_t pnMask = register_set.pg11 & 3;
	uint8_t ctMask1 = (register_set.ct6>>5) & 3;
	uint8_t ctMask2 = ((register_set.ct6&31)<<3)+7;


    if (line_idx < TOP_BORDER) { drawBlankLine(linebuffer_size, linebuffer); return; }
    if (line_idx >= TOP_BORDER + 192) { drawBlankLine(linebuffer_size, linebuffer); return; }

    int active_line_idx = line_idx - TOP_BORDER;
    for (int i=0; i<LEFT_BORDER; i++) 
        linebuffer[i] = register_set.backdrop;

	// TILES
    {
        int lb = active_line_idx >> 6 ;
        int lt = active_line_idx >> 3 ;
        int l  = active_line_idx & 7;

        uint8_t *pixel = linebuffer + LEFT_BORDER;
        for (int j=0; j<32; j++) {
            uint8_t p = PG[lb&pnMask][PN[lt][j]][l];
            uint8_t c = CT[lb&ctMask1][PN[lt][j]&ctMask2][l];
            for (int jj=0; jj<8; jj++) {
                if (p&128) {
                    if (c>>4) {
                        for (int k=0; k<H_DIV; k++)
                            *pixel++ = (c>>4);
                    } else {
                        for (int k=0; k<H_DIV; k++)
                            *pixel++ = register_set.backdrop;
                    }
                } else {
                    if (c & 0xF) {
                        for (int k=0; k<H_DIV; k++)
                            *pixel++ = (c&0xF);
                    } else {
                        for (int k=0; k<H_DIV; k++)
                            *pixel++ = register_set.backdrop;
                    }
                }
                p = (p << 1);
            }
        }
        for (int i=LEFT_BORDER + 32 * 8 * H_DIV; i<linebuffer_size; i++) 
            linebuffer[i] = register_set.backdrop;       
	}

    drawSprites<H_DIV>(active_line_idx, linebuffer_size, linebuffer);

}

template<size_t V_DIV, size_t H_DIV>
static const uint8_t *drawLineBuffer(int line_idx) {
    //return &vdp_linebuffer[0];

    if(0) if (line_idx==0) {
        _putchar('F');
        _putchar(':');                
        _putchar("0123456789ABCDEF"[(register_set.flags >> 12)&0xF]);
        _putchar("0123456789ABCDEF"[(register_set.flags >>  8)&0xF]);
        _putchar("0123456789ABCDEF"[(register_set.flags >>  4)&0xF]);
        _putchar("0123456789ABCDEF"[(register_set.flags      )&0xF]);
        _putchar(':');                
        _putchar("NY"[register_set.mode2]);
        _putchar('\n');
        return &vdp_linebuffer[0];
    }
    
    

    line_idx /= V_DIV;
    // BLANK SCREEN
    if (not register_set.blankScreen) { drawBlankLine(visible_area_size, vdp_linebuffer); return &vdp_linebuffer[0]; }

    // DRAW
    if (register_set.mode1) {
        //drawM1(line_idx,linebuffer_size, linebuffer);
    } else if (register_set.mode2) {
        drawM2<H_DIV>(line_idx, visible_area_size, vdp_linebuffer);
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


void init(uint8_t port0_, uint8_t port1_, const CRT::CRT_Type &type) {

    port0 = port0_;
    port1 = port1_;
    port1_state = PORT1_STATE0;
    visible_area_size = type.h_visible_area;

    if (visible_area_size > 256*2-1) {
        CRT::init(type, &drawLineBuffer<1,2>);
    } else {
        CRT::init(type, &drawLineBuffer<1,1>);
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

    for (int i=0; i<16; i++) 
        CRT::set_palette(i, TMS9918_Palette[i][0], TMS9918_Palette[i][1], TMS9918_Palette[i][2]);
}
}

