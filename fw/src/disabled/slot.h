#pragma once

#include <bus/bus.h>
#include <config/config.h>

namespace BUS {
    
    void init_subslot(const Config::Cartridge &cartridge, SubslotIndex subslot_idx);
}

