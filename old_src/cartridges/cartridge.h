#pragma once

#include <algorithm>


namespace  CartridgeConfig {

    uint32_t parse_dec(const char *s);
    uint32_t parse_hex(const char *s);
    uint32_t parse_unsigned(const char *s);
    int32_t parse_signed(const char *s);
    std::pair<const char *, const char *> parse_string(const char *s);
    const char *parse_key(const char *key, const char *config);

    uint32_t get_uint32_t(const char *config, const char *key, uint32_t default_value = 0) { const char *c = CartridgeConfig::parse_key(key, config); if (c) return CartridgeConfig::parse_unsigned(c);         return default_value; }
    int32_t  get_int32_t (const char *config, const char *key, uint32_t default_value = 0) { const char *c = CartridgeConfig::parse_key(key, config); if (c) return CartridgeConfig::parse_signed(c);           return default_value; }
    void *   get_pointer (const char *config, const char *key)                             { const char *c = CartridgeConfig::parse_key(key, config); if (c) return (void *)CartridgeConfig::parse_unsigned(c); return nullptr; }
    std::pair<const char *, const char *> get_string (const char *config, const char *key) { const char *c = CartridgeConfig::parse_key(key, config); if (c) return CartridgeConfig::parse_string(c);           return {config, config}; }

}

struct Cartridge {

    const char *config = "";

    uint32_t get_uint32_t(const char *key, uint32_t default_value = 0) { return CartridgeConfig::get_uint32_t(config, key, default_value);  }
    int32_t  get_int32_t (const char *key, uint32_t default_value = 0) { return CartridgeConfig::get_int32_t(config, key, default_value);  }
    void *   get_pointer (const char *key)                             { return CartridgeConfig::get_pointer(config, key);  }
    std::pair<const char *, const char *> get_string (const char *key) { return CartridgeConfig::get_string(config, key);  }
    
    const char *name = "Disabled";

    using InitCallback = size_t(*)(Cartridge &, uint8_t *&);
    using DeinitCallback = void(*)(Cartridge &);

    DeinitCallback deinit = nullptr;

    const uint8_t *rom_base = nullptr;
    uint8_t *ram_base = nullptr;

    const uint8_t *memory_read_addresses[8] = {0};
    uint8_t *memory_write_addresses[8] = {0};

    using GPIOBus32 = uint32_t;
    using BusCallback = std::pair<bool, uint8_t>(*)(Cartridge &, GPIOBus32);

    BusCallback memory_read_callbacks[8] = {0};
    BusCallback memory_write_callbacks[8] = {0};

    BusCallback io_read_callbacks[256] = {0};
    BusCallback io_write_callbacks[256] = {0};
};

struct CartridgeRegistry {

    struct CartridgeDescription {
        const char *default_configuration = "";
        Cartridge::InitCallback init = nullptr;
        Cartridge::DeinitCallback deinit = nullptr;
    };

    static CartridgeRegistry& getInstance() {
        static CartridgeRegistry instance;
        return instance;
    }

    static uint8_t get_register_cartridge(const CartridgeDescription& cartidge_description) { getInstance().get_register_cartridge_int(cartidge_description); }
    uint8_t get_register_cartridge_int(const CartridgeDescription& cartidge_description) {
        auto &registry = getInstance();
        if (registry.num_cartridges == MAX_CARTRIDGE_TYPES) return; // let's not bother with debugging, as it won't be initialized yet.

        //TODO: ensure that all cartridges have different identifiers

        registry.cartidge_descriptions[registry.num_cartridges++] = cartidge_description;
    }

    static CartridgeDescription *find(std::string_view s) { getInstance().find_int(s); }
    CartridgeDescription *find_int(std::string_view s) {
            for (size_t i = 0; i < num_cartridges; i++) {
            auto cartridge_class = CartridgeConfig::get_string( cartidge_descriptions[i].default_configuration, "class");
            if (s == cartridge_class) return cartidge_descriptions[i];
        }
        return nullptr;
    }

    static constexpr const size_t MAX_CARTRIDGE_TYPES = 64;
    size_t num_cartridges = 0;
    CartridgeDescription cartidge_descriptions[MAX_CARTRIDGE_TYPES];
};

