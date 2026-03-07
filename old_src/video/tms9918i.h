#pragma once 

#include "crt.h"
#include "bus/cartridge.h"

namespace Cartridges {
    static Cartridge TMS9918I(uint8_t port0 = 0x98, uint8_t port1 = 0x99, const CRT::CRT_Type &type = CRT::VGA320x480_60Hz );
}