#pragma once

#include <bus/bus.h>

#define PARENS ()

#define EXPAND(...) EXPAND4(EXPAND4(EXPAND4(EXPAND4(__VA_ARGS__))))
#define EXPAND4(...) EXPAND3(EXPAND3(EXPAND3(EXPAND3(__VA_ARGS__))))
#define EXPAND3(...) EXPAND2(EXPAND2(EXPAND2(EXPAND2(__VA_ARGS__))))
#define EXPAND2(...) EXPAND1(EXPAND1(EXPAND1(EXPAND1(__VA_ARGS__))))
#define EXPAND1(...) __VA_ARGS__

#define FOR_EACH(macro, ...)  __VA_OPT__(EXPAND(FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define FOR_EACH_HELPER(macro, a1, ...) macro(a1) __VA_OPT__(FOR_EACH_AGAIN PARENS (macro, __VA_ARGS__))
#define FOR_EACH_AGAIN() FOR_EACH_HELPER


#define ENUM_CASE(name) case name: return #name;

#define MAKE_ENUM(type, type_int, ...)          \
enum type : type_int { __VA_ARGS__ };           \
                                                \
constexpr const char *                          \
to_cstring(type _e)                             \
{                                               \
  using enum type;                              \
  switch (_e) {                                 \
  FOR_EACH(ENUM_CASE, __VA_ARGS__)              \
  default:                                      \
    return "unknown";                           \
  }                                             \
}

MAKE_ENUM( CartridgeType , uint32_t,
    SUBSLOT_DISABLED,

    SUBSLOT_RAM,
    SUBSLOT_RAM_DEBUG,

    MAPPER_LINEAR,
    MAPPER_32K_MIRRORED,

    MAPPER_KONAMI,
    MAPPER_KONAMI_Z,

    MAPPER_ASCII8,
    MAPPER_ASCII16
);

struct Cartridge {

    const char *name = "disabled";

    CartridgeType cartridge_type = SUBSLOT_DISABLED;

    const uint8_t *rom_base = nullptr;  
    uint8_t *ram_base = nullptr;    
};

namespace BUS {
    
    void init_subslot(const Cartridge &cartridge, SubslotIndex subslot_idx);
}

