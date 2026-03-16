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
//   │ SYSTEM_KV                    │ 0x200000     │ 512 KB (0x080000)  │
//   │ EVENT_LOG                    │ 0x280000     │ 512 KB (0x080000)  │
//   │ PROFILES_KV                  │ 0x300000     │ 512 KB (0x080000)  │
//   │ SAVES_KV       (reserved)    │ 0x380000     │ 512 KB (0x080000)  │
//   │ CONTENT_INDEX  (reserved)    │ 0x400000     │  2 MB  (0x200000)  │
//   │ CONTENT_DATA   (reserved)    │ 0x600000     │ 10 MB  (0xA00000)  │
//   └──────────────────────────────┴──────────────┴────────────────────┘
//   Total: 16 MB (0x1000000)
//
// Note: the policy document is currently stored at a fixed offset (0x1FF000)
// within the Firmware region by policy_store.cc (temporary Stage 3 layout).
// It will migrate to SYSTEM_KV in a future stage.

#include <cstdint>

static constexpr uint32_t FLASH_LAYOUT_VERSION  = 1u;

static constexpr uint32_t FLASH_SIZE_BYTES       = 16u * 1024u * 1024u;  // 16 MB
static constexpr uint32_t FLASH_SECTOR_SIZE      = 4096u;   // RP2350 erase granularity
static constexpr uint32_t FLASH_PAGE_SIZE        = 256u;    // programming granularity

static constexpr uint32_t FLASH_FIRMWARE_OFS     = 0x000000u;
static constexpr uint32_t FLASH_FIRMWARE_SIZE    = 0x200000u;  // 2 MB

static constexpr uint32_t FLASH_SYSTEM_KV_OFS    = 0x200000u;
static constexpr uint32_t FLASH_SYSTEM_KV_SIZE   = 0x080000u;  // 512 KB

static constexpr uint32_t FLASH_EVENT_LOG_OFS    = 0x280000u;
static constexpr uint32_t FLASH_EVENT_LOG_SIZE   = 0x080000u;  // 512 KB

static constexpr uint32_t FLASH_PROFILES_KV_OFS    = 0x300000u;
static constexpr uint32_t FLASH_PROFILES_KV_SIZE   = 0x080000u;  // 512 KB

static constexpr uint32_t FLASH_SAVES_KV_OFS       = 0x380000u;
static constexpr uint32_t FLASH_SAVES_KV_SIZE       = 0x080000u;  // 512 KB (reserved)

static constexpr uint32_t FLASH_CONTENT_INDEX_OFS  = 0x400000u;
static constexpr uint32_t FLASH_CONTENT_INDEX_SIZE = 0x200000u;  // 2 MB (reserved)

static constexpr uint32_t FLASH_CONTENT_DATA_OFS   = 0x600000u;
static constexpr uint32_t FLASH_CONTENT_DATA_SIZE  = 0xA00000u;  // 10 MB (reserved)

// Compile-time partition table integrity checks.
static_assert(FLASH_FIRMWARE_OFS    + FLASH_FIRMWARE_SIZE    == FLASH_SYSTEM_KV_OFS,
              "SYSTEM_KV must immediately follow firmware");
static_assert(FLASH_SYSTEM_KV_OFS   + FLASH_SYSTEM_KV_SIZE   == FLASH_EVENT_LOG_OFS,
              "EVENT_LOG must immediately follow SYSTEM_KV");
static_assert(FLASH_EVENT_LOG_OFS   + FLASH_EVENT_LOG_SIZE   == FLASH_PROFILES_KV_OFS,
              "PROFILES_KV must immediately follow EVENT_LOG");
static_assert(FLASH_PROFILES_KV_OFS + FLASH_PROFILES_KV_SIZE == FLASH_SAVES_KV_OFS,
              "SAVES_KV must immediately follow PROFILES_KV");
static_assert(FLASH_SAVES_KV_OFS    + FLASH_SAVES_KV_SIZE    == FLASH_CONTENT_INDEX_OFS,
              "CONTENT_INDEX must immediately follow SAVES_KV");
static_assert(FLASH_CONTENT_INDEX_OFS + FLASH_CONTENT_INDEX_SIZE == FLASH_CONTENT_DATA_OFS,
              "CONTENT_DATA must immediately follow CONTENT_INDEX");
static_assert(FLASH_CONTENT_DATA_OFS + FLASH_CONTENT_DATA_SIZE == FLASH_SIZE_BYTES,
              "Partitions must exactly cover FLASH_SIZE_BYTES");

// All partitions (except firmware) must be sector-aligned.
static_assert((FLASH_SYSTEM_KV_OFS    % FLASH_SECTOR_SIZE) == 0);
static_assert((FLASH_EVENT_LOG_OFS    % FLASH_SECTOR_SIZE) == 0);
static_assert((FLASH_PROFILES_KV_OFS  % FLASH_SECTOR_SIZE) == 0);
static_assert((FLASH_SAVES_KV_OFS     % FLASH_SECTOR_SIZE) == 0);
static_assert((FLASH_CONTENT_INDEX_OFS % FLASH_SECTOR_SIZE) == 0);
static_assert((FLASH_CONTENT_DATA_OFS  % FLASH_SECTOR_SIZE) == 0);
