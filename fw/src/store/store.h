#pragma once
// store.h — FAT-backed append-only store with sync tracking and compaction.
//
// Storage:
//   1:/system/events.bin   — the live log (fixed-size 160-byte records)
//   1:/system/events.tmp   — scratch file during compaction
//   1:/system/device.uuid  — stable 32-char hex UUID for this cartridge
//
// Compaction (compact()):
//   For each key, the most recent SYNCED record becomes a CHECKPOINT and all
//   older SYNCED records are dropped.  All PENDING records are preserved.
//   Uses atomic rename: writes events.tmp fully, then deletes events.bin,
//   then renames.  init() recovers an interrupted compaction automatically.
//
// Pressure (pressure()):
//   Ratio of current file size to STORE_MAX_BYTES (320 KB ≈ 2048 records).
//   At >= 0.8 the caller should compact or warn the user.

#include "store/uuid.h"
#include "diag/diag.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// StoreKey — 32-byte key identifying what an event record modifies.
// ---------------------------------------------------------------------------

enum class StoreKeyType : uint8_t {
    SAVE         = 0x01,
    ACHIEVEMENT  = 0x02,
    STAT         = 0x03,
    LEADER_ENTRY = 0x04,
    PROFILE      = 0x05,
};

struct StoreKey {
    uint8_t data[32] = {};

    StoreKeyType type() const { return static_cast<StoreKeyType>(data[0]); }
    bool operator==(const StoreKey& o) const { return memcmp(data, o.data, 32) == 0; }
    bool operator!=(const StoreKey& o) const { return !(*this == o); }

    static StoreKey for_save(const Uuid& profile, const char* slot_str);
    static StoreKey for_achievement(const Uuid& profile, const char* payload_id,
                                     const char* ach_id);
    static StoreKey for_stat(const Uuid& profile, const char* payload_id,
                              const char* stat_name);
    static StoreKey for_leaderboard(const Uuid& profile, const char* payload_id,
                                     const char* board_id);
    static StoreKey for_profile(const Uuid& profile);

private:
    static uint32_t fnv32(const char* s) {
        uint32_t h = 2166136261u;
        for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
        return h;
    }
    static void put_u32le(uint8_t* dst, uint32_t v) {
        dst[0] = v & 0xFFu; dst[1] = (v >> 8) & 0xFFu;
        dst[2] = (v >> 16) & 0xFFu; dst[3] = (v >> 24) & 0xFFu;
    }
};

inline StoreKey StoreKey::for_save(const Uuid& p, const char* slot_str) {
    StoreKey k; k.data[0] = static_cast<uint8_t>(StoreKeyType::SAVE);
    memcpy(k.data + 1, p.bytes, 16); put_u32le(k.data + 17, fnv32(slot_str)); return k;
}
inline StoreKey StoreKey::for_achievement(const Uuid& p, const char* pid, const char* aid) {
    StoreKey k; k.data[0] = static_cast<uint8_t>(StoreKeyType::ACHIEVEMENT);
    memcpy(k.data + 1, p.bytes, 16); put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(aid)); return k;
}
inline StoreKey StoreKey::for_stat(const Uuid& p, const char* pid, const char* sname) {
    StoreKey k; k.data[0] = static_cast<uint8_t>(StoreKeyType::STAT);
    memcpy(k.data + 1, p.bytes, 16); put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(sname)); return k;
}
inline StoreKey StoreKey::for_leaderboard(const Uuid& p, const char* pid, const char* bid) {
    StoreKey k; k.data[0] = static_cast<uint8_t>(StoreKeyType::LEADER_ENTRY);
    memcpy(k.data + 1, p.bytes, 16); put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(bid)); return k;
}
inline StoreKey StoreKey::for_profile(const Uuid& p) {
    StoreKey k; k.data[0] = static_cast<uint8_t>(StoreKeyType::PROFILE);
    memcpy(k.data + 1, p.bytes, 16); return k;
}

// ---------------------------------------------------------------------------
// StoreRecord — Fixed-size 160-byte record for the append-only event log.
// ---------------------------------------------------------------------------

enum class StoreEntryType : uint8_t {
    SAVE         = 0x01,
    ACHIEVEMENT  = 0x02,
    STAT         = 0x03,
    STAT_DELTA   = 0x04,
    LEADER_ENTRY = 0x05,
    PROFILE      = 0x06,
};

enum class SyncState : uint8_t {
    PENDING    = 0x00,
    SYNCED     = 0x01,
    CHECKPOINT = 0x02,
};

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

// ---------------------------------------------------------------------------

static constexpr size_t STORE_MAX_BYTES = 320u * 1024u;

class Store {
public:
    DiagStatus init();
    bool initialized() const { return initialized_; }

    // Append a new event.  Caller fills: key, event_type, payload, payload_len,
    // and optionally parent.  append() fills: magic, uuid, device_uuid,
    // timestamp, sync_state (PENDING), and crc32.
    DiagStatus append(StoreRecord& rec);

    // Return the most recent valid record for key, newest first.
    // Returns STORAGE_NOT_FOUND if no record exists for that key.
    DiagStatus find_latest(const StoreKey& key, StoreRecord& out) const;

    // Iterate all valid records, oldest first.
    // Return false from fn to stop early.
    using IterFn = bool(*)(const StoreRecord&, void* ctx);
    void iterate(IterFn fn, void* ctx) const;

    // Mark all PENDING records for the given key as SYNCED.
    DiagStatus mark_synced(const StoreKey& key);

    // Mark every PENDING record as SYNCED (call after a full successful upload).
    DiagStatus mark_all_synced();

    // Collapse SYNCED records to one CHECKPOINT per key; preserve PENDING.
    DiagStatus compact();

    // Delete all PENDING records (destructive, requires user confirmation).
    DiagStatus purge_pending();

    float  pressure()      const;
    size_t record_count()  const { return record_count_;  }
    size_t pending_count() const { return pending_count_; }

private:
    static constexpr const char* LOG_PATH      = "1:/system/events.bin";
    static constexpr const char* TEMP_PATH     = "1:/system/events.tmp";
    static constexpr const char* DEV_UUID_PATH = "1:/system/device.uuid";
    static constexpr size_t      COMPACT_KEY_MAX = 256;

    bool   initialized_   = false;
    size_t record_count_  = 0;
    size_t pending_count_ = 0;
    Uuid   device_uuid_   = {};

    DiagStatus load_or_create_device_uuid();
    void       scan_counts();

    // Rewrite the log keeping only records selected by keep_fn.
    // After rename, scan_counts() is called automatically.
    DiagStatus rewrite(bool (*keep_fn)(const StoreRecord&, void* ctx), void* ctx);
};
