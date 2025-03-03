#include <config/config.h>

#include "pico/flash.h"
#include "hardware/flash.h"

static uint32_t compute_crc32( const Config::Config &config)  {

    uint32_t crc = 0xFFFFFFFF;  // Initial value
    const uint8_t* bytes = (const uint8_t*)&config;
    uint32_t polynomial = 0x04C11DB7;  // IEEE 802.3 CRC-32 polynomial

    for (size_t i = 0; i < sizeof(Config::Config); ++i) {

        crc ^= (uint32_t)(bytes[i]) << 24;  // Align byte with upper bits

        for (int i = 0; i < 8; i++) {  // Process each bit
            if (crc & 0x80000000) {  // If MSB is 1, perform XOR with polynomial
                crc = (crc << 1) ^ polynomial;
            } else {
                crc <<= 1;
            }
        }
    }

    DBG::msg<DEBUG_INFO>("crc: ", crc);

    return ~crc;  // Final XOR step
}

struct CRCConfig {
    Config::Config config;
    uint32_t crc32;
};

namespace Config {
    
    void Config::Config::read_from_flash() {

        static_assert(sizeof(CRCConfig) < 16*1024, "Configuration is too large");

        const CRCConfig &crc_config_rom_a = *(const CRCConfig *)FLASH_CONF_A;
        const CRCConfig &crc_config_rom_b = *(const CRCConfig *)FLASH_CONF_B;

        if (crc_config_rom_a.crc32 == compute_crc32(crc_config_rom_a.config)) {
            DBG::msg<DEBUG_INFO>("Read Config A from FLASH successful!");
            *this = crc_config_rom_a.config;      
        } else if (crc_config_rom_b.crc32 == compute_crc32(crc_config_rom_b.config)) {
            DBG::msg<DEBUG_INFO>("Read Config B from FLASH successful!");
            *this = crc_config_rom_b.config;      
        } else {
            DBG::msg<DEBUG_INFO>("Read Config from FLASH failed, loading defaults.");
            *this = Config();
        }
    };

    void __no_inline_not_in_flash_func(Config::Config::save_to_flash)() const {
        static_assert(sizeof(CRCConfig) < 16 * 1024, "Configuration is too large");

        // Compute CRC before writing
        CRCConfig crc_config_to_write;
        crc_config_to_write.config = *this;
        crc_config_to_write.crc32 = compute_crc32(crc_config_to_write.config);

        // Determine which flash slot to use
        const CRCConfig& crc_config_rom_a = *(const CRCConfig*)FLASH_CONF_A;
        const CRCConfig& crc_config_rom_b = *(const CRCConfig*)FLASH_CONF_B;

        // Disable interrupts to prevent execution from Flash during erase/write
        uint32_t interrupts = save_and_disable_interrupts();

        flash_range_erase(FLASH_CONF_A % (16*1024*1024), 16*1024);
        flash_range_program(FLASH_CONF_A % (16*1024*1024), reinterpret_cast<const uint8_t*>(&crc_config_to_write), sizeof(CRCConfig));

        flash_range_erase(FLASH_CONF_B % (16*1024*1024), 16*1024);
        flash_range_program(FLASH_CONF_B % (16*1024*1024), reinterpret_cast<const uint8_t*>(&crc_config_to_write), sizeof(CRCConfig));

        // Restore interrupts
        restore_interrupts(interrupts);
    }
}