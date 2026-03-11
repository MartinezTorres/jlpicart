// test_bus_mapper.cc — Stage 9: mapper setup, MappingPlan, and manifest bridge tests.

#include "bus/mapping_plan.h"
#include "mappers/mappers.h"
#include "cartridges/cartridge.h"
#include "content/manifest.h"
#include "content/manifest_parser.h"
#include "boards/gpio_defs.h"

#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Build a simulated 32-bit GPIO bus word with address and data fields.
static uint32_t make_bus_word(uint16_t address, uint8_t data) {
    return (static_cast<uint32_t>(address) << GPIO_A0) |
           (static_cast<uint32_t>(data)    << GPIO_D0);
}

// ---------------------------------------------------------------------------
// Test 1: mapper_type_from_string / mapper_type_to_string round-trip
// ---------------------------------------------------------------------------
static void test_mapper_type_strings() {
    // All named types parse correctly.
    assert(mapper_type_from_string("rom")              == MapperType::ROM);
    assert(mapper_type_from_string("rom_32k_mirrored") == MapperType::ROM_32K_MIRRORED);
    assert(mapper_type_from_string("konami")           == MapperType::KONAMI);
    assert(mapper_type_from_string("konami_z")         == MapperType::KONAMI_Z);
    assert(mapper_type_from_string("ascii8")           == MapperType::ASCII8);
    assert(mapper_type_from_string("ascii16")          == MapperType::ASCII16);
    assert(mapper_type_from_string("ram")              == MapperType::RAM);

    // Unknown or empty → NONE.
    assert(mapper_type_from_string("none")             == MapperType::NONE);
    assert(mapper_type_from_string("")                 == MapperType::NONE);
    assert(mapper_type_from_string(nullptr)            == MapperType::NONE);
    assert(mapper_type_from_string("scc")              == MapperType::NONE);

    // Round-trip: from_string(to_string(t)) == t for all non-NONE types.
    MapperType all[] = {
        MapperType::ROM, MapperType::ROM_32K_MIRRORED,
        MapperType::KONAMI, MapperType::KONAMI_Z,
        MapperType::ASCII8, MapperType::ASCII16,
        MapperType::RAM,
    };
    for (MapperType t : all) {
        assert(mapper_type_from_string(mapper_type_to_string(t)) == t);
    }
    assert(strcmp(mapper_type_to_string(MapperType::NONE), "none") == 0);

    printf("PASS test_mapper_type_strings\n");
}

// ---------------------------------------------------------------------------
// Test 2: mapper_setup_rom — read_addresses[] correct for various ROM sizes
// ---------------------------------------------------------------------------
static void test_mapper_setup_rom() {
    // 16 KB ROM (2 segments): segments 0,1 should mirror across all 8 pages.
    static uint8_t rom16k[16384];
    for (size_t i = 0; i < 16384; ++i) rom16k[i] = static_cast<uint8_t>(i & 0xFF);
    Cartridge c;
    mapper_setup_rom(c, rom16k, sizeof(rom16k));
    assert(strcmp(c.name, "rom") == 0);
    assert(c.rom_base == rom16k);
    // 2 segments → mirror i%2: pages 0,2,4,6 = seg 0; pages 1,3,5,7 = seg 1.
    assert(c.memory_read_addresses[0] == &rom16k[0]);
    assert(c.memory_read_addresses[1] == &rom16k[8192]);
    assert(c.memory_read_addresses[2] == &rom16k[0]);
    assert(c.memory_read_addresses[3] == &rom16k[8192]);
    // No write addresses for ROM.
    for (int i = 0; i < 8; ++i) assert(c.memory_write_addresses[i] == nullptr);
    // No callbacks.
    for (int i = 0; i < 8; ++i) assert(c.memory_read_callbacks[i] == nullptr);

    // 64 KB ROM (8 segments): each segment maps to its own page.
    static uint8_t rom64k[65536];
    mapper_setup_rom(c, rom64k, sizeof(rom64k));
    for (int i = 0; i < 8; ++i) {
        assert(c.memory_read_addresses[i] == &rom64k[i * 8192]);
    }

    printf("PASS test_mapper_setup_rom\n");
}

// ---------------------------------------------------------------------------
// Test 3: mapper_setup_rom_32k_mirrored — ((i+2)%4) pattern
// ---------------------------------------------------------------------------
static void test_mapper_setup_rom_32k_mirrored() {
    static uint8_t rom32k[32768];
    Cartridge c;
    mapper_setup_rom_32k_mirrored(c, rom32k);
    for (int i = 0; i < 8; ++i) {
        assert(c.memory_read_addresses[i] == &rom32k[((i + 2) % 4) * 8192]);
    }
    printf("PASS test_mapper_setup_rom_32k_mirrored\n");
}

// ---------------------------------------------------------------------------
// Test 4: mapper_setup_konami — initial addresses + simulated segment switch
// ---------------------------------------------------------------------------
static void test_mapper_setup_konami() {
    static uint8_t rom[256 * 8192];  // 2 MB ROM for segment testing
    Cartridge c;
    mapper_setup_konami(c, rom);

    // Initial: pages 2–5 map to ROM segs 0–3.
    assert(c.memory_read_addresses[0] == nullptr);
    assert(c.memory_read_addresses[1] == nullptr);
    assert(c.memory_read_addresses[2] == &rom[0 * 8192]);
    assert(c.memory_read_addresses[3] == &rom[1 * 8192]);
    assert(c.memory_read_addresses[4] == &rom[2 * 8192]);
    assert(c.memory_read_addresses[5] == &rom[3 * 8192]);

    // Write callbacks installed on pages 2–5.
    assert(c.memory_write_callbacks[2] != nullptr);
    assert(c.memory_write_callbacks[3] != nullptr);
    assert(c.memory_write_callbacks[4] != nullptr);
    assert(c.memory_write_callbacks[5] != nullptr);
    assert(c.memory_write_callbacks[0] == nullptr);
    assert(c.memory_write_callbacks[1] == nullptr);

    // Simulate a write to 0x8000 (page 4) switching to segment 7.
    // Bus word: address=0x8000, data=7.
    uint32_t bus = make_bus_word(0x8000, 7);
    auto [active, _] = c.memory_write_callbacks[4](c, bus);
    assert(!active);  // write callback does not drive data bus
    // page = (0x8000 / 0x2000) % 8 = 4 → c.memory_read_addresses[4] = rom + 7*8192.
    assert(c.memory_read_addresses[4] == &rom[7 * 8192]);

    printf("PASS test_mapper_setup_konami\n");
}

// ---------------------------------------------------------------------------
// Test 5: mapper_setup_konami_z — only pages 4–5 switchable
// ---------------------------------------------------------------------------
static void test_mapper_setup_konami_z() {
    static uint8_t rom[256 * 8192];
    Cartridge c;
    mapper_setup_konami_z(c, rom);

    // Pages 2–5 initially set; pages 2 and 3 have NO write callback.
    assert(c.memory_read_addresses[2] == &rom[0 * 8192]);
    assert(c.memory_read_addresses[3] == &rom[1 * 8192]);
    assert(c.memory_read_addresses[4] == &rom[2 * 8192]);
    assert(c.memory_read_addresses[5] == &rom[3 * 8192]);

    assert(c.memory_write_callbacks[2] == nullptr);
    assert(c.memory_write_callbacks[3] == nullptr);
    assert(c.memory_write_callbacks[4] != nullptr);
    assert(c.memory_write_callbacks[5] != nullptr);

    printf("PASS test_mapper_setup_konami_z\n");
}

// ---------------------------------------------------------------------------
// Test 6: mapper_setup_ascii8 — all pages start at seg 0; callback on seg 3
// ---------------------------------------------------------------------------
static void test_mapper_setup_ascii8() {
    static uint8_t rom[256 * 8192];
    Cartridge c;
    mapper_setup_ascii8(c, rom);

    for (int i = 0; i < 8; ++i) {
        assert(c.memory_read_addresses[i] == &rom[0]);
    }
    assert(c.memory_write_callbacks[3] != nullptr);
    for (int i = 0; i < 8; ++i) {
        if (i != 3) assert(c.memory_write_callbacks[i] == nullptr);
    }

    // Simulate ASCII8 write: address=0x6000 (A[15:0]=0x6000), data=5.
    // page = (0x6000 / 0x800) % 4 = 12 % 4 = 0 → read_addresses[2+0] = rom+5*8192.
    uint32_t bus = make_bus_word(0x6000, 5);
    auto [active, _] = c.memory_write_callbacks[3](c, bus);
    assert(!active);
    assert(c.memory_read_addresses[2] == &rom[5 * 8192]);

    printf("PASS test_mapper_setup_ascii8\n");
}

// ---------------------------------------------------------------------------
// Test 7: mapper_setup_ram — read and write addresses both set
// ---------------------------------------------------------------------------
static void test_mapper_setup_ram() {
    static uint8_t ram[32768];  // 32 KB = 4 segments
    Cartridge c;
    mapper_setup_ram(c, ram, sizeof(ram));

    assert(strcmp(c.name, "ram") == 0);
    assert(c.ram_base == ram);
    for (int i = 0; i < 4; ++i) {
        assert(c.memory_read_addresses[i]  == &ram[i * 8192]);
        assert(c.memory_write_addresses[i] == &ram[i * 8192]);
    }
    // Segments beyond RAM size are null.
    for (int i = 4; i < 8; ++i) {
        assert(c.memory_read_addresses[i]  == nullptr);
        assert(c.memory_write_addresses[i] == nullptr);
    }

    printf("PASS test_mapper_setup_ram\n");
}

// ---------------------------------------------------------------------------
// Test 8: mapper_plan_from_manifest — manifest with mapper_type and subslot
// ---------------------------------------------------------------------------
static void test_mapper_plan_from_manifest() {
    static const char kJson[] = R"({
        "format_version": "1.0",
        "collection_id": "test.col",
        "version": "1.0.0",
        "payloads": [
            {
                "payload_id": "game",
                "path": "game/",
                "mapper_type": "konami",
                "subslot": 1,
                "required_capabilities": ["sw.mapper"]
            }
        ]
    })";

    CollectionManifest manifest;
    DiagStatus ds = parse_collection_manifest(kJson, sizeof(kJson) - 1, manifest);
    assert(ds.ok());
    assert(manifest.payload_count == 1);

    // Verify manifest parsing of new fields.
    const PayloadEntry& pe = manifest.payloads[0];
    assert(strcmp(pe.mapper_type, "konami") == 0);
    assert(pe.subslot == 1);

    // Build MappingPlan from manifest.
    MappingPlan plan = mapper_plan_from_manifest(manifest, 0);
    assert(plan.entry_count == 1);
    assert(plan.entries[0].mapper_type == MapperType::KONAMI);
    assert(plan.entries[0].subslot == 1);
    assert(plan.entries[0].rom_data == nullptr);  // ROM not loaded in Stage 9
    assert(!plan.expanded);

    printf("PASS test_mapper_plan_from_manifest\n");
}

// ---------------------------------------------------------------------------
// Test 9: mapper_plan_from_manifest — out-of-range index → empty plan
// ---------------------------------------------------------------------------
static void test_mapper_plan_out_of_range() {
    static const char kJson[] = R"({
        "format_version": "1.0",
        "collection_id": "test.col",
        "version": "1.0.0",
        "payloads": [{"payload_id": "p", "path": "p/"}]
    })";

    CollectionManifest manifest;
    DiagStatus ds = parse_collection_manifest(kJson, sizeof(kJson) - 1, manifest);
    assert(ds.ok());

    // Payload 0 has no mapper_type → empty plan.
    MappingPlan plan0 = mapper_plan_from_manifest(manifest, 0);
    assert(plan0.entry_count == 0);

    // Out-of-range index → empty plan.
    MappingPlan plan1 = mapper_plan_from_manifest(manifest, 99);
    assert(plan1.entry_count == 0);

    printf("PASS test_mapper_plan_out_of_range\n");
}

// ---------------------------------------------------------------------------
// Test 10: manifest parse — subslot validation (> 3 is rejected)
// ---------------------------------------------------------------------------
static void test_manifest_subslot_validation() {
    static const char kJson[] = R"({
        "format_version": "1.0",
        "collection_id": "test.col",
        "version": "1.0.0",
        "payloads": [
            {
                "payload_id": "game",
                "path": "game/",
                "mapper_type": "rom",
                "subslot": 4
            }
        ]
    })";

    CollectionManifest manifest;
    DiagStatus ds = parse_collection_manifest(kJson, sizeof(kJson) - 1, manifest);
    assert(!ds.ok());  // subslot=4 is out of range

    printf("PASS test_manifest_subslot_validation\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_mapper_type_strings();
    test_mapper_setup_rom();
    test_mapper_setup_rom_32k_mirrored();
    test_mapper_setup_konami();
    test_mapper_setup_konami_z();
    test_mapper_setup_ascii8();
    test_mapper_setup_ram();
    test_mapper_plan_from_manifest();
    test_mapper_plan_out_of_range();
    test_manifest_subslot_validation();
    printf("All bus/mapper tests passed.\n");
    return 0;
}
