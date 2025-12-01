#pragma once

struct Cartridge;
struct CartridgeRegistry {

    using InitCallback = void(*)(Cartridge &, uint8_t *&);

    static CartridgeRegistry& get_instance() { static CartridgeRegistry instance; return instance; }

    static int register_cartridge(std::string_view cartridge_type, InitCallback init) {
        
        auto reg = get_instance();
        if (reg.num_cartridges == MAX_CARTRIDGE_TYPES) return -1; // let's not bother with debugging, as it won't be initialized yet.
        reg.cartidge_descriptions[ reg.num_cartridges++ ] = { cartridge_type, init };
        return reg.num_cartridges;
    }

    static InitCallback find(std::string_view cartridge_type) { 
        
        auto reg = get_instance();
        for (size_t i = 0; i < reg.num_cartridges; i++) 
            if (cartridge_type == reg.cartidge_descriptions[i].first) 
                return reg.cartidge_descriptions[i].second;
        
        return nullptr;
    }

    static constexpr const size_t MAX_CARTRIDGE_TYPES = 64;
    size_t num_cartridges = 0;
    std::pair<std::string_view, InitCallback> cartidge_descriptions[MAX_CARTRIDGE_TYPES];
};

#define REGISTER_CARTRIDGE_TYPE(name, init)  inline int _cartridge_register_##name = CartridgeRegistry::register_cartridge(#name, init)
