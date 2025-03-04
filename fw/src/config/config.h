#pragma once

#include <board.h>
#include <bus/slot.h>

///////////////////////////////////////////////////////////////////////////////
// config represents the saved configuration of the cartridge.
// Should only be accessed during reset, or when it is updated.
// 

namespace Config {

    enum Status : bool { DISABLED = false,  ENABLED = true };

    enum CRTDevice : uint8_t  { VGA320x480_60Hz };

    enum VDPDevice : uint8_t { TMS9918 };

    enum WIFI_MODE : uint8_t { HOST, CLIENT, AUTO };

    struct Config {

        struct {

            Status read_access = ENABLED;
            Status write_access = ENABLED;
            Status menu_on_boot = ENABLED;
            Status menu_on_request = ENABLED;

            uint32_t version = 0;
        } configuration;

        struct {

            struct { Status write_access = ENABLED; } firmware;
            struct { Status write_access = ENABLED; } configuration;
            std::bitset<32*1024/4> block_access = {0};
        } flash;

        struct {
            Status expander = DISABLED;

            Cartridge subslots[4] = {

                //{ .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                //{ .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                //{ .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                //{ .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },

                //{ .cartridge_type = SUBSLOT_RAM, .rom_base = nullptr, .ram_base = (uint8_t *)0x20020000U },
                //{ .cartridge_type = SUBSLOT_RAM, .rom_base = nullptr, .ram_base = (uint8_t *)0x20030000U },
                //{ .cartridge_type = SUBSLOT_RAM, .rom_base = nullptr, .ram_base = (uint8_t *)0x20040000U },
                //{ .cartridge_type = SUBSLOT_RAM, .rom_base = nullptr, .ram_base = (uint8_t *)0x20050000U },

                { .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                { .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                { .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },
                { .cartridge_type = SUBSLOT_DISABLED, .rom_base = nullptr, .ram_base = nullptr },


            };
        } slot;

        struct USB {

            Status status = DISABLED;
            Status host_mode = DISABLED;
            Status device_mode = DISABLED;
        } usb;

        struct {
            Status status = ENABLED;
            Status respond_to_reads = DISABLED;
            Status irq_generation = DISABLED;
            VDPDevice vdp_device = TMS9918;
            CRTDevice crt_device = VGA320x480_60Hz;
            uint8_t base_port = 0x98;
        } vdp;

        struct {
            Status status = ENABLED;
        } oled;

        struct {

            Status status = ENABLED;
            WIFI_MODE mode = AUTO;
            char access_point[128] = "MSX_JLPiCart";
            char password[128] = "Backed Potaito";
        } esp32;

        struct {
            Status status = DISABLED;
        } eink;

        struct {
            struct AudioDevice {
                Status status = DISABLED;

                Status output_crt = ENABLED;
                Status output_msx = DISABLED;

                Status respond_to_reads = DISABLED;
                uint8_t base_port = 0xA0;
            };
            AudioDevice PSG0, PSG1, SCC;
        } audio;

        void save_to_flash() const;

        void read_from_flash();
    };
}
