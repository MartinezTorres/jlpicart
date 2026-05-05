#pragma once
// store_record.h — Fixed-size (160-byte) record for the append-only event log.
//
// Field layout (all naturally aligned, no compiler padding):
//   magic[4]          @   0  framing sentinel {0x45,0x4C,0x47,0x21} "ELG!"
//   uuid[16]          @   4  this record's UUID
//   parent[16]        @  20  parent UUID; zeroed for checkpoints and roots
//   key[32]           @  36  EventKey::data
//   device_uuid[16]   @  68  cartridge that wrote this record
//   timestamp(u32)    @  84  seconds since boot, best-effort; 0 = unknown
//   payload_len(u16)  @  88  bytes used in payload[] (0..STORE_INLINE_MAX)
//   event_type(u8)    @  90  StoreEntryType
//   sync_state(u8)    @  91  SyncState
//   payload[64]       @  92  inline data (small events) or FAT path ref (blobs)
//   crc32(u32)        @ 156  CRC32 of bytes 0..155
//                     = 160 bytes total

#include "store/store_key.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// StoreEntryType
// ---------------------------------------------------------------------------

enum class StoreEntryType : uint8_t {
    SAVE         = 0x01,  // raw save blob (inline ≤64 B, or FAT path for larger)
    ACHIEVEMENT  = 0x02,  // payload[0] = 1 (unlocked)
    STAT         = 0x03,  // payload = int32_t (absolute value)
    STAT_DELTA   = 0x04,  // payload = int32_t (relative increment)
    LEADER_ENTRY = 0x05,  // payload = int32_t score + char tag[60]
    PROFILE      = 0x06,  // payload = ProfilePayload: name[32] + lang[8] + flags[1] + pad[23]
};

// ---------------------------------------------------------------------------
// SyncState
// ---------------------------------------------------------------------------

enum class SyncState : uint8_t {
    PENDING    = 0x00,  // not yet uploaded; must be kept until synced
    SYNCED     = 0x01,  // successfully uploaded; eligible for compaction
    CHECKPOINT = 0x02,  // synthetic; written by compact(), never has a parent
};

// ---------------------------------------------------------------------------
// StoreRecord
// ---------------------------------------------------------------------------

static constexpr uint8_t STORE_RECORD_MAGIC[4] = { 0x45, 0x4C, 0x47, 0x21 };
static constexpr size_t  STORE_INLINE_MAX       = 64u;

struct StoreRecord {
    uint8_t  magic[4];
    uint8_t  uuid[16];
    uint8_t  parent[16];
    uint8_t  key[32];
    uint8_t  device_uuid[16];
    uint32_t timestamp;
    uint16_t payload_len;
    uint8_t  event_type;
    uint8_t  sync_state;
    uint8_t  payload[64];
    uint32_t crc32;
};

static_assert(sizeof(StoreRecord) == 160, "StoreRecord must be exactly 160 bytes");
