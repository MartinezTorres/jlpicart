// mappers.cc — MSX mapper implementations.

#include "bus/mappers.h"
#include "peripherals/scc.h"
#include "platform/gpio_defs.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Reset callbacks — restore per-mapper bank state to power-on defaults.
// Called by BUS::reset_callback when the MSX /RESET line is asserted.
// Each function mirrors the initial memory_read_addresses[] setup performed
// by the corresponding mapper_setup_XXX() call.
// ---------------------------------------------------------------------------

static void konami_reset_fn(Cartridge& c) {
    for (int i = 0; i < 4; ++i)
        c.memory_read_addresses[2 + i] = &c.rom_base[i * 8192u];
}

static void ascii8_reset_fn(Cartridge& c) {
    for (int i = 0; i < 8; ++i)
        c.memory_read_addresses[i] = &c.rom_base[0];
}

static void ascii16_reset_fn(Cartridge& c) {
    for (int i = 0; i < 8; ++i)
        c.memory_read_addresses[i] = &c.rom_base[(i % 2u) * 8192u];
}

static void konami_scc_reset_fn(Cartridge& c) {
    konami_reset_fn(c);
    // SccState* is stashed in c.ram_base by scc_setup().
    if (c.ram_base) {
        SccState* ss = reinterpret_cast<SccState*>(c.ram_base);
        scc_reset(*ss);
    }
}

// ---------------------------------------------------------------------------
// Banking callbacks — run from SRAM (RAMFUNC) to avoid XIP cache stalls.
// Each callback decodes the address and data from the raw GPIO bus word and
// updates the affected memory_read_addresses[] entry in the cartridge.
// ---------------------------------------------------------------------------

static std::pair<bool, uint8_t> RAMFUNC(konami_write_cb)(Cartridge& c, uint32_t bus) {
    // Konami: write address determines which 8 KB page to switch.
    // Page index = (A[15:0] / 8 KB) % 8.
    uint32_t page    = ((bus >> GPIO_A0) / 0x2000u) % 8u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[page] = &c.rom_base[segment * 8192u];
    return {false, 0};
}

static std::pair<bool, uint8_t> RAMFUNC(ascii8_write_cb)(Cartridge& c, uint32_t bus) {
    // ASCII8: four 8 KB windows, each selected by A[12:11] within the register area.
    // Page index within the 4-window bank = (A[15:0] / 2 KB) % 4.
    uint32_t page    = ((bus >> GPIO_A0) / 0x800u) % 4u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[2u + page] = &c.rom_base[segment * 8192u];
    return {false, 0};
}

static std::pair<bool, uint8_t> RAMFUNC(ascii16_write_cb)(Cartridge& c, uint32_t bus) {
    // ASCII16: two 16 KB windows; bit 12 of the address selects window 0 or 1.
    // Each window spans two consecutive 8 KB segments.
    uint32_t window  = ((bus >> GPIO_A0) / 0x1000u) % 2u;
    uint32_t segment = (bus  >> GPIO_D0) & 0xFFu;
    c.memory_read_addresses[2u + 2u * window + 0u] = &c.rom_base[(2u * segment + 0u) * 8192u];
    c.memory_read_addresses[2u + 2u * window + 1u] = &c.rom_base[(2u * segment + 1u) * 8192u];
    return {false, 0};
}

// ---------------------------------------------------------------------------
// mapper_setup_rom
// ---------------------------------------------------------------------------

void mapper_setup_rom(Cartridge& c, const uint8_t* rom_base, size_t rom_size) {
    c.clear();
    c.name     = "rom";
    c.rom_base = rom_base;
    size_t seg_count = (rom_size + 8191u) / 8192u;
    if (seg_count < 1u)  seg_count = 1u;
    if (seg_count > 8u)  seg_count = 8u;
    for (size_t i = 0; i < 8u; ++i) {
        // Mirror: if ROM is smaller than 64 KB, wrap within available segments.
        c.memory_read_addresses[i] = &rom_base[(i % seg_count) * 8192u];
    }
}

// ---------------------------------------------------------------------------
// mapper_setup_rom_32k_mirrored
// ---------------------------------------------------------------------------

void mapper_setup_rom_32k_mirrored(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "rom_32k_mirrored";
    c.rom_base = rom_base;
    // ((i+2)%4) pattern: designed for 32 KB ROMs whose header sits at 0x4000.
    // MSX address 0x0000–0x3FFF → ROM 0x4000–0x7FFF; 0x4000–0x7FFF → ROM 0x0000–0x3FFF.
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[((i + 2) % 4) * 8192u];
    }
}

// ---------------------------------------------------------------------------
// mapper_setup_konami
// ---------------------------------------------------------------------------

void mapper_setup_konami(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "konami";
    c.rom_base = rom_base;
    // Initial state: ROM segments 0–3 mapped to MSX pages 2–5 (0x4000–0xBFFF).
    for (int i = 0; i < 4; ++i) {
        c.memory_read_addresses[2 + i]  = &rom_base[i * 8192u];
        c.memory_write_callbacks[2 + i] = konami_write_cb;
    }
    c.reset_fn = konami_reset_fn;
}

// ---------------------------------------------------------------------------
// mapper_setup_konami_z
// ---------------------------------------------------------------------------

void mapper_setup_konami_z(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "konami_z";
    c.rom_base = rom_base;
    // Initial state: ROM segments 0–3 at pages 2–5.
    for (int i = 0; i < 4; ++i) {
        c.memory_read_addresses[2 + i] = &rom_base[i * 8192u];
    }
    // Only pages 4–5 (0x8000–0xBFFF) are switchable (no 0x6000 register).
    c.memory_write_callbacks[4] = konami_write_cb;
    c.memory_write_callbacks[5] = konami_write_cb;
    c.reset_fn = konami_reset_fn;
}

// ---------------------------------------------------------------------------
// mapper_setup_ascii8
// ---------------------------------------------------------------------------

void mapper_setup_ascii8(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "ascii8";
    c.rom_base = rom_base;
    // All 8 segments initially point to ROM segment 0.
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[0];
    }
    // Writes to segment 3 (0x6000–0x7FFF) switch the four 8 KB windows.
    c.memory_write_callbacks[3] = ascii8_write_cb;
    c.reset_fn = ascii8_reset_fn;
}

// ---------------------------------------------------------------------------
// mapper_setup_ascii16
// ---------------------------------------------------------------------------

void mapper_setup_ascii16(Cartridge& c, const uint8_t* rom_base) {
    c.clear();
    c.name     = "ascii16";
    c.rom_base = rom_base;
    // All 8 segments initially interleave segments 0 and 1 (i % 2 pattern).
    for (int i = 0; i < 8; ++i) {
        c.memory_read_addresses[i] = &rom_base[(i % 2u) * 8192u];
    }
    // Writes to segment 3 (0x6000–0x7FFF) switch the two 16 KB windows.
    c.memory_write_callbacks[3] = ascii16_write_cb;
    c.reset_fn = ascii16_reset_fn;
}

// ---------------------------------------------------------------------------
// mapper_setup_konami_scc
// ---------------------------------------------------------------------------

void mapper_setup_konami_scc(Cartridge& c, const uint8_t* rom_base,
                              SccState& state) {
    // Segments 0–1 (0x0000–0x3FFF): unused for Konami SCC carts.
    // Segments 2–5 (0x4000–0xBFFF): switchable 8 KB banks, same as Konami.
    // Segment 4 (0x8000–0x9FFF): also hosts the SCC register space at 0x9800.
    //
    // scc_setup() installs scc_read_cb and scc_write_cb on segment 4.
    // Segments 2, 3, 5 use the plain konami_write_cb for bank switching.

    c.clear();
    c.name     = "konami_scc";
    c.rom_base = rom_base;
    // Initial bank mapping: segments 0–3 at pages 2–5.
    for (int i = 0; i < 4; ++i) {
        c.memory_read_addresses[2 + i]  = &rom_base[i * 8192u];
        c.memory_write_callbacks[2 + i] = konami_write_cb;
    }
    // Segment 4 callbacks are overridden by scc_setup() to handle both
    // bank switching (0x8000–0x97FF) and SCC registers (0x9800–0x9FFF).
    scc_setup(c, state);
    c.reset_fn = konami_scc_reset_fn;
}

// ---------------------------------------------------------------------------
// mapper_setup_ram
// ---------------------------------------------------------------------------

void mapper_setup_ram(Cartridge& c, uint8_t* ram_base, size_t ram_size) {
    c.clear();
    c.name     = "ram";
    c.ram_base = ram_base;
    size_t seg_count = (ram_size + 8191u) / 8192u;
    if (seg_count > 8u) seg_count = 8u;
    for (size_t i = 0; i < seg_count; ++i) {
        c.memory_read_addresses[i]  = &ram_base[i * 8192u];
        c.memory_write_addresses[i] = &ram_base[i * 8192u];
    }
}

// ---------------------------------------------------------------------------
// MappingPlan builders (moved from mapping_plan.cc)
// ---------------------------------------------------------------------------

#include <cstring>

MapperType mapper_type_from_string(const char* s) {
    if (!s || s[0] == '\0')           return MapperType::NONE;
    if (strcmp(s, "rom")              == 0) return MapperType::ROM;
    if (strcmp(s, "rom_32k_mirrored") == 0) return MapperType::ROM_32K_MIRRORED;
    if (strcmp(s, "konami")           == 0) return MapperType::KONAMI;
    if (strcmp(s, "konami_scc")       == 0) return MapperType::KONAMI_SCC;
    if (strcmp(s, "konami_z")         == 0) return MapperType::KONAMI_Z;
    if (strcmp(s, "ascii8")           == 0) return MapperType::ASCII8;
    if (strcmp(s, "ascii16")          == 0) return MapperType::ASCII16;
    if (strcmp(s, "ram")              == 0) return MapperType::RAM;
    return MapperType::NONE;
}

const char* mapper_type_to_string(MapperType t) {
    switch (t) {
        case MapperType::ROM:              return "rom";
        case MapperType::ROM_32K_MIRRORED: return "rom_32k_mirrored";
        case MapperType::KONAMI:           return "konami";
        case MapperType::KONAMI_SCC:       return "konami_scc";
        case MapperType::KONAMI_Z:         return "konami_z";
        case MapperType::ASCII8:           return "ascii8";
        case MapperType::ASCII16:          return "ascii16";
        case MapperType::RAM:              return "ram";
        case MapperType::NONE:             return "none";
    }
    return "none";
}

MappingPlan mapping_plan_from_payload_record(const PayloadRecord& record)
{
    MappingPlan plan = {};
    MapperType mt = mapper_type_from_string(record.mapper_type);
    if (mt != MapperType::NONE && record.data_size > 0) {
        MappingEntry& entry = plan.entries[0];
        entry.mapper_type   = mt;
        entry.subslot       = record.subslot;
        entry.rom_size      = record.data_size;
        entry.ram_data      = nullptr;
        entry.ram_size      = 0;
#ifndef JLPICART_HOST_TEST
        static constexpr uint32_t XIP_BASE = 0x10000000u;
        entry.rom_data = reinterpret_cast<const uint8_t*>(XIP_BASE + record.data_flash_offset);
#else
        entry.rom_data = nullptr;
#endif
        plan.entry_count = 1;
        plan.expanded    = false;
    }
    for (uint8_t i = 0; i < record.device_count && i < PAYLOAD_DEVICES_MAX; ++i) {
        const PayloadDeviceRecord& pdr = record.devices[i];
        const PeripheralDescriptor* desc = find_peripheral_by_id(pdr.type);
        if (!desc || desc->memory_mapped) continue;
        if (plan.io_device_count >= MAPPING_MAX_IO_DEVICES) break;
        IoDeviceEntry& io = plan.io_devices[plan.io_device_count++];
        if (strcmp(desc->name, "psg") == 0) {
            io.type = IoDeviceType::PSG;
        } else if (strcmp(desc->name, "opl4") == 0) {
            io.type = IoDeviceType::OPL4;
            size_t plen = strnlen(pdr.params, sizeof(pdr.params));
            if (plen > 0 && plen < PAYLOAD_ID_MAX)
                memcpy(io.wave_payload_id, pdr.params, plen + 1u);
        }
    }
    return plan;
}

MappingPlan mapper_plan_from_manifest(const CollectionManifest& manifest,
                                       uint8_t payload_index)
{
    MappingPlan plan = {};
    if (payload_index >= manifest.payload_count) return plan;
    const PayloadEntry& pe = manifest.payloads[payload_index];
    MapperType mt = mapper_type_from_string(pe.mapper_type);
    if (mt == MapperType::NONE) return plan;
    MappingEntry& entry  = plan.entries[0];
    entry.mapper_type    = mt;
    entry.subslot        = pe.subslot;
    entry.rom_data       = nullptr;
    entry.rom_size       = 0;
    entry.ram_data       = nullptr;
    entry.ram_size       = 0;
    plan.entry_count = 1;
    plan.expanded    = false;
    for (uint8_t i = 0; i < pe.device_count && i < PAYLOAD_DEVICES_MAX; ++i) {
        const ManifestDeviceEntry& de = pe.devices[i];
        const PeripheralDescriptor* desc = de.descriptor;
        if (!desc || desc->memory_mapped) continue;
        if (plan.io_device_count >= MAPPING_MAX_IO_DEVICES) break;
        IoDeviceEntry& io = plan.io_devices[plan.io_device_count++];
        if (strcmp(desc->name, "psg") == 0) {
            io.type = IoDeviceType::PSG;
        } else if (strcmp(desc->name, "opl4") == 0) {
            io.type = IoDeviceType::OPL4;
            size_t plen = strnlen(de.params, sizeof(de.params));
            if (plen > 0 && plen < PAYLOAD_ID_MAX)
                memcpy(io.wave_payload_id, de.params, plen + 1u);
        }
    }
    return plan;
}
