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

#include "store/store_key.h"
#include "store/store_record.h"
#include "store/uuid.h"
#include "diag/diag.h"
#include <cstddef>

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
