#pragma once

namespace BUS {

    enum SubslotIndex : uint8_t { SUBSLOT0 = 0, SUBSLOT1 = 1, SUBSLOT2 = 2, SUBSLOT3 = 3 };
    union SubslotRegister {
        uint8_t reg; 
        struct {
            SubslotIndex subslot0 : 2;
            SubslotIndex subslot1 : 2;
            SubslotIndex subslot2 : 2;
            SubslotIndex subslot3 : 2;
        };
    };

    using GPIOBus32 = uint32_t;

    using IOCallback = std::pair<bool, uint8_t>(*)(GPIOBus32);
    using MemCallback = std::pair<bool, uint8_t>(*)(SubslotIndex, GPIOBus32);
    
    using ResetCallback = void(*)();

    extern const uint8_t *memory_read_addresses[4][8];
    extern uint8_t *memory_write_addresses[4][8];

    extern MemCallback memory_read_callbacks[4][8];
    extern MemCallback memory_write_callbacks[4][8];

    extern IOCallback io_read_callbacks[256];
    extern IOCallback io_write_callbacks[256];

    extern SubslotIndex subslots[4];
    extern bool is_expanded;

    extern size_t tick_last_irq;

    extern ResetCallback reset_callback;
    
    void start();
}