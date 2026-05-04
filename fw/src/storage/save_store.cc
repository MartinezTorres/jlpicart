// save_store.cc — Save blob CRUD over FAT files + optional Store sync tracking.

#include "storage/save_store.h"
#include "storage/fat_util.h"
#include "storage/store.h"
#include "storage/store_record.h"
#include "storage/store_key.h"
#include "profiles/profile_store.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

void SaveStore::make_prof_dir(char* buf, size_t sz, uint16_t profile_id)
{
    snprintf(buf, sz, "1:/saves/%04x", static_cast<unsigned>(profile_id));
}

void SaveStore::make_blob_path(char* buf, size_t sz,
                                uint16_t profile_id, uint16_t blob_id)
{
    snprintf(buf, sz, "1:/saves/%04x/%04x.sav",
             static_cast<unsigned>(profile_id),
             static_cast<unsigned>(blob_id));
}

void SaveStore::make_idx_path(char* buf, size_t sz, uint16_t profile_id)
{
    snprintf(buf, sz, "1:/saves/%04x/_idx.bin",
             static_cast<unsigned>(profile_id));
}

// ---------------------------------------------------------------------------
// Registry format: u16 count, then count × { u16 blob_id, u16 flags, u16 size }
// ---------------------------------------------------------------------------

static constexpr uint16_t REG_ENTRY_SIZE = 6u;
static constexpr uint16_t REG_MAX_ENTRIES =
    static_cast<uint16_t>((SAVE_MAX_BLOB - 2u) / REG_ENTRY_SIZE);

void SaveStore::update_registry(uint16_t profile_id, uint16_t blob_id,
                                 uint16_t flags, uint16_t size)
{
    char idx_path[48];
    make_idx_path(idx_path, sizeof(idx_path), profile_id);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
    fat_read_file(idx_path, buf, sizeof(buf), &actual);

    uint16_t n = 0;
    if (actual >= 2u) memcpy(&n, buf, 2u);

    // Look for existing entry to update.
    bool found = false;
    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == blob_id) {
            memcpy(e + 2, &flags, 2u);
            memcpy(e + 4, &size,  2u);
            found = true;
            break;
        }
    }
    if (!found && n < REG_MAX_ENTRIES) {
        uint8_t* e = buf + 2u + n * REG_ENTRY_SIZE;
        memcpy(e + 0, &blob_id, 2u);
        memcpy(e + 2, &flags,   2u);
        memcpy(e + 4, &size,    2u);
        ++n;
    }
    memcpy(buf, &n, 2u);
    size_t total = static_cast<size_t>(2u + n * REG_ENTRY_SIZE);
    fat_write_file(idx_path, buf, total);
}

void SaveStore::remove_from_registry(uint16_t profile_id, uint16_t blob_id)
{
    char idx_path[48];
    make_idx_path(idx_path, sizeof(idx_path), profile_id);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
    if (!fat_read_file(idx_path, buf, sizeof(buf), &actual) || actual < 2u) return;

    uint16_t n = 0;
    memcpy(&n, buf, 2u);

    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == blob_id) {
            uint16_t rem = static_cast<uint16_t>(n - i - 1u);
            if (rem > 0) memmove(e, e + REG_ENTRY_SIZE, rem * REG_ENTRY_SIZE);
            --n;
            memcpy(buf, &n, 2u);
            size_t total = static_cast<size_t>(2u + n * REG_ENTRY_SIZE);
            fat_write_file(idx_path, buf, total);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void SaveStore::init(ProfileStore& ps)
{
    ps_  = &ps;
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) handles_[i].in_use = false;
    initialized_ = true;
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------

uint8_t SaveStore::list(uint16_t profile_id, uint8_t /*kind*/,
                        BlobInfo* out, uint8_t max_out) const
{
    if (!initialized_) return 0u;

    char idx_path[48];
    make_idx_path(idx_path, sizeof(idx_path), profile_id);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
    if (!fat_read_file(idx_path, buf, sizeof(buf), &actual) || actual < 4u) return 0u;

    uint16_t n;
    memcpy(&n, buf, 2u);
    if (actual < static_cast<size_t>(2u + n * REG_ENTRY_SIZE)) return 0u;

    uint8_t count = 0;
    for (uint16_t i = 0; i < n && count < max_out; ++i) {
        const uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        BlobInfo& bi = out[count++];
        memcpy(&bi.blob_id,  e + 0, 2);
        memcpy(&bi.flags,    e + 2, 2);
        memcpy(&bi.size,     e + 4, 2);
        bi.max_bytes = SAVE_MAX_BLOB;
    }
    return count;
}

// ---------------------------------------------------------------------------
// read
// ---------------------------------------------------------------------------

DiagStatus SaveStore::read(uint16_t profile_id, uint16_t blob_id,
                           uint32_t offset, uint8_t* buf, uint16_t len) const
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char path[48];
    make_blob_path(path, sizeof(path), profile_id, blob_id);

    uint8_t  val[SAVE_MAX_BLOB];
    size_t   val_len = 0;
    if (!fat_read_file(path, val, sizeof(val), &val_len))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    if (offset >= val_len) return DiagStatus::success();

    uint16_t avail = static_cast<uint16_t>(val_len - static_cast<uint16_t>(offset));
    if (len > avail) len = avail;
    memcpy(buf, val + offset, len);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// write_begin / write_chunk / write_commit
// ---------------------------------------------------------------------------

DiagStatus SaveStore::write_begin(uint16_t profile_id, uint16_t blob_id,
                                   uint16_t total_len, uint16_t flags,
                                   uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (total_len > SAVE_MAX_BLOB) return DiagStatus::error(DiagCode::STORAGE_FULL);

    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        if (!handles_[i].in_use) {
            handles_[i].in_use        = true;
            handles_[i].profile_id    = profile_id;
            handles_[i].blob_id       = blob_id;
            handles_[i].total_len     = total_len;
            handles_[i].flags         = flags;
            handles_[i].bytes_written = 0u;
            memset(handles_[i].staging, 0, sizeof(handles_[i].staging));
            if (handle_out) *handle_out = static_cast<uint8_t>(i);
            return DiagStatus::success();
        }
    }
    return DiagStatus::error(DiagCode::STORAGE_FULL);
}

DiagStatus SaveStore::write_chunk(uint8_t handle, uint32_t offset,
                                   const uint8_t* buf, uint16_t len)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !handles_[handle].in_use)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    WriteHandle& h = handles_[handle];
    if (offset + len > h.total_len) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    memcpy(h.staging + offset, buf, len);
    if (offset + len > h.bytes_written)
        h.bytes_written = static_cast<uint16_t>(offset + len);
    return DiagStatus::success();
}

DiagStatus SaveStore::write_commit(uint8_t handle)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !handles_[handle].in_use)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    WriteHandle& h = handles_[handle];

    // Ensure per-profile directory exists.
    char dir[32];
    make_prof_dir(dir, sizeof(dir), h.profile_id);
    fat_ensure_dir("1:/saves");
    fat_ensure_dir(dir);

    char path[48];
    make_blob_path(path, sizeof(path), h.profile_id, h.blob_id);
    if (!fat_write_file(path, h.staging, h.total_len)) {
        h.in_use = false;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    update_registry(h.profile_id, h.blob_id, h.flags, h.total_len);

    if (store_) {
        Uuid profile_uuid = ps_ ? ps_->uuid_for(h.profile_id) : Uuid::zero();
        char slot_str[8];
        snprintf(slot_str, sizeof(slot_str), "%u",
                 static_cast<unsigned>(h.blob_id));
        StoreRecord rec = {};
        StoreKey key = StoreKey::for_save(profile_uuid, slot_str);
        memcpy(rec.key, key.data, 32);
        rec.event_type = static_cast<uint8_t>(StoreEntryType::SAVE);
        uint16_t blob_id_le = h.blob_id;
        uint16_t size_le    = h.total_len;
        uint16_t flags_le   = h.flags;
        memcpy(rec.payload + 0, &blob_id_le, 2);
        memcpy(rec.payload + 2, &size_le,    2);
        memcpy(rec.payload + 4, &flags_le,   2);
        rec.payload_len = 6u;
        store_->append(rec);  // best-effort
    }

    h.in_use = false;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// delete_blob
// ---------------------------------------------------------------------------

DiagStatus SaveStore::delete_blob(uint16_t profile_id, uint16_t blob_id)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    char path[48];
    make_blob_path(path, sizeof(path), profile_id, blob_id);
    fat_delete_file(path);
    remove_from_registry(profile_id, blob_id);
    return DiagStatus::success();
}
