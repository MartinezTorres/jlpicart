#pragma once 

#include "../board.h"

namespace CRT {

    constexpr size_t MAX_HORIZONTAL_RESOLUTION = 640;

    struct CRT_Type {
                
        enum EHSyncPolarity { H_SYNC_POLARITY_NEGATIVE, H_SYNC_POLARITY_POSITIVE };
        enum EVSyncPolarity { V_SYNC_POLARITY_NEGATIVE, V_SYNC_POLARITY_POSITIVE };
        enum ESyncType { VGA_H_V_SYNC, COMPOSITE_SYNC_XOR, COMPOSITE_SYNC_AND, DISABLED_SYNC };
        enum EInterlace { NON_INTERLACED, INTERLACED };
        enum EDither { DITHER_ANALOG, DITHER_VGA640_LG_LCD };

        size_t vco, sys_div, flash_div;
        enum vreg_voltage voltage;

        size_t cycles_per_pixel, pio_clk_divider;
        EDither dither_type;

        EHSyncPolarity h_sync_polarity;
        EVSyncPolarity v_sync_polarity;
        
        ESyncType sync_type;
        EInterlace interlace;

        size_t h_sync_pulse, h_back_porch, h_visible_area, h_front_porch;
        size_t v_sync_pulse, v_back_porch, v_visible_area, v_front_porch;
    };

    static const CRT_Type VGA320x480_60Hz = { // 302 MHz
        .vco = 906, .sys_div = 3, .flash_div = 3, .voltage = VREG_VOLTAGE_1_20,
        .cycles_per_pixel = 24, .pio_clk_divider = 1, 
        .dither_type = CRT_Type::DITHER_ANALOG,

        .h_sync_polarity = CRT_Type::H_SYNC_POLARITY_NEGATIVE, .v_sync_polarity = CRT_Type::V_SYNC_POLARITY_NEGATIVE,
        .sync_type = CRT_Type::VGA_H_V_SYNC, .interlace = CRT_Type::NON_INTERLACED, 

        .h_sync_pulse = 48, .h_back_porch = 24, .h_visible_area = 320, .h_front_porch = 8,
        .v_sync_pulse =  2, .v_back_porch = 33, .v_visible_area = 480, .v_front_porch = 10,
    };

    static const CRT_Type SCART_288p_50Hz = { // 256.5 MHz
        .vco = 1026, .sys_div = 4, .flash_div = 2, .voltage = VREG_VOLTAGE_1_15,
        .cycles_per_pixel = 48, .pio_clk_divider = 1,
        .dither_type = CRT_Type::DITHER_ANALOG,

        .h_sync_polarity = CRT_Type::H_SYNC_POLARITY_NEGATIVE, .v_sync_polarity = CRT_Type::V_SYNC_POLARITY_NEGATIVE,
        .sync_type = CRT_Type::COMPOSITE_SYNC_AND, .interlace = CRT_Type::NON_INTERLACED, 

        .h_sync_pulse = 25, .h_back_porch = 26, .h_visible_area = 284, .h_front_porch = 8,
        .v_sync_pulse =  2, .v_back_porch = 12, .v_visible_area = 288, .v_front_porch = 10,
    };

    static const CRT_Type SCART_240p_60Hz = { // 256.5 MHz
        .vco = 1026, .sys_div = 4, .flash_div = 2, .voltage = VREG_VOLTAGE_1_15,
        .cycles_per_pixel = 48, .pio_clk_divider = 1,
        .dither_type = CRT_Type::DITHER_ANALOG,

        .h_sync_polarity = CRT_Type::H_SYNC_POLARITY_NEGATIVE, .v_sync_polarity = CRT_Type::V_SYNC_POLARITY_NEGATIVE,
        .sync_type = CRT_Type::COMPOSITE_SYNC_AND, .interlace = CRT_Type::NON_INTERLACED, 

        .h_sync_pulse = 25, .h_back_porch = 22, .h_visible_area = 284, .h_front_porch = 8,
        .v_sync_pulse =  2, .v_back_porch = 10, .v_visible_area = 240, .v_front_porch = 10,
    };  

    static const CRT_Type SCART_HI_240p_60Hz = { // 256.5 MHz
        .vco = 1026, .sys_div = 4, .flash_div = 2, .voltage = VREG_VOLTAGE_1_20,
        .cycles_per_pixel = 24, .pio_clk_divider = 1,
        .dither_type = CRT_Type::DITHER_ANALOG,

        .h_sync_polarity = CRT_Type::H_SYNC_POLARITY_NEGATIVE, .v_sync_polarity = CRT_Type::V_SYNC_POLARITY_NEGATIVE,
        .sync_type = CRT_Type::COMPOSITE_SYNC_AND, .interlace = CRT_Type::NON_INTERLACED, 

        .h_sync_pulse = 25 * 2, .h_back_porch = 36 * 2 - 5 , .h_visible_area = 262 * 2, .h_front_porch = 20 * 2,
        .v_sync_pulse =  3, .v_back_porch = 13, .v_visible_area = 243, .v_front_porch = 3,
    };  

    void init(const CRT_Type &type, std::function<const uint8_t *(uint16_t)> get_framebuffer_line = std::function<const uint8_t *(uint16_t)>() );
    void set_palette(uint8_t idx, uint8_t r, uint8_t g, uint8_t b);


    void add_hook_line(size_t idx, std::function<void(void)> hook);
    void add_hook_single(std::function<void(void)> hook);
}
