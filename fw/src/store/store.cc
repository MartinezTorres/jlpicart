// event_log.cc — FAT-backed append-only event log.

#include "store/store.h"
#include "filesystem/fat_util.h"
#include "crypto/crc32.h"
#include "diag/log.h"
#include "ff.h"
#include <cstring>
#include <cstdio>

#ifndef JLPICART_HOST_TEST
#include "pico/time.h"
static uint32_t now_seconds() {
    return static_cast<uint32_t>(time_us_64() / 1000000ull);
}
#else
static uint32_t g_ts = 1;
static uint32_t now_seconds() { return g_ts++; }
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static uint32_t record_crc(const StoreRecord& r)
{
    return crc32_bytes(&r, sizeof(r) - sizeof(r.crc32));
}

static bool record_valid(const StoreRecord& r)
{
    if (memcmp(r.magic, STORE_RECORD_MAGIC, 4) != 0) return false;
    if (r.payload_len > STORE_INLINE_MAX)             return false;
    return record_crc(r) == r.crc32;
}

static void record_seal(StoreRecord& r)
{
    r.crc32 = record_crc(r);
}

// ---------------------------------------------------------------------------
// Device UUID
// ---------------------------------------------------------------------------

DiagStatus Store::load_or_create_device_uuid()
{
    char hex[33] = {};
    size_t actual = 0;
    if (fat_read_file(DEV_UUID_PATH, hex, 32, &actual) && actual == 32) {
        if (Uuid::from_hex(hex, device_uuid_))
            return DiagStatus::success();
    }
    device_uuid_ = Uuid::generate();
    device_uuid_.to_hex(hex);
    fat_ensure_dir("1:/system");
    if (!fat_write_file(DEV_UUID_PATH, hex, 32))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void Store::scan_counts()
{
    record_count_  = 0;
    pending_count_ = 0;
    FIL f;
    if (f_open(&f, LOG_PATH, FA_READ) != FR_OK) return;
    StoreRecord rec;
    UINT br;
    while (f_read(&f, &rec, sizeof(rec), &br) == FR_OK && br == sizeof(rec)) {
        if (!record_valid(rec)) continue;
        ++record_count_;
        if (static_cast<SyncState>(rec.sync_state) == SyncState::PENDING)
            ++pending_count_;
    }
    f_close(&f);
}

DiagStatus Store::init()
{
    fat_ensure_dir("1:/system");

    // Recover from interrupted compaction: tmp exists but log does not.
    if (!fat_file_exists(LOG_PATH) && fat_file_exists(TEMP_PATH))
        f_rename(TEMP_PATH, LOG_PATH);

    DiagStatus s = load_or_create_device_uuid();
    if (!s.ok()) return s;

    if (!fat_file_exists(LOG_PATH)) {
        if (!fat_write_file(LOG_PATH, nullptr, 0))
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    scan_counts();
    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// append
// ---------------------------------------------------------------------------

DiagStatus Store::append(StoreRecord& rec)
{
    if (!initialized_)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    if (fat_file_size(LOG_PATH) >= STORE_MAX_BYTES)
        return DiagStatus::error(DiagCode::STORAGE_FULL);

    Uuid u = Uuid::generate();
    memcpy(rec.magic,       STORE_RECORD_MAGIC,  4);
    memcpy(rec.uuid,        u.bytes,            16);
    memcpy(rec.device_uuid, device_uuid_.bytes, 16);
    rec.timestamp  = now_seconds();
    rec.sync_state = static_cast<uint8_t>(SyncState::PENDING);
    record_seal(rec);

    if (!fat_append_bytes(LOG_PATH, &rec, sizeof(rec)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);

    ++record_count_;
    ++pending_count_;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// find_latest  (scan backward — stops on first key match)
// ---------------------------------------------------------------------------

DiagStatus Store::find_latest(const StoreKey& key, StoreRecord& out) const
{
    FIL f;
    if (f_open(&f, LOG_PATH, FA_READ) != FR_OK)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    size_t total = static_cast<size_t>(f_size(&f) / sizeof(StoreRecord));
    bool   found = false;

    for (size_t i = total; i > 0; --i) {
        if (f_lseek(&f, static_cast<FSIZE_t>((i - 1) * sizeof(StoreRecord))) != FR_OK)
            continue;
        StoreRecord rec;
        UINT br;
        if (f_read(&f, &rec, sizeof(rec), &br) != FR_OK || br != sizeof(rec))
            continue;
        if (!record_valid(rec)) continue;
        if (memcmp(rec.key, key.data, 32) != 0) continue;
        out   = rec;
        found = true;
        break;
    }
    f_close(&f);
    return found ? DiagStatus::success()
                 : DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// iterate
// ---------------------------------------------------------------------------

void Store::iterate(IterFn fn, void* ctx) const
{
    FIL f;
    if (f_open(&f, LOG_PATH, FA_READ) != FR_OK) return;
    StoreRecord rec;
    UINT br;
    while (f_read(&f, &rec, sizeof(rec), &br) == FR_OK && br == sizeof(rec)) {
        if (!record_valid(rec)) continue;
        if (!fn(rec, ctx)) break;
    }
    f_close(&f);
}

// ---------------------------------------------------------------------------
// mark_synced helpers  (in-place rewrite of sync_state field)
// ---------------------------------------------------------------------------

static void mark_records_synced(const char* path,
                                  bool (*match)(const StoreRecord&, void* ctx),
                                  void* ctx,
                                  size_t* pending_out)
{
    *pending_out = 0;
    FIL f;
    if (f_open(&f, path, FA_READ | FA_WRITE) != FR_OK) return;

    size_t total = static_cast<size_t>(f_size(&f) / sizeof(StoreRecord));
    for (size_t i = 0; i < total; ++i) {
        FSIZE_t pos = static_cast<FSIZE_t>(i * sizeof(StoreRecord));
        if (f_lseek(&f, pos) != FR_OK) continue;
        StoreRecord rec;
        UINT br;
        if (f_read(&f, &rec, sizeof(rec), &br) != FR_OK || br != sizeof(rec)) continue;
        if (!record_valid(rec)) continue;

        bool is_pending = (static_cast<SyncState>(rec.sync_state) == SyncState::PENDING);
        if (is_pending && match(rec, ctx)) {
            rec.sync_state = static_cast<uint8_t>(SyncState::SYNCED);
            record_seal(rec);
            f_lseek(&f, pos);
            UINT bw;
            f_write(&f, &rec, sizeof(rec), &bw);
        } else if (is_pending) {
            ++(*pending_out);
        }
    }
    f_close(&f);
}

static bool match_key(const StoreRecord& r, void* ctx)
{
    return memcmp(r.key, static_cast<const uint8_t*>(ctx), 32) == 0;
}

static bool match_all(const StoreRecord&, void*) { return true; }

DiagStatus Store::mark_synced(const StoreKey& key)
{
    size_t pending;
    mark_records_synced(LOG_PATH, match_key,
                         const_cast<void*>(static_cast<const void*>(key.data)),
                         &pending);
    pending_count_ = pending;
    return DiagStatus::success();
}

DiagStatus Store::mark_all_synced()
{
    size_t pending;
    mark_records_synced(LOG_PATH, match_all, nullptr, &pending);
    pending_count_ = 0;
    (void)pending;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// compact
// ---------------------------------------------------------------------------

DiagStatus Store::compact()
{
    if (!initialized_)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    // Index: for each unique key, track the file offset of its most recent
    // SYNCED or CHECKPOINT record.
    struct KeyEntry {
        uint8_t  key[32];
        uint32_t best_off;
        bool     has_best;
    };
    static KeyEntry index[COMPACT_KEY_MAX];
    size_t key_count = 0;
    memset(index, 0, sizeof(index));

    // Pass 1 — build index.
    {
        FIL f;
        if (f_open(&f, LOG_PATH, FA_READ) != FR_OK)
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        StoreRecord rec;
        UINT br;
        uint32_t off = 0;
        while (f_read(&f, &rec, sizeof(rec), &br) == FR_OK && br == sizeof(rec)) {
            if (record_valid(rec)) {
                SyncState ss = static_cast<SyncState>(rec.sync_state);
                if (ss == SyncState::SYNCED || ss == SyncState::CHECKPOINT) {
                    KeyEntry* e = nullptr;
                    for (size_t i = 0; i < key_count; ++i) {
                        if (memcmp(index[i].key, rec.key, 32) == 0) {
                            e = &index[i]; break;
                        }
                    }
                    if (!e && key_count < COMPACT_KEY_MAX) {
                        e = &index[key_count++];
                        memcpy(e->key, rec.key, 32);
                        e->has_best = false;
                    }
                    if (e) { e->best_off = off; e->has_best = true; }
                }
            }
            off += sizeof(StoreRecord);
        }
        f_close(&f);
    }

    // Pass 2 — write temp file.
    fat_delete_file(TEMP_PATH);
    {
        FIL src, dst;
        if (f_open(&src, LOG_PATH, FA_READ) != FR_OK)
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        if (f_open(&dst, TEMP_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            f_close(&src);
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }

        // Write one CHECKPOINT per key.
        for (size_t i = 0; i < key_count; ++i) {
            if (!index[i].has_best) continue;
            if (f_lseek(&src, index[i].best_off) != FR_OK) continue;
            StoreRecord rec;
            UINT br;
            if (f_read(&src, &rec, sizeof(rec), &br) != FR_OK || br != sizeof(rec)) continue;
            if (!record_valid(rec)) continue;

            Uuid nu = Uuid::generate();
            memcpy(rec.uuid,   nu.bytes, 16);
            memset(rec.parent, 0,        16);
            rec.sync_state = static_cast<uint8_t>(SyncState::CHECKPOINT);
            record_seal(rec);
            UINT bw;
            f_write(&dst, &rec, sizeof(rec), &bw);
        }

        // Append all PENDING records.
        if (f_lseek(&src, 0) == FR_OK) {
            StoreRecord rec;
            UINT br;
            while (f_read(&src, &rec, sizeof(rec), &br) == FR_OK && br == sizeof(rec)) {
                if (!record_valid(rec)) continue;
                if (static_cast<SyncState>(rec.sync_state) != SyncState::PENDING) continue;
                UINT bw;
                f_write(&dst, &rec, sizeof(rec), &bw);
            }
        }

        f_close(&src);
        f_close(&dst);
    }

    // Pass 3 — atomic rename.
    fat_delete_file(LOG_PATH);
    if (f_rename(TEMP_PATH, LOG_PATH) != FR_OK) {
        fat_write_file(LOG_PATH, nullptr, 0);  // restore empty log
        scan_counts();
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    scan_counts();
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// purge_pending
// ---------------------------------------------------------------------------

DiagStatus Store::purge_pending()
{
    if (!initialized_)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    fat_delete_file(TEMP_PATH);
    {
        FIL src, dst;
        if (f_open(&src, LOG_PATH, FA_READ) != FR_OK)
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        if (f_open(&dst, TEMP_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
            f_close(&src);
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        StoreRecord rec;
        UINT br;
        while (f_read(&src, &rec, sizeof(rec), &br) == FR_OK && br == sizeof(rec)) {
            if (!record_valid(rec)) continue;
            if (static_cast<SyncState>(rec.sync_state) == SyncState::PENDING) continue;
            UINT bw;
            f_write(&dst, &rec, sizeof(rec), &bw);
        }
        f_close(&src);
        f_close(&dst);
    }

    fat_delete_file(LOG_PATH);
    if (f_rename(TEMP_PATH, LOG_PATH) != FR_OK) {
        fat_write_file(LOG_PATH, nullptr, 0);
        scan_counts();
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    scan_counts();
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// pressure
// ---------------------------------------------------------------------------

float Store::pressure() const
{
    size_t sz = fat_file_size(LOG_PATH);
    return static_cast<float>(sz) / static_cast<float>(STORE_MAX_BYTES);
}
