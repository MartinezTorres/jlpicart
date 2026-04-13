// sunrise_ide.cc — Sunrise ATA-IDE compatible interface emulation.

#include "peripherals/sunrise_ide.h"
#include "boards/gpio_defs.h"
#include <cstring>

// ---------------------------------------------------------------------------
// ATA constants
// ---------------------------------------------------------------------------

// Status register bits (tf_status)
static constexpr uint8_t ATA_ST_ERR  = 0x01;  // error occurred
static constexpr uint8_t ATA_ST_DRQ  = 0x08;  // data request (buffer ready)
static constexpr uint8_t ATA_ST_DSC  = 0x10;  // drive seek complete
static constexpr uint8_t ATA_ST_DRDY = 0x40;  // drive ready
static constexpr uint8_t ATA_ST_BSY  = 0x80;  // busy

// Error register bits (tf_error)
static constexpr uint8_t ATA_ERR_ABRT = 0x04;  // command aborted
static constexpr uint8_t ATA_ERR_IDNF = 0x10;  // ID (sector) not found

// Commands
static constexpr uint8_t ATA_CMD_DIAGNOSTIC = 0x90;
static constexpr uint8_t ATA_CMD_IDENTIFY   = 0xEC;
static constexpr uint8_t ATA_CMD_READ_SEC   = 0x20;
static constexpr uint8_t ATA_CMD_WRITE_SEC  = 0x30;

// Sunrise IDE register addresses in MSX memory space
static constexpr uint16_t IDE_BANK_SEL    = 0x4104;  // write: ROM bank select
static constexpr uint16_t IDE_DATA_START  = 0x7C00;  // sector buffer window start
static constexpr uint16_t IDE_DATA_END    = 0x7DFF;  // sector buffer window end
static constexpr uint16_t IDE_REG_BASE    = 0x7E00;  // ATA CS0 register base (R0-R7)
static constexpr uint16_t IDE_REG_ALT     = 0x7E0E;  // ATA CS1-R6: alt status / dev ctrl

// Segment indices for 0x4000-0x7FFF
static constexpr int SEG2 = 2;  // 0x4000-0x5FFF
static constexpr int SEG3 = 3;  // 0x6000-0x7FFF

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static inline IdeState& get_state(Cartridge& c) {
    return *reinterpret_cast<IdeState*>(c.ram_base);
}

// Build the 512-byte IDENTIFY DEVICE response in sector_buf.
static void ide_build_identify(IdeState& s) {
    uint8_t* b = s.sector_buf;
    memset(b, 0, 512);

    // Word 0: general config — fixed disk, not removable
    b[0] = 0x40; b[1] = 0x00;

    // Approximate CHS geometry from sector count (for legacy tools).
    // Clamp to ATA CHS limits (max 16383/16/63 = ~8 GB).
    uint32_t ns = s.disk_sectors;
    uint32_t chs_sec  = 63u;
    uint32_t chs_head = 16u;
    uint32_t chs_cyl  = ns / (chs_head * chs_sec);
    if (chs_cyl > 16383u) chs_cyl = 16383u;
    if (chs_cyl < 1u)     chs_cyl = 1u;
    b[2] = (uint8_t)(chs_cyl & 0xFF); b[3] = (uint8_t)(chs_cyl >> 8);   // word 1
    b[6] = (uint8_t)chs_head;         b[7] = 0x00;                       // word 3
    b[12] = (uint8_t)chs_sec;         b[13] = 0x00;                      // word 6

    // ATA string helper: fill n chars at byte offset `off`, byte-swap each word pair.
    // ATA strings have bytes swapped within each 16-bit word.
    auto write_str = [&](int off, const char* str, int n) {
        for (int i = 0; i < n; ++i) {
            char c = (str[i] != '\0') ? str[i] : ' ';
            b[off + (i ^ 1)] = (uint8_t)c;
        }
    };

    // Words 10-19 (offset 20): serial number (20 chars)
    write_str(20, "JLPICART00000001    ", 20);

    // Words 23-26 (offset 46): firmware revision (8 chars)
    write_str(46, "1.0     ", 8);

    // Words 27-46 (offset 54): model number (40 chars)
    write_str(54, "JLPiCart Virtual Disk                   ", 40);

    // Word 47 (offset 94): max sectors per interrupt (bits[7:0]=1, bits[15:8]=0x80)
    b[94] = 0x01; b[95] = 0x80;

    // Word 49 (offset 98): capabilities — bit 9 = LBA supported
    b[98] = 0x00; b[99] = 0x02;

    // Word 53 (offset 106): validity flags — words 54-58 valid
    b[106] = 0x01; b[107] = 0x00;

    // Words 60-61 (offset 120): total user LBA sectors (28-bit)
    b[120] = (uint8_t)(ns & 0xFF);
    b[121] = (uint8_t)((ns >>  8) & 0xFF);
    b[122] = (uint8_t)((ns >> 16) & 0xFF);
    b[123] = (uint8_t)((ns >> 24) & 0xFF);
}

// Load a sector from the disk image into sector_buf.
// If the LBA is out of range or disk_image is null, fills with zeros.
static inline void ide_load_sector(IdeState& s, uint32_t lba) {
    if (s.disk_image && lba < s.disk_sectors) {
        memcpy(s.sector_buf, s.disk_image + lba * 512u, 512);
    } else {
        memset(s.sector_buf, 0, 512);
    }
}

// Called when a sector read completes (buf_pos wrapped to 512).
// Loads the next sector if any remain, or clears DRQ.
static inline void ide_advance_read(IdeState& s) {
    if (s.sectors_todo == 0) {
        s.buf_pos  = 0;
        s.tf_status = ATA_ST_DRDY | ATA_ST_DSC;  // DRQ cleared
        return;
    }
    ide_load_sector(s, s.lba_next);
    s.lba_next++;
    s.sectors_todo--;
    s.buf_pos   = 0;
    s.tf_status = ATA_ST_DRDY | ATA_ST_DSC | ATA_ST_DRQ;
}

// Called when a write sector buffer is full (buf_pos reached 512).
// Data is discarded (read-only disk).
static inline void ide_commit_write(IdeState& s) {
    if (s.sectors_todo == 0) {
        s.buf_pos   = 0;
        s.write_mode = false;
        s.tf_status  = ATA_ST_DRDY | ATA_ST_DSC;  // DRQ cleared
    } else {
        s.sectors_todo--;
        s.buf_pos   = 0;
        // Keep DRQ set for the next sector
        s.tf_status = ATA_ST_DRDY | ATA_ST_DRQ;
    }
}

// Update the Nextor ROM bank and retarget segment pointers.
// Called from the write callback so c is available.
static inline void ide_set_bank(Cartridge& c, IdeState& s, uint8_t bank) {
    s.rom_bank = bank;
    if (!s.nextor_rom || s.nextor_size == 0) return;
    uint32_t bank_offset = (uint32_t)bank * 16384u;
    if (bank_offset + 16384u > s.nextor_size) bank_offset = 0u;
    c.memory_read_addresses[SEG2] = s.nextor_rom + bank_offset;
    c.memory_read_addresses[SEG3] = s.nextor_rom + bank_offset + 8192u;
}

// Execute an ATA command written to the command register (0x7E07).
static void ide_execute_cmd(IdeState& s, uint8_t cmd) {
    s.write_mode = false;

    switch (cmd) {
        // ---- EXECUTE DEVICE DIAGNOSTIC ----
        // Signature: error=0x01 (device 0 passed, no device 1), status=DRDY.
        case ATA_CMD_DIAGNOSTIC:
            s.tf_error   = 0x01;
            s.tf_status  = ATA_ST_DRDY | ATA_ST_DSC;
            break;

        // ---- IDENTIFY DEVICE ----
        // Fill sector_buf with 512-byte identity block, set DRQ.
        case ATA_CMD_IDENTIFY:
            ide_build_identify(s);
            s.buf_pos      = 0;
            s.sectors_todo = 0;
            s.tf_error     = 0;
            s.tf_status    = ATA_ST_DRDY | ATA_ST_DSC | ATA_ST_DRQ;
            break;

        // ---- READ SECTOR(S) ----
        // LBA28 from tf_dev_head[3:0] | tf_lba2 | tf_lba1 | tf_lba0.
        // tf_sector_count == 0 means 256 sectors (ATA spec).
        case ATA_CMD_READ_SEC: {
            uint32_t lba = (uint32_t)s.tf_lba0
                         | ((uint32_t)s.tf_lba1 << 8)
                         | ((uint32_t)s.tf_lba2 << 16)
                         | ((uint32_t)(s.tf_dev_head & 0x0Fu) << 24);
            uint16_t nsec = s.tf_sector_count ? s.tf_sector_count : 256u;

            if (s.disk_image == nullptr || lba >= s.disk_sectors) {
                s.tf_error  = ATA_ERR_IDNF;
                s.tf_status = ATA_ST_DRDY | ATA_ST_DSC | ATA_ST_ERR;
                break;
            }
            ide_load_sector(s, lba);
            s.lba_next     = lba + 1u;
            s.buf_pos      = 0;
            s.sectors_todo = (uint16_t)(nsec - 1u);
            s.tf_error     = 0;
            s.tf_status    = ATA_ST_DRDY | ATA_ST_DSC | ATA_ST_DRQ;
            break;
        }

        // ---- WRITE SECTOR(S) ----
        // Accept data for protocol compliance; discard (read-only disk).
        case ATA_CMD_WRITE_SEC: {
            uint16_t nsec = s.tf_sector_count ? s.tf_sector_count : 256u;
            s.write_mode   = true;
            s.buf_pos      = 0;
            s.sectors_todo = (uint16_t)(nsec - 1u);
            s.tf_error     = 0;
            s.tf_status    = ATA_ST_DRDY | ATA_ST_DRQ;
            break;
        }

        // ---- Unknown / unsupported ----
        default:
            s.tf_error  = ATA_ERR_ABRT;
            s.tf_status = ATA_ST_DRDY | ATA_ST_DSC | ATA_ST_ERR;
            break;
    }
}

// ---------------------------------------------------------------------------
// Bus callbacks
// ---------------------------------------------------------------------------

// Read callback — installed on segments SEG2 and SEG3.
// Returns {false, 0} to fall through to memory_read_addresses[] for normal ROM
// reads; returns {true, byte} for ATA register and data port reads.
static std::pair<bool, uint8_t> RAMFUNC(ide_read_cb)(Cartridge& c, uint32_t bus) {
    IdeState& s   = get_state(c);
    uint16_t addr = (uint16_t)(bus & 0xFFFFu);

    // ---- Sector buffer window: 0x7C00–0x7DFF ----
    if (addr >= IDE_DATA_START && addr <= IDE_DATA_END) {
        if (!(s.tf_status & ATA_ST_DRQ) || s.write_mode) {
            return {true, 0xFF};
        }
        uint8_t byte = s.sector_buf[s.buf_pos];
        s.buf_pos++;
        if (s.buf_pos >= 512u) {
            ide_advance_read(s);
        }
        return {true, byte};
    }

    // ---- ATA CS0 task file registers: 0x7E00–0x7E07 ----
    if (addr >= IDE_REG_BASE && addr <= IDE_REG_BASE + 7u) {
        uint8_t reg = (uint8_t)(addr - IDE_REG_BASE);
        switch (reg) {
            case 0: {  // data register (R0) — single-byte access via register path
                if (!(s.tf_status & ATA_ST_DRQ) || s.write_mode) return {true, 0xFF};
                uint8_t byte = s.sector_buf[s.buf_pos];
                s.buf_pos++;
                if (s.buf_pos >= 512u) ide_advance_read(s);
                return {true, byte};
            }
            case 1: return {true, s.tf_error};
            case 2: return {true, s.tf_sector_count};
            case 3: return {true, s.tf_lba0};
            case 4: return {true, s.tf_lba1};
            case 5: return {true, s.tf_lba2};
            case 6: return {true, s.tf_dev_head};
            case 7: return {true, s.tf_status};
        }
    }

    // ---- ATA CS1-R6: alternate status (0x7E0E, read) ----
    if (addr == IDE_REG_ALT) {
        return {true, s.tf_status};  // alt status mirrors status, no side effects
    }

    // Fall through to memory_read_addresses[] — returns ROM data.
    return {false, 0};
}

// Write callback — installed on segments SEG2 and SEG3.
static std::pair<bool, uint8_t> RAMFUNC(ide_write_cb)(Cartridge& c, uint32_t bus) {
    IdeState& s   = get_state(c);
    uint16_t addr = (uint16_t)(bus & 0xFFFFu);
    uint8_t  data = (uint8_t)((bus >> GPIO_D0) & 0xFFu);

    // ---- ROM bank select: write to 0x4104 ----
    if (addr == IDE_BANK_SEL) {
        ide_set_bank(c, s, data);
        return {false, 0};
    }

    // ---- Sector buffer window: 0x7C00–0x7DFF ----
    if (addr >= IDE_DATA_START && addr <= IDE_DATA_END) {
        if (s.write_mode) {
            s.sector_buf[s.buf_pos] = data;
            s.buf_pos++;
            if (s.buf_pos >= 512u) {
                ide_commit_write(s);
            }
        }
        return {false, 0};
    }

    // ---- ATA CS0 task file registers: 0x7E00–0x7E07 ----
    if (addr >= IDE_REG_BASE && addr <= IDE_REG_BASE + 7u) {
        uint8_t reg = (uint8_t)(addr - IDE_REG_BASE);
        switch (reg) {
            case 0:  // data register (W) — single-byte write via register path
                if (s.write_mode) {
                    s.sector_buf[s.buf_pos] = data;
                    s.buf_pos++;
                    if (s.buf_pos >= 512u) ide_commit_write(s);
                }
                break;
            case 1: s.tf_features    = data; break;
            case 2: s.tf_sector_count = data; break;
            case 3: s.tf_lba0        = data; break;
            case 4: s.tf_lba1        = data; break;
            case 5: s.tf_lba2        = data; break;
            case 6: s.tf_dev_head    = data; break;
            case 7: ide_execute_cmd(s, data); break;  // command register — execute
        }
        return {false, 0};
    }

    // ---- ATA CS1-R6: device control (0x7E0E, write) ----
    if (addr == IDE_REG_ALT) {
        uint8_t prev = s.tf_dev_ctrl;
        s.tf_dev_ctrl = data;
        // SRST (software reset): bit 2 high → low edge triggers reset
        if ((prev & 0x04u) && !(data & 0x04u)) {
            ide_reset(s);
        }
        return {false, 0};
    }

    return {false, 0};
}

// Reset callback — restore ROM bank 0 and ATA power-on state.
static void ide_reset_fn(Cartridge& c) {
    IdeState& s = get_state(c);
    uint8_t saved_bank = s.rom_bank;
    ide_reset(s);
    if (saved_bank != 0u) {
        // Bank was non-zero before reset; restore pointers to bank 0.
        ide_set_bank(c, s, 0u);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ide_reset(IdeState& state) {
    // ATA power-on / software reset defaults.
    state.tf_error        = 0x01;  // diagnostic signature: device 0 passed
    state.tf_features     = 0x00;
    state.tf_sector_count = 0x01;
    state.tf_lba0         = 0x01;
    state.tf_lba1         = 0x00;
    state.tf_lba2         = 0x00;
    state.tf_dev_head     = 0x00;
    state.tf_status       = ATA_ST_DRDY | ATA_ST_DSC;
    state.tf_dev_ctrl     = 0x00;

    state.buf_pos      = 0;
    state.sectors_todo = 0;
    state.lba_next     = 0;
    state.write_mode   = false;
    state.rom_bank     = 0;
}

void ide_setup(Cartridge& c, IdeState& state,
               const uint8_t* nextor_rom, uint32_t nextor_size,
               const uint8_t* disk_image, uint32_t disk_sectors)
{
    c.clear();
    c.name     = "sunrise_ide";
    c.ram_base = reinterpret_cast<uint8_t*>(&state);

    state.nextor_rom   = nextor_rom;
    state.nextor_size  = nextor_size;
    state.disk_image   = disk_image;
    state.disk_sectors = disk_sectors;

    // Point segment pointers at Nextor bank 0 (16 KB = 2 × 8 KB segments).
    if (nextor_rom && nextor_size >= 16384u) {
        c.memory_read_addresses[SEG2] = nextor_rom;
        c.memory_read_addresses[SEG3] = nextor_rom + 8192u;
    }

    // Both read callbacks installed: return {false,0} to fall through to the
    // ROM pointer for normal ROM addresses; intercept ATA registers / data port.
    c.memory_read_callbacks[SEG2]  = ide_read_cb;
    c.memory_read_callbacks[SEG3]  = ide_read_cb;
    c.memory_write_callbacks[SEG2] = ide_write_cb;
    c.memory_write_callbacks[SEG3] = ide_write_cb;

    c.reset_fn = ide_reset_fn;
}
