#include "crt.h"

#include <board.h>
#include <multitask/multitask.h>

#include "crt.pio.h"
#include <functional>
#include <cmath>

#include "hardware/regs/busctrl.h"
#include "hardware/structs/bus_ctrl.h"

namespace CRT {

///////////////////////////////////////////////////////////////////////////////
// CRT Type
static CRT_Type crt_type;

///////////////////////////////////////////////////////////////////////////////
// CRT Engine State
enum ECRTEngineState { CRT_NOT_INITIALIZED, CRT_REQUEST_PARK, CRT_PARKED, CRT_REQUEST_RUN, CRT_RUNNING };
volatile static ECRTEngineState CRT_engine_state= CRT_NOT_INITIALIZED;

///////////////////////////////////////////////////////////////////////////////
// Bitbanged Buffer Patterns

// Each sys_clock we sent 4 bits to the PIO.
// LINE_BUFFER_SIZE needs to be larger than ( system_clock / line_frequency ) / 2
// Plugging ( 300 Mhz / 15 KHz / 2 ) = 10000  
#define LINE_BUFFER_SIZE ( 1024 * 10 )

static uint8_t linebuffer_blank[LINE_BUFFER_SIZE];
static uint8_t linebuffer_vsync[LINE_BUFFER_SIZE];
static uint8_t linebuffer_visible[2][LINE_BUFFER_SIZE];
uint32_t line_idx, frame_idx;


///////////////////////////////////////////////////////////////////////////////
// Color And Palette Management

struct alignas(4) PixelPattern  {

    uint8_t data[32];

    PixelPattern(bool sync = false, uint64_t red = 0, uint64_t green = 0, uint64_t blue = 0) {

        for (int i=0; i<32; i++) {

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

    void draw(uint8_t *linebuffer, size_t idx /*, size_t dither_pattern = 0*/) {

        linebuffer += idx * (crt_type.cycles_per_pixel/2);
        for (size_t i = 0; i < crt_type.cycles_per_pixel / 2; i++) {
            linebuffer[i] = data[i];
        }
    } 
};

static uint64_t dither_patterns[256];
static uint8_t palette_rgb[256][3] = {
{  0,  0,  0}, {  0,  0,170}, {  0,170,  0}, {  0,170,170}, {170,  0,  0}, {170,  0,170}, {170, 85,  0}, {170,170,170}, 
{ 85, 85, 85}, { 85, 85,255}, { 85,255, 85}, { 85,255,255}, {255, 85, 85}, {255, 85,255}, {255,255, 85}, {255,255,255}, 
{  0,  0,  0}, { 20, 20, 20}, { 32, 32, 32}, { 44, 44, 44}, { 56, 56, 56}, { 68, 68, 68}, { 80, 80, 80}, { 97, 97, 97}, 
{113,113,113}, {129,129,129}, {145,145,145}, {161,161,161}, {182,182,182}, {202,202,202}, {226,226,226}, {255,255,255}, 
{  0,  0,255}, { 64,  0,255}, {125,  0,255}, {190,  0,255}, {255,  0,255}, {255,  0,190}, {255,  0,125}, {255,  0, 64}, 
{255,  0,  0}, {255, 64,  0}, {255,125,  0}, {255,190,  0}, {255,255,  0}, {190,255,  0}, {125,255,  0}, { 64,255,  0}, 
{  0,255,  0}, {  0,255, 64}, {  0,255,125}, {  0,255,190}, {  0,255,255}, {  0,190,255}, {  0,125,255}, {  0, 64,255}, 
{125,125,255}, {157,125,255}, {190,125,255}, {222,125,255}, {255,125,255}, {255,125,222}, {255,125,190}, {255,125,157}, 
{255,125,125}, {255,157,125}, {255,190,125}, {255,222,125}, {255,255,125}, {222,255,125}, {190,255,125}, {157,255,125}, 
{125,255,125}, {125,255,157}, {125,255,190}, {125,255,222}, {125,255,255}, {125,222,255}, {125,190,255}, {125,157,255}, 
{182,182,255}, {198,182,255}, {218,182,255}, {234,182,255}, {255,182,255}, {255,182,234}, {255,182,218}, {255,182,198}, 
{255,182,182}, {255,198,182}, {255,218,182}, {255,234,182}, {255,255,182}, {234,255,182}, {218,255,182}, {198,255,182}, 
{182,255,182}, {182,255,198}, {182,255,218}, {182,255,234}, {182,255,255}, {182,234,255}, {182,218,255}, {182,198,255}, 
{  0,  0,113}, { 28,  0,113}, { 56,  0,113}, { 85,  0,113}, {113,  0,113}, {113,  0, 85}, {113,  0, 56}, {113,  0, 28}, 
{113,  0,  0}, {113, 28,  0}, {113, 56,  0}, {113, 85,  0}, {113,113,  0}, { 85,113,  0}, { 56,113,  0}, { 28,113,  0}, 
{  0,113,  0}, {  0,113, 28}, {  0,113, 56}, {  0,113, 85}, {  0,113,113}, {  0, 85,113}, {  0, 56,113}, {  0, 28,113}, 
{ 56, 56,113}, { 68, 56,113}, { 85, 56,113}, { 97, 56,113}, {113, 56,113}, {113, 56, 97}, {113, 56, 85}, {113, 56, 68}, 
{113, 56, 56}, {113, 68, 56}, {113, 85, 56}, {113, 97, 56}, {113,113, 56}, { 97,113, 56}, { 85,113, 56}, { 68,113, 56}, 
{ 56,113, 56}, { 56,113, 68}, { 56,113, 85}, { 56,113, 97}, { 56,113,113}, { 56, 97,113}, { 56, 85,113}, { 56, 68,113}, 
{ 80, 80,113}, { 89, 80,113}, { 97, 80,113}, {105, 80,113}, {113, 80,113}, {113, 80,105}, {113, 80, 97}, {113, 80, 89}, 
{113, 80, 80}, {113, 89, 80}, {113, 97, 80}, {113,105, 80}, {113,113, 80}, {105,113, 80}, { 97,113, 80}, { 89,113, 80}, 
{ 80,113, 80}, { 80,113, 89}, { 80,113, 97}, { 80,113,105}, { 80,113,113}, { 80,105,113}, { 80, 97,113}, { 80, 89,113}, 
{  0,  0, 64}, { 16,  0, 64}, { 32,  0, 64}, { 48,  0, 64}, { 64,  0, 64}, { 64,  0, 48}, { 64,  0, 32}, { 64,  0, 16}, 
{ 64,  0,  0}, { 64, 16,  0}, { 64, 32,  0}, { 64, 48,  0}, { 64, 64,  0}, { 48, 64,  0}, { 32, 64,  0}, { 16, 64,  0}, 
{  0, 64,  0}, {  0, 64, 16}, {  0, 64, 32}, {  0, 64, 48}, {  0, 64, 64}, {  0, 48, 64}, {  0, 32, 64}, {  0, 16, 64}, 
{ 32, 32, 64}, { 40, 32, 64}, { 48, 32, 64}, { 56, 32, 64}, { 64, 32, 64}, { 64, 32, 56}, { 64, 32, 48}, { 64, 32, 40}, 
{ 64, 32, 32}, { 64, 40, 32}, { 64, 48, 32}, { 64, 56, 32}, { 64, 64, 32}, { 56, 64, 32}, { 48, 64, 32}, { 40, 64, 32}, 
{ 32, 64, 32}, { 32, 64, 40}, { 32, 64, 48}, { 32, 64, 56}, { 32, 64, 64}, { 32, 56, 64}, { 32, 48, 64}, { 32, 40, 64}, 
{ 44, 44, 64}, { 48, 44, 64}, { 52, 44, 64}, { 60, 44, 64}, { 64, 44, 64}, { 64, 44, 60}, { 64, 44, 52}, { 64, 44, 48}, 
{ 64, 44, 44}, { 64, 48, 44}, { 64, 52, 44}, { 64, 60, 44}, { 64, 64, 44}, { 60, 64, 44}, { 52, 64, 44}, { 48, 64, 44}, 
{ 44, 64, 44}, { 44, 64, 48}, { 44, 64, 52}, { 44, 64, 60}, { 44, 64, 64}, { 44, 60, 64}, { 44, 52, 64}, { 44, 48, 64}, 
{  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, {  0,  0,  0}, 
};
static PixelPattern palette_patterns[256];

static void init_dither_patterns_VGA640_LG_LCD() {

    const uint32_t pat[10][2] = {
        
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

        int l = 0;
        {
            for (int k = 0; k < 10 - 1; k++) {
                if ( int(pat[k][1]) >= step_goal)
                    break;
                l = k;
            }
        }

        for (int j=0; j<2; j++) {
            current_goal += step_goal;

            uint32_t *dp = (uint32_t *)&dither_patterns[i];

            if (current_goal >  int(pat[l+1][1])) {
                dp[j] = pat[l+1][0];
                current_goal -= int(pat[l+1][1]);
            } else {
                dp[j] = pat[l][0];
                current_goal -= int(pat[l][1]);
            }
        }
    }
}



static void init_dither_patterns_Analog() {

    /*auto srgbToLinear = [](double x) {
        if (x <= 0.0)
            return 0.0;
        else if (x >= 1.0)
            return 1.0;
        else if (x < 0.04045)
            return x / 12.92;
        else
            return std::pow((x + 0.055) / 1.055, 2.4);
    };*/


    const uint8_t pat[5][2] = {
        
        { 0x00U,  0 },
        { 0x22U, 20 },
        { 0x55U, 55 },
        { 0xDDU, 100 },
        { 0xFFU, 160 },
    };

    for (int i=0; i<256; i++) {

        //double step_goal = srgbToLinear(i/255.) * 160.;
        double step_goal = (i/255.) * 160.;

        double current_goal = 0;

        int l = 0;
        {
            for (int k = 0; k < 5 - 1; k++) {
                if ( pat[k][1] >= step_goal)
                    break;
                l = k;
            }
        }

        dither_patterns[i] = 0;
        for (uint j=0; j<16; j++) {
            current_goal += step_goal;

            uint8_t *dp = (uint8_t *)&dither_patterns[i];
            uint8_t mask = (j%2==0?0x0F:0xF0);

            
            if (current_goal >  pat[l+1][1]) {
                dp[j/2] += mask & pat[l+1][0];
                current_goal -= pat[l+1][1];
            } else {
                dp[j/2] += mask & pat[l  ][0];
                current_goal -= pat[l  ][1];
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// PIO Parameters
static const PIO  crt_pio = pio2;
static const uint crt_sm = 0; 

static void init_PIO() {
    
    uint crt_offset;
    pio_sm_config crt_config;
        
    pio_set_gpio_base(crt_pio, 16);
    pio_sm_claim(crt_pio, crt_sm);
    crt_offset = pio_add_program(crt_pio, &crt_program);
    crt_config = crt_program_get_default_config( crt_offset );
    sm_config_set_out_pin_base(&crt_config, GPIO64_V_HSYNC);

    // Init pins (V_SYNC may be used either for sync, or audio output)
    gpio_init(GPIO64_V_VSYNC); 
    gpio_put(GPIO64_V_VSYNC, false);
    gpio_set_dir(GPIO64_V_VSYNC, GPIO_OUT); 
    for (int pin = GPIO64_V_HSYNC; pin <= GPIO64_V_RED; pin++) {
        
        //gpio_set_drive_strength(pin, GPIO_DRIVE_STRENGTH_2MA);
        //gpio_set_slew_rate(pin, GPIO_SLEW_RATE_SLOW);
        gpio_set_function(pin, PIO_FUNCSEL_NUM(crt_pio, pin));
    }    

    // Init state machine
    pio_sm_init(crt_pio, crt_sm, crt_offset, &crt_config);
    pio_sm_set_clkdiv_int_frac8(crt_pio, crt_sm, crt_type.pio_clk_divider, 0);
    pio_sm_set_enabled(crt_pio, crt_sm, true);
}

///////////////////////////////////////////////////////////////////////////////
// DMA Parameters
static uint dma_channels[2];
static uint dma_idx;

static void dma_irq_handler_crt();

static void configure_dma() {

    size_t h_whole = crt_type.h_sync_pulse + crt_type.h_back_porch + crt_type.h_visible_area + crt_type.h_front_porch;

    // DMA CLEANUP
    irq_set_enabled(DMA_IRQ_0, false);

    dma_channel_cleanup(dma_channels[0]);
    dma_channel_cleanup(dma_channels[1]);
    
    //bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
    

    for (int c_idx = 0; c_idx<2; c_idx++) {
        dma_channel_config c = dma_channel_get_default_config(dma_channels[c_idx]);

        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(crt_pio, crt_sm, true)) ;
        channel_config_set_chain_to(&c, dma_channels[1 -c_idx]);

        dma_channel_configure(
            dma_channels[c_idx],
            &c,
            &crt_pio->txf[crt_sm],
            &linebuffer_blank[0],
            (h_whole * crt_type.cycles_per_pixel / 2) / sizeof(uint32_t),
            false
        );

        dma_channel_set_irq0_enabled(dma_channels[c_idx], true);
    }
    
    // Register IRQ Handler
    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq_handler_crt);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(dma_channels[0]);
    line_idx = 1; // We configured line0 and line1
    frame_idx = 0;
    dma_idx = 0;
}


///////////////////////////////////////////////////////////////////////////////
// Framebuffer Filling Callback
static std::function<const uint8_t *(uint16_t)> get_framebuffer_line;

///////////////////////////////////////////////////////////////////////////////
// SYNC Levels and Patterns
static bool   active_vsync_polarity_level() { return crt_type.v_sync_polarity == CRT_Type::V_SYNC_POLARITY_POSITIVE ? true : false; }
static bool inactive_vsync_polarity_level() { return crt_type.v_sync_polarity == CRT_Type::V_SYNC_POLARITY_POSITIVE ? false : true; }

static bool   active_hsync_polarity_level() { return crt_type.h_sync_polarity == CRT_Type::H_SYNC_POLARITY_POSITIVE ? true : false; }
static bool inactive_hsync_polarity_level() { return crt_type.h_sync_polarity == CRT_Type::H_SYNC_POLARITY_POSITIVE ? false : true; }

static void init_precomputed_sync_patterns() {

    size_t h_whole = crt_type.h_sync_pulse + crt_type.h_back_porch + crt_type.h_visible_area + crt_type.h_front_porch;

    // INIT SYNC PATTERNS
    if (crt_type.sync_type == CRT_Type::VGA_H_V_SYNC and crt_type.interlace == CRT_Type::NON_INTERLACED) {
        for (size_t i=0; i<h_whole; i++) {

            bool hsync = inactive_hsync_polarity_level();
            if (i < crt_type.h_sync_pulse) hsync = active_hsync_polarity_level();

            PixelPattern( hsync ).draw(&linebuffer_blank[0],      i);
            PixelPattern( hsync ).draw(&linebuffer_vsync[0],      i);
            PixelPattern( hsync ).draw(&linebuffer_visible[0][0], i);
            PixelPattern( hsync ).draw(&linebuffer_visible[1][0], i);
        }
        gpio_init(GPIO64_V_VSYNC); 
        gpio_put(GPIO64_V_VSYNC, false);
        gpio_set_dir(GPIO64_V_VSYNC, GPIO_OUT);

    } else if (crt_type.sync_type == CRT_Type::COMPOSITE_SYNC_XOR and crt_type.interlace == CRT_Type::NON_INTERLACED) {
        for (size_t i=0; i<h_whole; i++) {

            bool hsync = inactive_hsync_polarity_level();
            if (i < crt_type.h_sync_pulse) hsync = active_hsync_polarity_level();

            PixelPattern( hsync xor inactive_vsync_polarity_level() ).draw(&linebuffer_blank[0],      i);
            PixelPattern( hsync xor   active_vsync_polarity_level() ).draw(&linebuffer_vsync[0],      i);
            PixelPattern( hsync xor inactive_vsync_polarity_level() ).draw(&linebuffer_visible[0][0], i);
            PixelPattern( hsync xor inactive_vsync_polarity_level() ).draw(&linebuffer_visible[1][0], i);
        }

        gpio_set_function(GPIO64_V_VSYNC, GPIO_FUNC_PWM);

    } else if (crt_type.sync_type == CRT_Type::COMPOSITE_SYNC_AND and crt_type.interlace == CRT_Type::NON_INTERLACED) {
        for (size_t i=0; i<h_whole; i++) {

            bool hsync = inactive_hsync_polarity_level();
            if (i < crt_type.h_sync_pulse) hsync = active_hsync_polarity_level();

            PixelPattern( hsync and inactive_vsync_polarity_level() ).draw(&linebuffer_blank[0],      i);
            PixelPattern( hsync and   active_vsync_polarity_level() ).draw(&linebuffer_vsync[0],      i);
            PixelPattern( hsync and inactive_vsync_polarity_level() ).draw(&linebuffer_visible[0][0], i);
            PixelPattern( hsync and inactive_vsync_polarity_level() ).draw(&linebuffer_visible[1][0], i);
        }

        gpio_set_function(GPIO64_V_VSYNC, GPIO_FUNC_PWM);

    } else if (crt_type.sync_type == CRT_Type::DISABLED_SYNC) {
        for (size_t i=0; i<h_whole; i++) {

            bool hsync = inactive_hsync_polarity_level();
            PixelPattern( hsync ).draw(&linebuffer_blank[0],      i);
            PixelPattern( hsync ).draw(&linebuffer_vsync[0],      i);
            PixelPattern( hsync ).draw(&linebuffer_visible[0][0], i);
            PixelPattern( hsync ).draw(&linebuffer_visible[1][0], i);
        }
        gpio_set_function(GPIO64_V_VSYNC, GPIO_FUNC_PWM);
    } else {

        //_putchar('X');
    }
}

static void init_test_pattern() {
    // DRAW A TEST PATTERN
    for (size_t l=0; l<2; l++) {
        for (size_t i=0; i < crt_type.h_visible_area; i++) {
            palette_patterns[(i/2) % 256 ].draw( &linebuffer_visible[l][0], crt_type.h_sync_pulse + crt_type.h_back_porch + i);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// IRQ

template<size_t CYCLES>
static void fill_line(const uint8_t *framebuffer_line) {

    uint32_t *dma_line = (uint32_t *)&linebuffer_visible[dma_idx];

    dma_line += ( (crt_type.h_sync_pulse + crt_type.h_back_porch) * CYCLES ) / 8;

    for (size_t i=0; i < crt_type.h_visible_area; i+=4) {
        for (size_t j=0; j < 4; j++) {
            uint8_t color_idx = *framebuffer_line++;
            const uint32_t *palette_idx = (const uint32_t *)&palette_patterns[color_idx].data[0];

            for (size_t k=0; k < CYCLES / 8; k++) {
                *dma_line++ = *palette_idx++;
            }
        }
    }
}

static queue_t queue;
struct QueueItem { uint32_t idx; std::function<void(void)> hook; };
std::array<std::function<void(void)>, 32> hooks = {};

static void dma_irq_handler_crt() {

    {
        QueueItem queue_item;
        if (queue_try_remove(&queue, &queue_item)) {
            if (queue_item.idx < hooks.size()) {
                hooks[queue_item.idx] = queue_item.hook;
            } else {
                queue_item.hook();
            }
        }
    }
 
    if (CRT_engine_state == CRT_REQUEST_RUN) {
        configure_dma();
        CRT_engine_state = CRT_RUNNING;
    }
    if (CRT_engine_state == CRT_REQUEST_PARK) CRT_engine_state = CRT_PARKED;
    if (CRT_engine_state == CRT_PARKED) line_idx = 0;

    if (dma_hw->ints0 & (1u << dma_idx)) {
        dma_hw->ints0 = (1u << dma_idx);  // Clear interrupt
    }

    if ( crt_type.sync_type == CRT_Type::VGA_H_V_SYNC) {
        if (line_idx == 0                     ) gpio_put(GPIO64_V_VSYNC,   active_vsync_polarity_level());
        if (line_idx == crt_type.v_sync_pulse ) gpio_put(GPIO64_V_VSYNC, inactive_vsync_polarity_level());
    }

    line_idx = line_idx + 1;
    if (line_idx == crt_type.v_sync_pulse + crt_type.v_back_porch + crt_type.v_visible_area + crt_type.v_front_porch) {
        line_idx = 0;
        frame_idx ++;
    };


    if ( line_idx < crt_type.v_sync_pulse) { // Sync pulse
            
        dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_vsync[0]), false);

    } else if ( line_idx < crt_type.v_sync_pulse + crt_type.v_back_porch ) { //Back porch
            
        dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_blank[0]), false);
        
    } else if ( line_idx < crt_type.v_sync_pulse + crt_type.v_back_porch + crt_type.v_visible_area ) { // Main image

        dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_visible[dma_idx][0]), false);
        
        //_putchar('A');    

        if (1) if (get_framebuffer_line) {

            //_putchar('B');    

            int l = line_idx -( crt_type.v_sync_pulse + crt_type.v_back_porch );

            const uint8_t *framebuffer_line = get_framebuffer_line(l);

            if      (crt_type.cycles_per_pixel ==  8) fill_line< 8>(framebuffer_line);
            else if (crt_type.cycles_per_pixel == 16) fill_line<16>(framebuffer_line);
            else if (crt_type.cycles_per_pixel == 24) fill_line<24>(framebuffer_line);
            else if (crt_type.cycles_per_pixel == 32) fill_line<32>(framebuffer_line);
            else if (crt_type.cycles_per_pixel == 40) fill_line<40>(framebuffer_line);
            else if (crt_type.cycles_per_pixel == 48) fill_line<48>(framebuffer_line);
        }
    } else { // Front porch

        dma_channel_set_read_addr(dma_channels[dma_idx], &(linebuffer_blank[0]), false);
    }

    if (line_idx < hooks.size() and hooks[line_idx]) hooks[line_idx]();

    dma_idx = (1-dma_idx);
}



///////////////////////////////////////////////////////////////////////////////
// INIT CORE1
static Multitask::CallAgain core1_init_crt() {

    // INIT PIO
    init_PIO();

    // INIT DMA
    dma_channels[0] = dma_claim_unused_channel(true);
    dma_channels[1] = dma_claim_unused_channel(true);
    configure_dma();

    return Multitask::DO_NOT_CALL_AGAIN;
}






void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b) {

    palette_rgb[idx][0] = r; palette_rgb[idx][1] = g; palette_rgb[idx][2] = b; 

    palette_patterns[idx] = PixelPattern( inactive_hsync_polarity_level(), dither_patterns[r], dither_patterns[g], dither_patterns[b]);
}


void init(const CRT_Type &crt_type_, std::function<const uint8_t *(uint16_t)> get_framebuffer_line_) {

    // PARK CRT LOGIC IF ACTIVE
    if (CRT_engine_state == CRT_RUNNING) {
        
        CRT_engine_state = CRT_REQUEST_PARK;
        do { busy_wait_us(64000); } while ( CRT_engine_state != CRT_PARKED );
    }

    //putstring("CRT_A\n");

    // Update variables
    crt_type = crt_type_;
    get_framebuffer_line = get_framebuffer_line_;

    // Update cpu speed
    set_speed(crt_type.vco, crt_type.sys_div, crt_type.flash_div, crt_type.voltage);

    // We need to replace the precomputed sync patterns.
    init_precomputed_sync_patterns();

    //putstring("CRT_B\n");

    // Recalculate the palette.
    if (crt_type.dither_type == CRT_Type::DITHER_ANALOG) init_dither_patterns_Analog();
    if (crt_type.dither_type == CRT_Type::DITHER_VGA640_LG_LCD) init_dither_patterns_VGA640_LG_LCD();
    for (int i=0; i<256; i++) set_palette( i, palette_rgb[i][0], palette_rgb[i][1], palette_rgb[i][2]);

    // And init the core if needed:
    if (CRT_engine_state == CRT_NOT_INITIALIZED) {
        queue_init(&queue, sizeof(QueueItem), 16);
        init_test_pattern();
        Multitask::add_task(core1_init_crt);
    }

    //putstring("CRT_D\n");

    // we request the core to start:         
    CRT_engine_state = CRT_REQUEST_RUN;

    // And wait to confirm it started:         
    do { busy_wait_us(64000); } while ( CRT_engine_state != CRT_RUNNING );
    
}

void add_hook_line(size_t idx, std::function<void(void)> hook) {
    QueueItem qi = {idx, hook};
    queue_add_blocking(&queue, &qi);
}

void add_hook_single(std::function<void(void)> hook) {
    QueueItem qi = { uint32_t(-1), hook };
    queue_add_blocking(&queue, &qi);
}

}