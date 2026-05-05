#pragma once
// sunrise_ide.h — Sunrise ATA-IDE compatible interface emulation.
//
// Emulates a Sunrise ATA-IDE v2.0 cartridge, enabling Nextor (MSX-DOS2
// derivative) and SymbOS to access a virtual ATA hard disk backed by a
// flat sector image in XIP flash.
//
// ─────────────────────────────────────────────────────────────────────────────
// Sunrise ATA-IDE memory map (MSX page 1, slot 0x4000–0x7FFF):
//
//   0x4000–0x7FFF  Nextor ROM — 16 KB banked window
//   0x4104  (W)    ROM bank select: data byte = bank index (16 KB banks)
//   0x7C00–0x7DFF  ATA 16-bit data port — byte-wide, auto-advances through
//                  the 512-byte sector buffer on each access
//   0x7E00         ATA CS0-R0: data register (alias for 0x7C00 single-byte access)
//   0x7E01         ATA CS0-R1: error (R) / features (W)
//   0x7E02         ATA CS0-R2: sector count
//   0x7E03         ATA CS0-R3: LBA[7:0]
//   0x7E04         ATA CS0-R4: LBA[15:8]
//   0x7E05         ATA CS0-R5: LBA[23:16]
//   0x7E06         ATA CS0-R6: device/head  [7]=1 [6]=LBA [5]=1 [4]=DEV [3:0]=LBA[27:24]
//   0x7E07         ATA CS0-R7: status (R) / command (W)
//   0x7E0E         ATA CS1-R6: alternate status (R) / device control (W)
//
// ATA status register (0x7E07 read):
//   bit 7 = BSY   bit 6 = DRDY   bit 4 = DSC   bit 3 = DRQ   bit 0 = ERR
//
// ─────────────────────────────────────────────────────────────────────────────
// Supported ATA commands:
//   0x90  EXECUTE DEVICE DIAGNOSTIC — returns signature (error=0x01, no device 1)
//   0xEC  IDENTIFY DEVICE           — returns 512-byte identity block
//   0x20  READ SECTOR(S)            — LBA28, single or multi-sector
//   0x30  WRITE SECTOR(S)           — accepted and discarded (read-only disk)
//   other                           — aborted (error bit set)
//
// ─────────────────────────────────────────────────────────────────────────────
// Disk image:
//   Flat sequence of 512-byte LBA sectors pointed to by IdeState::disk_image.
//   disk_image may be an XIP flash pointer (0x10000000 + offset) or nullptr
//   for an empty (unformatted) disk.  Writes are accepted but discarded.
//
// ─────────────────────────────────────────────────────────────────────────────
// Nextor ROM banking:
//   Nextor is a banked 512 KB ROM (32 × 16 KB banks).
//   Bank 0 is mapped at reset.  Writing to 0x4104 switches the visible bank.
//   The ATA registers at 0x7C00–0x7E0F always override ROM reads regardless
//   of the current bank (they are in hardware, not in the ROM window).

#include "bus/cartridge.h"
#include <cstdint>

// If NEXTOR_ROM_DATA is not defined, sunrise_ide falls back to nullptr
// (slot returns 0xFF — Z80 will not boot Nextor until a real ROM is provided).
#ifndef NEXTOR_ROM_DATA
#  define NEXTOR_ROM_DATA  nullptr
#  define NEXTOR_ROM_SIZE  0u
#endif

// ---------------------------------------------------------------------------
// IDE state — one ATA device (device 0 / master) on one IDE channel
// ---------------------------------------------------------------------------

struct IdeState {
    // ---- Disk image (read-only; set once by ide_setup) ----
    const uint8_t* disk_image;    // flat sector image (XIP flash pointer or null)
    uint32_t       disk_sectors;  // total sector count (= image_bytes / 512)

    // ---- Nextor ROM banking ----
    const uint8_t* nextor_rom;   // Nextor ROM base (XIP flash; null = no ROM)
    uint32_t       nextor_size;  // Nextor ROM size in bytes
    uint8_t        rom_bank;     // current 16 KB bank index

    // ---- ATA task file registers ----
    uint8_t  tf_error;         // error register       (read  from 0x7E01)
    uint8_t  tf_features;      // features register    (write to  0x7E01)
    uint8_t  tf_sector_count;  // sector count         (0x7E02)
    uint8_t  tf_lba0;          // LBA[7:0]             (0x7E03)
    uint8_t  tf_lba1;          // LBA[15:8]            (0x7E04)
    uint8_t  tf_lba2;          // LBA[23:16]           (0x7E05)
    uint8_t  tf_dev_head;      // device/head          (0x7E06)
    uint8_t  tf_status;        // status register      (read  from 0x7E07)
    uint8_t  tf_dev_ctrl;      // device control       (write to  0x7E0E)

    // ---- Data transfer state ----
    uint8_t  sector_buf[512];  // sector data buffer (IDENTIFY data or disk sector)
    uint16_t buf_pos;          // current byte offset in sector_buf (0–511)
    uint16_t sectors_todo;     // sectors remaining after the current one completes
    uint32_t lba_next;         // LBA to load when the current sector is exhausted
    bool     write_mode;       // true = write data collection; false = read data ready
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reset all ATA registers and transfer state to power-on defaults.
// Does NOT clear disk_image / nextor_rom / rom_bank (set once by ide_setup).
void ide_reset(IdeState& state);

// Install Sunrise IDE into a Cartridge slot.
// Wires memory read/write callbacks on the two 8 KB segments covering
// 0x4000–0x7FFF (segments 2 and 3) and sets memory_read_addresses to serve
// the Nextor ROM through the callback fall-through path.
//
// nextor_rom / nextor_size: Nextor ROM image (XIP flash pointer) and its size.
//   Pass nullptr/0 to run without a ROM (slot returns 0xFF; Z80 won't boot).
//
// disk_image / disk_sectors: flat sector image and sector count.
//   Pass nullptr/0 for an empty/unformatted disk.
//
// The Cartridge must be cleared (or freshly created) before calling this.
// The caller must also have called ide_reset() before this.
void ide_setup(Cartridge& c, IdeState& state,
               const uint8_t* nextor_rom, uint32_t nextor_size,
               const uint8_t* disk_image, uint32_t disk_sectors);
