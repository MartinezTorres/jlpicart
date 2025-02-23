#include "../board.h"
#include "../util.h"

#include "vga.pio.h"
#include <functional>

#include "hardware/regs/busctrl.h"
#include "hardware/structs/bus_ctrl.h"

static void *crt_this;

enum HPolarity { H_POLARITY_NEGATIVE, H_POLARITY_POSITIVE };
enum VPolarity { V_POLARITY_NEGATIVE, V_POLARITY_POSITIVE };
enum ESync { VGA_H_V_SYNC, COMPOSITE_SYNC_XOR, COMPOSITE_SYNC_AND, PAL_SYNC, DISABLED_SYNC };
enum EInterlace { NON_INTERLACED, INTERLACED };

template<
    size_t VCO, size_t SYS_DIV, size_t FLASH_DIV, enum vreg_voltage VOLTAGE,
    size_t H_VISIBLE_AREA, size_t H_FRONT_PORCH, size_t H_SYNC_PULSE, size_t H_BACK_PORCH, enum HPolarity H_POLARITY,
    size_t V_VISIBLE_AREA, size_t V_FRONT_PORCH, size_t V_SYNC_PULSE, size_t V_BACK_PORCH, enum VPolarity V_POLARITY,
    size_t BPP, size_t PIO_CLOCK_DIVIDER, enum ESync SYNC, enum EInterlace INTERLACE>
struct CRT {

    // Uses normal VGA 640x480 resolution with a clock of 302MHz.
    // Each pixels should be 12 cycles.
    // One line: 12*800 = 9600 cycles = 31.788us
    // We leverage the 0.5% VESA tolerance
    // Each cycle we output 8 bits for normal sync, or 4 bits for composite sync.

    static constexpr size_t H_WHOLE = H_VISIBLE_AREA + H_FRONT_PORCH + H_SYNC_PULSE + H_BACK_PORCH;
    static constexpr size_t V_WHOLE = V_VISIBLE_AREA + V_FRONT_PORCH + V_SYNC_PULSE + V_BACK_PORCH;

    struct alignas(4) Pixel  {

        uint8_t data[BPP / 2];

        Pixel(bool sync, uint32_t red, uint32_t green, uint32_t blue) {

            static_assert( BPP % 2 == 0 , "not multiple of 16 bits");

            int idx = 0;
            for (int i=0; i<BPP/2; i++) {

                uint8_t d = 0;

                d += (red   & 1); d <<= 1; red   >>= 1;
                d += (green & 1); d <<= 1; green >>= 1;
                d += (blue  & 1); d <<= 1; blue  >>= 1;
                d += sync;        d <<= 1;

                d += (red   & 1); d <<= 1; red   >>= 1;
                d += (green & 1); d <<= 1; green >>= 1;
                d += (blue  & 1); d <<= 1; blue  >>= 1;
                d += sync;

                data[i] = d;
            }
        }

        Pixel() : Pixel( 0, 0, 0, 0 ) {}
    };

    Pixel linebuffer_blank[H_WHOLE];
    Pixel linebuffer_vsync[H_WHOLE];
    Pixel linebuffer_visible[2][H_WHOLE];

    Pixel palette[16][256]; // 16 dither patterns, 256 colors;
    uint32_t dither_patterns[16][256]; // 16 dither patterns per gray level


    void dither_tests() {
        
        // INIT DITHER PATTERNS
        if (0) {
            uint8_t shuffle[] = {110, 51, 11, 69, 209, 142, 17, 109, 164, 101, 115, 39, 108, 2, 75, 56, 59, 166, 153, 116, 143, 86, 57, 133, 169, 192, 27, 136, 104, 213, 30, 96, 13, 68, 252, 114, 71, 102, 201, 139, 66, 227, 226, 79, 89, 176, 106, 0, 152, 230, 182, 165, 163, 43, 16, 202, 49, 105, 149, 132, 171, 249, 141, 237, 53, 228, 8, 33, 168, 15, 25, 177, 196, 31, 82, 212, 247, 231, 255, 117, 23, 122, 77, 19, 126, 44, 120, 131, 194, 1, 238, 95, 4, 218, 157, 5, 225, 193, 155, 179, 7, 232, 180, 248, 140, 251, 188, 10, 223, 113, 92, 48, 61, 127, 190, 67, 40, 124, 63, 243, 158, 159, 74, 54, 151, 191, 200, 203, 199, 24, 3, 125, 246, 204, 187, 50, 62, 146, 244, 170, 118, 205, 181, 154, 46, 12, 99, 236, 240, 147, 233, 172, 134, 32, 210, 161, 156, 178, 221, 78, 85, 135, 145, 52, 211, 28, 18, 41, 37, 14, 22, 29, 34, 185, 250, 36, 81, 253, 195, 144, 64, 235, 88, 242, 217, 220, 121, 94, 239, 20, 207, 123, 150, 65, 97, 208, 70, 84, 186, 6, 224, 9, 60, 175, 162, 128, 129, 87, 137, 234, 91, 26, 241, 58, 245, 138, 72, 45, 21, 42, 173, 93, 38, 83, 160, 198, 103, 80, 76, 73, 167, 107, 215, 219, 119, 35, 197, 184, 222, 174, 254, 214, 112, 206, 98, 47, 216, 111, 148, 183, 229, 100, 130, 55, 189, 90};
            
            for (int i=0; i<256; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0;
                    if (i) dither_patterns[j][i] = dither_patterns[j][i-1];
                }
                uint8_t k = shuffle[i]; 
                k = i;
                dither_patterns[    (k%8)][i] |= (1U << (   (k/8)) );
                dither_patterns[8 + (k%8)][i] |= (1U << (31-(k/8)) );
            }
        }

        // INIT DITHER PATTERNS
        if(0) {
            for (int i= 0; i<22; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0;
                }
            }

            for (int i=22; i<64; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0x11111111U;
                }
            }

            for (int i=64; i<106; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0x24924924U;
                }
            }

            for (int i=106; i<256-106; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0x55555555U;
                }
            }

            for (int i=256-106; i<256-64; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0x6BD6BD6BU;
                }
            }

            for (int i=256-64; i<256-22; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0x77777777U;
                }
            }

            for (int i=256-22; i<256; i++) {
                for (int j=0; j<16; j++) {
                    dither_patterns[j][i] = 0xFFFFFFFFU;
                }
            }
        }



        // INIT DITHER PATTERNS
        if(0) {

            uint32_t pat[32] = {
                0x00000000U,
                0x00000000U,
                0x00000000U,
                0x00000000U,

                0x11111111U, // 3
                0x22222222U, // 2.5
                0x44444444U, // 1
                0x77777777U, // 7

                0xFFFFFFFFU, // 10
                0x49249249U, // 0
                0x92492492U, // 1
                0x24924924U, // 5

                0xFFFFFFFFU, // 10
                0x55555555U, // 5.5
                0xAAAAAAAAU, // 5
                0x33333333U, // 6

                0x66666666U, // 5.5 (flicker)
                0xCCCCCCCCU, // 4
                0x99999999U, // 5 (flicker)
                0xFFFFFFFFU, // 10

                0x6BD6BD6BU, // 5 (flicker)
                0xD6BD6BD6U, // 6 (flicker)
                0xBD6BD6BDU, // 9
                0x00000000U, // 0

                0xEEEEEEEEU, // 6.5
                0xDDDDDDDDU, // 6.5
                0xBBBBBBBBU, // 8.5 (flicker)
                0x77777777U, // 9 (flicker) 

                0xFFFFFFFFU,
                0xFFFFFFFFU,
                0xFFFFFFFFU,
                0xFFFFFFFFU,
            };

            for (int k = 0; k < 32; k++) {
                for (int i = k*8; i < k*8 + 8; i++) {
                    for (int j=0; j<16; j++) {
                        dither_patterns[j][i] = pat[k];
                    }
                }
            }

        }

        // INIT DITHER PATTERNS
        if(1) {

            uint32_t pat[32] = {
                0x92492492U, // 1 (flicker)
                0x6BD6BD6BU, // 5 (flicker)
                0x99999999U, // 5 (flicker)
                0x66666666U, // 5.5 (flicker)
                0xD6BD6BD6U, // 6 (flicker)
                0xBBBBBBBBU, // 8.5 (flicker)
                0x77777777U, // 9 (flicker) 
                0x24924924U, // 5 (flicker)
                0x00000000U,
                0x11111111U, // 3 (blue)
                0x55555555U, // 5.5 (blue?)
                0xEEEEEEEEU, // 6.5
                0x77777777U, // 9.5
                0x00000000U,
                0xFFFFFFFFU,
                0x00000000U,

                0x44444444U, // 1
                0x49249249U, // 0
                0x22222222U, // 2.5
                0xCCCCCCCCU, // 4
                0xAAAAAAAAU, // 5
                0x33333333U, // 6
                0xDDDDDDDDU, // 6.5
                0xBD6BD6BDU, // 9.9


                0xFFFFFFFFU, // 10

                0x00000000U, // 0

                0xBD6BD6BDU, // 9.9

                0x6DB6DB6DU, // 9.9
                0xDB6DB6DBU, // 9.9
                0xB6DB6DB6U, // 9.9

                0xFFFFFFFFU,
                0xFFFFFFFFU,
            };

            for (int k = 0; k < 32; k++) {
                for (int i = k*8; i < k*8 + 8; i++) {
                    for (int j=0; j<16; j++) {
                        dither_patterns[j][i] = pat[k];
                    }
                }
            }

        }
    }

    void init_dither_patterns_VGA640_LCD() {

        static const uint32_t pat[10][2] = {
                
                { 0x00000000U, 0 },

                { 0x44444444U, 10 },
                { 0x49249249U, 20 },
                { 0x22222222U, 40 },
                { 0xCCCCCCCCU, 60 },

                { 0xAAAAAAAAU, 80 },
                { 0x33333333U, 110 },
                { 0xDDDDDDDDU, 140 },
                { 0xBD6BD6BDU, 170 },

                { 0xFFFFFFFFU, 180 },
        };


        for (int i=0; i<256; i++) {
            int step_goal = ( i * 180 ) / 255;

            int current_goal = 0;
            int current_score = 0;

            int l = 0;
            {
                for (int k = 0; k < 10 - 1; k++) {
                    if ( pat[k][1] >= step_goal)
                        break;
                    l = k;
                }
            }

            for (int j=0; j<16; j++) {
                current_goal += step_goal;
                if (current_goal >  pat[l+1][1]) {
                    dither_patterns[j][i] = pat[l+1][0];
                    current_goal -= pat[l+1][1];
                } else {
                    dither_patterns[j][i] = pat[l][0];
                    current_goal -= pat[l][1];
                }
            }
        }


    }

    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {

        for (int j=0; j<16; j++)
            palette[j][idx] = Pixel(1, dither_patterns[j][r], dither_patterns[j][g], dither_patterns[j][b]);
    }               

    uint line_idx, frame_idx;

    uint dma_channels[2];
    uint dma_idx;
    uint vga_sm;
    PIO  vga_pio;

    std::function<const uint8_t *(uint16_t)> get_framebuffer_line;

    static void dma_irq_handler_static() { ((CRT *)crt_this)->dma_irq_handler(); }
    void dma_irq_handler() {

        if (dma_hw->ints0 & (1u << dma_idx)) {
            dma_hw->ints0 = (1u << dma_idx);  // Clear interrupt
        }
        //_putchar('0'+dma_idx);

        if constexpr ( SYNC == VGA_H_V_SYNC) {
            if (line_idx == 0            ) gpio_put(GPIO39_V_VSYNC, V_POLARITY == V_POLARITY_POSITIVE);
            if (line_idx == V_SYNC_PULSE ) gpio_put(GPIO39_V_VSYNC, V_POLARITY != V_POLARITY_POSITIVE);
        }

        line_idx = line_idx + 1;
        if (line_idx == V_WHOLE) {
            line_idx = 0;
            frame_idx ++;
        };



        if ( line_idx < V_SYNC_PULSE) { // Sync pulse
                
            dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_vsync[0]), false);

        } else if ( line_idx < V_SYNC_PULSE + V_BACK_PORCH ) { //Back porch
                
            dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_blank[0]), false);
            
        } else if ( line_idx < V_SYNC_PULSE + V_BACK_PORCH + V_VISIBLE_AREA ) { // Main image

            dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_visible[dma_idx][0]), false);
            
            if (0) if (get_framebuffer_line) {

                int l = line_idx -( V_SYNC_PULSE + V_BACK_PORCH );

                const uint8_t *framebuffer_line = get_framebuffer_line(l);

                Pixel *dma_line = &linebuffer_visible[dma_idx][H_SYNC_PULSE + H_BACK_PORCH];

                Pixel *palette_dither0 = &palette[(1 * frame_idx + 1 * l + 0) % 2][0];
                Pixel *palette_dither1 = &palette[(1 * frame_idx + 1 * l + 1) % 2][0];
                Pixel *palette_dither2 = &palette[(1 * frame_idx + 1 * l + 2) % 2][0];
                Pixel *palette_dither3 = &palette[(1 * frame_idx + 1 * l + 3) % 2][0];

                for (int i=0; i < H_VISIBLE_AREA; i+=4) {

                //for (int i=0; i < 320; i+=4) {
                    *dma_line++ = palette_dither0[ *framebuffer_line++ ];
                    *dma_line++ = palette_dither1[ *framebuffer_line++ ];
                    *dma_line++ = palette_dither2[ *framebuffer_line++ ];
                    *dma_line++ = palette_dither3[ *framebuffer_line++ ];
                }
            }
        } else { // Front porch

            dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_blank[0]), false);
        }

        dma_idx = (1-dma_idx);
    }

    static void core1_init_static() { ((CRT *)crt_this)->core1_init(); }
    void core1_init() {

        // INIT SYNC PATTERNS
        if constexpr (SYNC == VGA_H_V_SYNC and INTERLACE == NON_INTERLACED) {
            for (int i=0; i<H_WHOLE; i++) {


                bool hsync_active   =     (H_POLARITY == H_POLARITY_POSITIVE);
                bool hsync_inactive = not (H_POLARITY == H_POLARITY_POSITIVE);

                bool hsync = hsync_inactive;
                if (i < H_SYNC_PULSE) hsync = hsync_active;

                linebuffer_blank[i] = Pixel( hsync, 0, 0, 0);
                linebuffer_vsync[i] = Pixel( hsync, 0, 0, 0);
                linebuffer_visible[0][i] = Pixel( hsync, 0, 0, 0);
                linebuffer_visible[1][i] = Pixel( hsync, 0, 0, 0);
            }
        } else if constexpr (SYNC == COMPOSITE_SYNC_XOR and INTERLACE == NON_INTERLACED) {
            for (int i=0; i<H_WHOLE; i++) {

                bool hsync_active   =     (H_POLARITY == H_POLARITY_POSITIVE);
                bool hsync_inactive = not (H_POLARITY == H_POLARITY_POSITIVE);

                bool vsync_active   =     (V_POLARITY == V_POLARITY_POSITIVE);
                bool vsync_inactive = not (V_POLARITY == V_POLARITY_POSITIVE);

                bool hsync = hsync_inactive;
                if (i < H_SYNC_PULSE) hsync = hsync_active;

                linebuffer_blank[i]      = Pixel( hsync xor vsync_inactive, 0, 0, 0);
                linebuffer_vsync[i]      = Pixel( hsync xor vsync_active,   0, 0, 0);
                linebuffer_visible[0][i] = Pixel( hsync xor vsync_inactive, 0, 0, 0);
                linebuffer_visible[1][i] = Pixel( hsync xor vsync_inactive, 0, 0, 0);
            }

        } else if constexpr (SYNC == COMPOSITE_SYNC_AND and INTERLACE == NON_INTERLACED) {
            for (int i=0; i<H_WHOLE; i++) {

                bool hsync_active   =     (H_POLARITY == H_POLARITY_POSITIVE);
                bool hsync_inactive = not (H_POLARITY == H_POLARITY_POSITIVE);

                bool vsync_active   =     (V_POLARITY == V_POLARITY_POSITIVE);
                bool vsync_inactive = not (V_POLARITY == V_POLARITY_POSITIVE);

                bool hsync = hsync_inactive;
                if (i < H_SYNC_PULSE) hsync = hsync_active;

                linebuffer_blank[i]      = Pixel( hsync and vsync_inactive, 0, 0, 0);
                linebuffer_vsync[i]      = Pixel( hsync and vsync_active,   0, 0, 0);
                linebuffer_visible[0][i] = Pixel( hsync and vsync_inactive, 0, 0, 0);
                linebuffer_visible[1][i] = Pixel( hsync and vsync_inactive, 0, 0, 0);
            }

        } else if constexpr (SYNC == PAL_SYNC and INTERLACE == NON_INTERLACED) {
            for (int i=0; i<H_WHOLE; i++) {

                bool hsync_active   =     (H_POLARITY == H_POLARITY_POSITIVE);
                bool hsync_inactive = not (H_POLARITY == H_POLARITY_POSITIVE);

                bool vsync_active   =     (V_POLARITY == V_POLARITY_POSITIVE);
                bool vsync_inactive = not (V_POLARITY == V_POLARITY_POSITIVE);

                bool hsync = hsync_inactive;
                if (i < H_SYNC_PULSE) hsync = hsync_active;

                //bool vsync = vsync_inactive;
                //if (i < H_WHOLE * 270 / 640) vsync = vsync_active;
                bool vsync = vsync_active;

                linebuffer_blank[i]      = Pixel( hsync, 0, 0, 0);
                linebuffer_vsync[i]      = Pixel( vsync, 0, 0, 0);
                linebuffer_visible[0][i] = Pixel( hsync, 0, 0, 0);
                linebuffer_visible[1][i] = Pixel( hsync, 0, 0, 0);
            }

        } else if constexpr (SYNC == DISABLED_SYNC) {
            for (int i=0; i<H_WHOLE; i++) {

                linebuffer_blank[i] = Pixel( 0, 0, 0, 0);
                linebuffer_vsync[i] = Pixel( 0, 0, 0, 0);
                linebuffer_visible[0][i] = Pixel( 0, 0, 0, 0);
                linebuffer_visible[1][i] = Pixel( 0, 0, 0, 0);
            }

        } else {

            static_assert(false, "Not supported sync");
        }

        // INIT BARS
        {
            init_dither_patterns_VGA640_LCD();

            for (int i=0; i<64; i++) {
                int b = 0;
                int f = i * 4 + 2;

                set_palette(       i, b + f, b + f, b + f);
                set_palette(  64 + i, b + f, b    , b    );
                set_palette( 128 + i, b    , b + f, b    );
                set_palette( 192 + i, b    , b    , b + f);
            }

            for (int l=0; l<2; l++) {

                Pixel *dma_line = &linebuffer_visible[l][H_SYNC_PULSE + H_BACK_PORCH];
                Pixel *palette_dither0 = &palette[l % 16][0];

                for (int i=0; i < H_VISIBLE_AREA; i++) {
                    *dma_line++ = palette_dither0[ (i/2) % 256 ];
                }
            }

        }

        _putchar('2');

        // INIT PIO
        {

            vga_pio = pio2;
            vga_sm = 0; 
            uint vga_offset;
            pio_sm_config vga_config;
                
            pio_set_gpio_base(vga_pio, 16);
            pio_sm_claim(vga_pio, vga_sm);
            vga_offset = pio_add_program(vga_pio, &vga_program);
            vga_config = vga_program_get_default_config( vga_offset );
            sm_config_set_out_pin_base(&vga_config, GPIO40_V_HSYNC);
        
            // Init pins
            if constexpr ( SYNC == VGA_H_V_SYNC) {
                gpio_init(GPIO39_V_VSYNC); 
                gpio_put(GPIO39_V_VSYNC, true); 
                gpio_set_dir(GPIO39_V_VSYNC, GPIO_OUT);
            }
            for (int pin = GPIO40_V_HSYNC; pin <= GPIO43_V_RED; pin++) {
                
                //gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
                //gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
                gpio_set_function(pin, PIO_FUNCSEL_NUM(vga_pio, pin));
            }    
        
            // Init state machine
            pio_sm_init(vga_pio, vga_sm, vga_offset, &vga_config);

            pio_sm_set_enabled(vga_pio, vga_sm, true);

        }

        _putchar('4');

        for (int i=0; i<32; i++) {
            _putchar(' ');
            _putchar("0123456789ABCDEF"[(*(i+(uint8_t *)&linebuffer_blank[0]))/16]);
            _putchar("0123456789ABCDEF"[(*(i+(uint8_t *)&linebuffer_blank[0]))%16]);
            _putchar(' ');
            //busy_wait_us(1000);
        }

        
        _putchar(' ');
        _putchar("0123456789ABCDEF"[( ( sizeof(linebuffer_blank) / sizeof(uint32_t) ) >> 16 ) % 16]);
        _putchar("0123456789ABCDEF"[( ( sizeof(linebuffer_blank) / sizeof(uint32_t) ) >> 12 ) % 16]);
        _putchar("0123456789ABCDEF"[( ( sizeof(linebuffer_blank) / sizeof(uint32_t) ) >> 8 ) % 16]);
        _putchar("0123456789ABCDEF"[( ( sizeof(linebuffer_blank) / sizeof(uint32_t) ) >> 4 ) % 16]);
        _putchar("0123456789ABCDEF"[( ( sizeof(linebuffer_blank) / sizeof(uint32_t) ) >> 0 ) % 16]);
        _putchar(' ');

        _putchar(' ');
        _putchar("0123456789ABCDEF"[(vga_sm)/16]);
        _putchar("0123456789ABCDEF"[(vga_sm)%16]);
        _putchar(' ');

        _putchar('\n');

        // INIT DMA
        {

            bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
            dma_channels[0] = dma_claim_unused_channel(true);
            dma_channels[1] = dma_claim_unused_channel(true);

            for (int c_idx = 0; c_idx<2; c_idx++) {
                dma_channel_config c = dma_channel_get_default_config(dma_channels[c_idx]);

                channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
                channel_config_set_read_increment(&c, true);
                channel_config_set_write_increment(&c, false);
                channel_config_set_dreq(&c, pio_get_dreq(vga_pio, vga_sm, true)) ;
                channel_config_set_chain_to(&c, dma_channels[1 -c_idx]);

                dma_channel_configure(
                    dma_channels[c_idx],
                    &c,
                    &vga_pio->txf[vga_sm],
                    &linebuffer_blank[0],
                    sizeof(linebuffer_blank) / sizeof(uint32_t),
                    false
                );

                static_assert(sizeof(linebuffer_blank[0]) == BPP / 2, "Pixel struct is not compact");
                static_assert(sizeof(linebuffer_blank) % sizeof(uint32_t) == 0, "line buffer is not multiple of 32 bits");

                dma_channel_set_irq0_enabled(dma_channels[c_idx], true);
            }

            
            // Register IRQ Handler
            irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler_static);
            irq_set_enabled(DMA_IRQ_0, true);

            dma_channel_start(dma_channels[0]);
            line_idx = 1; // We configured line0 and line1
            frame_idx = 0;
            dma_idx = 0;
            
            while (1)
                tight_loop_contents();

        }
    }

    CRT( std::function<const uint8_t *(uint16_t)> get_framebuffer_line ) : get_framebuffer_line(get_framebuffer_line) {

        crt_this = this;
        __dsb();

        _putchar('Q');

        busy_wait_us(100*1000);
        _putchar('0');

        set_speed(VCO, SYS_DIV, FLASH_DIV, VOLTAGE);

        _putchar('S');
        busy_wait_us(100*1000);

        multicore_launch_core1(core1_init_static);
    }

};

using VGA320x480_60Hz = CRT< // 302 MHz
    906, 3, 3, VREG_VOLTAGE_1_20,         // size_t VCO, size_t SYS_DIV, size_t FLASH_DIV, enum vreg_voltage VOLTAGE,
    320,  8, 48, 24, H_POLARITY_NEGATIVE, // size_t H_VISIBLE_AREA, size_t H_FRONT_PORCH, size_t H_SYNC_PULSE, size_t H_BACK_PORCH, bool H_POLARITY_POSITIVE,
    480, 10,  2, 33, V_POLARITY_NEGATIVE, // size_t V_VISIBLE_AREA, size_t V_FRONT_PORCH, size_t V_SYNC_PULSE, size_t V_BACK_PORCH, bool V_POLARITY_POSITIVE,
    24, 1, VGA_H_V_SYNC, NON_INTERLACED>; // size_t BPP, size_t PIO_CLOCK_DIVIDER, ESync SYNC, EInterlace INTERLACED>

using SCART_288p_50Hz = CRT< // 256.5 MHz
    1026, 4, 2, VREG_VOLTAGE_1_15,         // size_t VCO, size_t SYS_DIV, size_t FLASH_DIV, enum vreg_voltage VOLTAGE,
    284*2,  8*2, 25*2, 26*2, H_POLARITY_NEGATIVE, // size_t H_VISIBLE_AREA, size_t H_FRONT_PORCH, size_t H_SYNC_PULSE, size_t H_BACK_PORCH, bool H_POLARITY_POSITIVE,
    288, 10,  2, 12, V_POLARITY_NEGATIVE, // size_t V_VISIBLE_AREA, size_t V_FRONT_PORCH, size_t V_SYNC_PULSE, size_t V_BACK_PORCH, bool V_POLARITY_POSITIVE,
    24, 1, COMPOSITE_SYNC_AND, NON_INTERLACED>; // size_t BPP, size_t PIO_CLOCK_DIVIDER, ESync SYNC, EInterlace INTERLACED>

using SCART_240p_60Hz = CRT< // 256.5 MHz
    1026, 4, 2, VREG_VOLTAGE_1_15,         // size_t VCO, size_t SYS_DIV, size_t FLASH_DIV, enum vreg_voltage VOLTAGE,
    284*2,  8*2, 25*2, 26*2-5, H_POLARITY_NEGATIVE, // size_t H_VISIBLE_AREA, size_t H_FRONT_PORCH, size_t H_SYNC_PULSE, size_t H_BACK_PORCH, bool H_POLARITY_POSITIVE,
    240, 10,  2, 10, V_POLARITY_NEGATIVE, // size_t V_VISIBLE_AREA, size_t V_FRONT_PORCH, size_t V_SYNC_PULSE, size_t V_BACK_PORCH, bool V_POLARITY_POSITIVE,
    24, 1, COMPOSITE_SYNC_AND, NON_INTERLACED>; // size_t BPP, size_t PIO_CLOCK_DIVIDER, ESync SYNC, EInterlace INTERLACED>

void vga320_init( std::function<const uint8_t *(uint16_t)> get_framebuffer_line = std::function<const uint8_t *(uint16_t)>() ) {

    putstring("   VGA ENTRY START\n");

    static SCART_240p_60Hz vga(get_framebuffer_line);
    //static SCART_288p_50Hz vga(get_framebuffer_line);

    //static VGA320x480_60Hz vga(get_framebuffer_line);

    busy_wait_us(5000000);

    putstring("   VGA INITIALIZED\n");

    //while(true) busy_wait_us(5000000);
}

