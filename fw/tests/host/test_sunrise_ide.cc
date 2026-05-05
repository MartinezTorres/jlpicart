// test_sunrise_ide.cc — Host tests for Sunrise ATA-IDE emulation.
//
// Exercises: ide_reset / ide_setup, DIAGNOSTIC, IDENTIFY, READ SECTOR(S),
// WRITE SECTOR(S) (discard), ROM banking, device control reset, and the
// sector-buffer window (0x7C00–0x7DFF).

#include "peripherals/sunrise_ide.h"
#include "bus/cartridge.h"
#include <cassert>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Synthesise a raw 32-bit GPIO bus word from address and data byte.
// GPIO_A0=0 (address at bits[15:0]), GPIO_D0=16 (data at bits[23:16]).
static inline uint32_t make_bus(uint16_t addr, uint8_t data = 0) {
    return ((uint32_t)data << 16) | (uint32_t)addr;
}

// Call a read callback and assert it drives the bus (returns {true, *}).
static uint8_t read_reg(Cartridge& c, uint16_t addr) {
    auto cb = c.memory_read_callbacks[addr < 0x6000u ? 2 : 3];
    assert(cb && "no read callback installed");
    auto [driven, val] = cb(c, make_bus(addr));
    assert(driven && "expected read callback to drive bus");
    return val;
}

// Call a read callback and assert it does NOT drive the bus (ROM fall-through).
static void read_rom_fallthrough(Cartridge& c, uint16_t addr) {
    auto cb = c.memory_read_callbacks[addr < 0x6000u ? 2 : 3];
    assert(cb && "no read callback installed");
    auto [driven, val] = cb(c, make_bus(addr));
    (void)val;
    assert(!driven && "expected ROM fall-through (driven=false)");
}

// Call a write callback.
static void write_reg(Cartridge& c, uint16_t addr, uint8_t data) {
    auto cb = c.memory_write_callbacks[addr < 0x6000u ? 2 : 3];
    assert(cb && "no write callback installed");
    cb(c, make_bus(addr, data));
}

// Transfer a full 512-byte sector from the data window (0x7C00–0x7DFF).
static void drain_sector(Cartridge& c, uint8_t* out) {
    for (int i = 0; i < 512; ++i) {
        out[i] = read_reg(c, 0x7C00u + (uint16_t)(i & 0x1FFu));
    }
}

// Push a full 512-byte sector to the data window.
static void push_sector(Cartridge& c, const uint8_t* in) {
    for (int i = 0; i < 512; ++i) {
        write_reg(c, 0x7C00u + (uint16_t)(i & 0x1FFu), in[i]);
    }
}

static Cartridge g_cart;
static IdeState  g_ide;

static void setup_empty_disk() {
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, nullptr, 0u);
}

// ---------------------------------------------------------------------------
// Test: power-on register values
// ---------------------------------------------------------------------------

static void test_power_on_registers() {
    setup_empty_disk();
    // Status: DRDY + DSC (no BSY, no DRQ)
    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert((st & 0x80u) == 0 && "BSY should be clear at reset");
    assert((st & 0x40u) != 0 && "DRDY should be set at reset");
    assert((st & 0x08u) == 0 && "DRQ should be clear at reset");
    assert((st & 0x01u) == 0 && "ERR should be clear at reset");

    // Error register: 0x01 (ATA diagnostic signature)
    uint8_t err = read_reg(g_cart, 0x7E01u);
    assert(err == 0x01u && "error register should be 0x01 at reset");

    printf("PASS: test_power_on_registers\n");
}

// ---------------------------------------------------------------------------
// Test: EXECUTE DEVICE DIAGNOSTIC (0x90)
// ---------------------------------------------------------------------------

static void test_diagnostic_command() {
    setup_empty_disk();
    write_reg(g_cart, 0x7E07u, 0x90u);  // DIAGNOSTIC

    uint8_t err = read_reg(g_cart, 0x7E01u);
    uint8_t st  = read_reg(g_cart, 0x7E07u);

    assert(err == 0x01u && "DIAGNOSTIC: error should be 0x01 (device 0 passed)");
    assert(!(st & 0x80u) && "DIAGNOSTIC: BSY should be clear");
    assert( (st & 0x40u) && "DIAGNOSTIC: DRDY should be set");
    assert(!(st & 0x08u) && "DIAGNOSTIC: DRQ should be clear");
    assert(!(st & 0x01u) && "DIAGNOSTIC: ERR should be clear");

    printf("PASS: test_diagnostic_command\n");
}

// ---------------------------------------------------------------------------
// Test: IDENTIFY DEVICE (0xEC)
// ---------------------------------------------------------------------------

static void test_identify_command() {
    setup_empty_disk();
    // Set up with a known disk size so we can verify word 60.
    g_ide.disk_sectors = 65536u;  // 32 MB

    write_reg(g_cart, 0x7E07u, 0xECu);  // IDENTIFY

    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x01u) && "IDENTIFY: ERR should be clear");
    assert( (st & 0x08u) && "IDENTIFY: DRQ should be set");
    assert( (st & 0x40u) && "IDENTIFY: DRDY should be set");

    // Drain the 512-byte identity block.
    uint8_t buf[512];
    drain_sector(g_cart, buf);

    // DRQ should be clear after full sector drain.
    st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x08u) && "IDENTIFY: DRQ should clear after 512 bytes drained");

    // Word 0 [offset 0-1]: 0x0040 = fixed disk
    assert(buf[0] == 0x40u && buf[1] == 0x00u && "IDENTIFY: word 0 = 0x0040");

    // Word 49 [offset 98-99]: capabilities, bit 9 = LBA supported → 0x0200
    assert(buf[98] == 0x00u && buf[99] == 0x02u && "IDENTIFY: LBA capability bit set");

    // Words 60-61 [offset 120-123]: total LBA sectors = 65536 (little-endian)
    uint32_t total_lba = (uint32_t)buf[120]
                       | ((uint32_t)buf[121] << 8)
                       | ((uint32_t)buf[122] << 16)
                       | ((uint32_t)buf[123] << 24);
    assert(total_lba == 65536u && "IDENTIFY: total LBA sectors mismatch");

    printf("PASS: test_identify_command\n");
}

// ---------------------------------------------------------------------------
// Test: READ SECTOR(S) — single sector
// ---------------------------------------------------------------------------

// Tiny disk image: 4 sectors × 512 bytes.
// Filled at runtime in init_test_disk() — one distinct byte per sector.
static uint8_t kTestDisk[4 * 512];
static bool    kTestDiskReady = false;

static void init_test_disk() {
    if (kTestDiskReady) return;
    memset(kTestDisk + 0 * 512, 0xA5u, 512);
    memset(kTestDisk + 1 * 512, 0x5Au, 512);
    memset(kTestDisk + 2 * 512, 0xBEu, 512);
    memset(kTestDisk + 3 * 512, 0xEFu, 512);
    kTestDiskReady = true;
}

static void test_read_single_sector() {
    init_test_disk();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, kTestDisk, 4u);

    // Set LBA = 1 (sector filled with 0x5A), count = 1.
    write_reg(g_cart, 0x7E06u, 0x40u);  // dev/head: LBA mode, device 0
    write_reg(g_cart, 0x7E05u, 0x00u);  // LBA[23:16]
    write_reg(g_cart, 0x7E04u, 0x00u);  // LBA[15:8]
    write_reg(g_cart, 0x7E03u, 0x01u);  // LBA[7:0] = 1
    write_reg(g_cart, 0x7E02u, 0x01u);  // sector count = 1
    write_reg(g_cart, 0x7E07u, 0x20u);  // READ SECTORS

    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x01u) && "READ: ERR should be clear");
    assert( (st & 0x08u) && "READ: DRQ should be set");

    uint8_t buf[512];
    drain_sector(g_cart, buf);

    // All bytes should be 0x5A
    for (int i = 0; i < 512; ++i) {
        assert(buf[i] == 0x5Au && "READ: sector data mismatch");
    }

    // DRQ should be clear now.
    st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x08u) && "READ: DRQ should clear after sector drained");
    assert(!(st & 0x01u) && "READ: ERR should remain clear after read");

    printf("PASS: test_read_single_sector\n");
}

// ---------------------------------------------------------------------------
// Test: READ SECTOR(S) — multi-sector (3 sectors)
// ---------------------------------------------------------------------------

static void test_read_multi_sector() {
    init_test_disk();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, kTestDisk, 4u);

    // Read sectors 1, 2, 3 (3 sectors starting at LBA 1).
    write_reg(g_cart, 0x7E06u, 0x40u);  // LBA mode
    write_reg(g_cart, 0x7E05u, 0x00u);
    write_reg(g_cart, 0x7E04u, 0x00u);
    write_reg(g_cart, 0x7E03u, 0x01u);  // LBA = 1
    write_reg(g_cart, 0x7E02u, 0x03u);  // sector count = 3
    write_reg(g_cart, 0x7E07u, 0x20u);  // READ SECTORS

    uint8_t expected[3] = {0x5Au, 0xBEu, 0xEFu};
    for (int s = 0; s < 3; ++s) {
        uint8_t st = read_reg(g_cart, 0x7E07u);
        assert((st & 0x08u) && "READ multi: DRQ should be set for each sector");
        uint8_t buf[512];
        drain_sector(g_cart, buf);
        for (int i = 0; i < 512; ++i) {
            assert(buf[i] == expected[s] && "READ multi: sector data mismatch");
        }
    }

    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x08u) && "READ multi: DRQ should be clear after all sectors");
    assert(!(st & 0x01u) && "READ multi: ERR should be clear");

    printf("PASS: test_read_multi_sector\n");
}

// ---------------------------------------------------------------------------
// Test: READ beyond disk end → IDNF error
// ---------------------------------------------------------------------------

static void test_read_out_of_range() {
    init_test_disk();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, kTestDisk, 4u);

    write_reg(g_cart, 0x7E06u, 0x40u);
    write_reg(g_cart, 0x7E05u, 0x00u);
    write_reg(g_cart, 0x7E04u, 0x00u);
    write_reg(g_cart, 0x7E03u, 0x10u);  // LBA = 16 (beyond 4-sector disk)
    write_reg(g_cart, 0x7E02u, 0x01u);
    write_reg(g_cart, 0x7E07u, 0x20u);  // READ SECTORS

    uint8_t st  = read_reg(g_cart, 0x7E07u);
    uint8_t err = read_reg(g_cart, 0x7E01u);

    assert((st & 0x01u) && "READ OOB: ERR should be set");
    assert(!(st & 0x08u) && "READ OOB: DRQ should not be set");
    assert((err & 0x10u) && "READ OOB: IDNF bit should be set in error register");

    printf("PASS: test_read_out_of_range\n");
}

// ---------------------------------------------------------------------------
// Test: WRITE SECTOR(S) — accepted, DRQ handshake completes, data discarded
// ---------------------------------------------------------------------------

static void test_write_accepted_discarded() {
    init_test_disk();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, kTestDisk, 4u);

    write_reg(g_cart, 0x7E06u, 0x40u);
    write_reg(g_cart, 0x7E05u, 0x00u);
    write_reg(g_cart, 0x7E04u, 0x00u);
    write_reg(g_cart, 0x7E03u, 0x00u);  // LBA = 0
    write_reg(g_cart, 0x7E02u, 0x01u);  // sector count = 1
    write_reg(g_cart, 0x7E07u, 0x30u);  // WRITE SECTORS

    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert((st & 0x08u) && "WRITE: DRQ should be set waiting for data");
    assert(!(st & 0x01u) && "WRITE: ERR should be clear");

    // Push 512 bytes of data (will be discarded, but protocol must complete).
    uint8_t junk[512];
    memset(junk, 0xCDu, 512);
    push_sector(g_cart, junk);

    // After full sector, DRQ should clear.
    st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x08u) && "WRITE: DRQ should clear after sector written");
    assert(!(st & 0x01u) && "WRITE: ERR should remain clear");

    // Read back sector 0 to confirm disk image was NOT modified.
    write_reg(g_cart, 0x7E06u, 0x40u);
    write_reg(g_cart, 0x7E05u, 0x00u);
    write_reg(g_cart, 0x7E04u, 0x00u);
    write_reg(g_cart, 0x7E03u, 0x00u);
    write_reg(g_cart, 0x7E02u, 0x01u);
    write_reg(g_cart, 0x7E07u, 0x20u);  // READ SECTORS

    uint8_t buf[512];
    drain_sector(g_cart, buf);
    for (int i = 0; i < 512; ++i) {
        assert(buf[i] == 0xA5u && "WRITE discard: sector 0 should be unmodified");
    }

    printf("PASS: test_write_accepted_discarded\n");
}

// ---------------------------------------------------------------------------
// Test: unknown command → ABRT error
// ---------------------------------------------------------------------------

static void test_unknown_command_aborted() {
    setup_empty_disk();
    write_reg(g_cart, 0x7E07u, 0x00u);  // unknown command

    uint8_t st  = read_reg(g_cart, 0x7E07u);
    uint8_t err = read_reg(g_cart, 0x7E01u);

    assert((st  & 0x01u) && "unknown cmd: ERR should be set");
    assert((err & 0x04u) && "unknown cmd: ABRT bit should be set");
    assert(!(st & 0x08u)  && "unknown cmd: DRQ should not be set");

    printf("PASS: test_unknown_command_aborted\n");
}

// ---------------------------------------------------------------------------
// Test: alternate status register mirrors status; reading doesn't clear DRQ
// ---------------------------------------------------------------------------

static void test_alt_status_mirrors_status() {
    setup_empty_disk();
    write_reg(g_cart, 0x7E07u, 0x90u);  // DIAGNOSTIC

    uint8_t st_main = read_reg(g_cart, 0x7E07u);
    uint8_t st_alt  = read_reg(g_cart, 0x7E0Eu);

    assert(st_main == st_alt && "alt status should mirror main status");

    printf("PASS: test_alt_status_mirrors_status\n");
}

// ---------------------------------------------------------------------------
// Test: ROM banking via 0x4104 updates memory_read_addresses
// ---------------------------------------------------------------------------

// Four minimal 16 KB "ROM banks", each filled with a distinct byte.
static uint8_t kTestRom[4 * 16384u];

static void init_test_rom() {
    for (int bank = 0; bank < 4; ++bank) {
        memset(kTestRom + bank * 16384u, (uint8_t)(0x10u + bank), 16384u);
    }
}

static void test_rom_banking() {
    init_test_rom();

    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, kTestRom, sizeof(kTestRom), nullptr, 0u);

    // Bank 0 should be visible at reset.
    assert(g_cart.memory_read_addresses[2] == kTestRom &&
           "bank 0: seg2 should point to start of ROM");
    assert(g_cart.memory_read_addresses[3] == kTestRom + 8192u &&
           "bank 0: seg3 should point to ROM + 8192");

    // Switch to bank 2.
    write_reg(g_cart, 0x4104u, 2u);
    assert(g_cart.memory_read_addresses[2] == kTestRom + 2u * 16384u &&
           "bank 2: seg2 pointer mismatch");
    assert(g_cart.memory_read_addresses[3] == kTestRom + 2u * 16384u + 8192u &&
           "bank 2: seg3 pointer mismatch");

    // Switch to bank 1.
    write_reg(g_cart, 0x4104u, 1u);
    assert(g_cart.memory_read_addresses[2] == kTestRom + 1u * 16384u &&
           "bank 1: seg2 pointer mismatch");

    printf("PASS: test_rom_banking\n");
}

// ---------------------------------------------------------------------------
// Test: normal ROM address falls through (callback returns driven=false)
// ---------------------------------------------------------------------------

static void test_rom_fallthrough() {
    init_test_rom();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, kTestRom, sizeof(kTestRom), nullptr, 0u);

    // 0x4000 is a normal ROM address — should fall through.
    read_rom_fallthrough(g_cart, 0x4000u);

    // 0x4103 and 0x4105 straddle the bank register — should fall through.
    read_rom_fallthrough(g_cart, 0x4103u);
    read_rom_fallthrough(g_cart, 0x4105u);

    // 0x7BFF is just before the data window — should fall through.
    read_rom_fallthrough(g_cart, 0x7BFFu);

    printf("PASS: test_rom_fallthrough\n");
}

// ---------------------------------------------------------------------------
// Test: device control SRST triggers soft reset
// ---------------------------------------------------------------------------

static void test_srst_soft_reset() {
    init_test_disk();
    g_cart = Cartridge{};
    ide_reset(g_ide);
    ide_setup(g_cart, g_ide, nullptr, 0u, kTestDisk, 4u);

    // Perform a READ to put the interface in DRQ state.
    write_reg(g_cart, 0x7E06u, 0x40u);
    write_reg(g_cart, 0x7E05u, 0x00u);
    write_reg(g_cart, 0x7E04u, 0x00u);
    write_reg(g_cart, 0x7E03u, 0x00u);
    write_reg(g_cart, 0x7E02u, 0x01u);
    write_reg(g_cart, 0x7E07u, 0x20u);  // READ SECTORS

    uint8_t st = read_reg(g_cart, 0x7E07u);
    assert((st & 0x08u) && "SRST pre: DRQ should be set");

    // Assert SRST (bit 2), then deassert → soft reset.
    write_reg(g_cart, 0x7E0Eu, 0x04u);  // SRST high
    write_reg(g_cart, 0x7E0Eu, 0x00u);  // SRST low → triggers reset

    // Interface should be back to power-on state: DRDY set, DRQ clear.
    st = read_reg(g_cart, 0x7E07u);
    assert(!(st & 0x08u) && "SRST: DRQ should be clear after reset");
    assert( (st & 0x40u) && "SRST: DRDY should be set after reset");
    assert(!(st & 0x01u) && "SRST: ERR should be clear after reset");

    printf("PASS: test_srst_soft_reset\n");
}

// ---------------------------------------------------------------------------
// Test: data window returns 0xFF when DRQ is not set
// ---------------------------------------------------------------------------

static void test_data_window_no_drq() {
    setup_empty_disk();
    // No command issued — DRQ is not set.
    uint8_t val = read_reg(g_cart, 0x7C00u);
    assert(val == 0xFFu && "data window with no DRQ should return 0xFF");

    printf("PASS: test_data_window_no_drq\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_power_on_registers();
    test_diagnostic_command();
    test_identify_command();
    test_read_single_sector();
    test_read_multi_sector();
    test_read_out_of_range();
    test_write_accepted_discarded();
    test_unknown_command_aborted();
    test_alt_status_mirrors_status();
    test_rom_banking();
    test_rom_fallthrough();
    test_srst_soft_reset();
    test_data_window_no_drq();

    printf("All sunrise_ide tests passed.\n");
    return 0;
}
