#pragma once
// flash_layout.h — Flash partition map for JLPiCart.
//
// All offsets are from the start of external flash (RP2350 XIP base 0x10000000).
// The Pico SDK flash_range_program/erase functions take offsets from the same base.
//
// Layout:
//   ┌──────────────────────────────┬──────────────┬────────────────────┐
//   │ Region                       │   Start      │   Size             │
//   ├──────────────────────────────┼──────────────┼────────────────────┤
//   │ Firmware                     │ 0x000000     │  2 MB  (0x200000)  │
//   │ FAT volume                   │ 0x200000     │ 14 MB  (0xE00000)  │
//   └──────────────────────────────┴──────────────┴────────────────────┘
//   Total: 16 MB (0x1000000)
//
// The FAT volume is a FAT16 filesystem (14 MB → ~28 K clusters at 512 B/sector).
// It is mounted as logical drive "1:" by FatVolume; USB host uses drive "0:".
// On first boot (or after a firmware flash that clears the region) FatVolume
// formats it automatically.
//
// Note: the policy document lives at 0x1FF000 within the Firmware region
// (last 4 KB before the FAT volume).  See policy_store.cc.

#include <cstdint>

static constexpr uint32_t FLASH_SIZE_BYTES   = 16u * 1024u * 1024u;  // 16 MB
static constexpr uint32_t FLASH_SECTOR_SIZE  = 4096u;   // RP2350 erase granularity
static constexpr uint32_t FLASH_PAGE_SIZE    = 256u;    // programming granularity

static constexpr uint32_t FLASH_FIRMWARE_OFS  = 0x000000u;
static constexpr uint32_t FLASH_FIRMWARE_SIZE = 0x200000u;  // 2 MB

static constexpr uint32_t FLASH_FAT_OFS  = 0x200000u;
static constexpr uint32_t FLASH_FAT_SIZE = 0xE00000u;  // 14 MB

// Compile-time integrity checks.
static_assert(FLASH_FIRMWARE_OFS + FLASH_FIRMWARE_SIZE == FLASH_FAT_OFS,
              "FAT volume must immediately follow firmware");
static_assert(FLASH_FAT_OFS + FLASH_FAT_SIZE == FLASH_SIZE_BYTES,
              "Partitions must exactly cover FLASH_SIZE_BYTES");
static_assert((FLASH_FAT_OFS  % FLASH_SECTOR_SIZE) == 0, "FAT must be sector-aligned");
static_assert((FLASH_FAT_SIZE % FLASH_SECTOR_SIZE) == 0, "FAT size must be sector-aligned");
