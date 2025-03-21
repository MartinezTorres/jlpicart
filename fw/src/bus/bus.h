#pragma once

#include "cartridge.h"

namespace BUS {

    enum SubslotIndex : uint8_t { SUBSLOT0 = 0, SUBSLOT1 = 1, SUBSLOT2 = 2, SUBSLOT3 = 3 };
    
    using ResetCallback = void(*)();

    extern Cartridge cartridges[8];
    extern SubslotIndex subslot_indexes[4];
    extern bool is_expanded;

    extern size_t tick_last_irq;

    extern ResetCallback reset_callback;
    
    [[noreturn]] void start();
}