// save_store.cc — Save blob CRUD over KvStore (Stage 19).

#include "storage/save_store.h"
#include "storage/kv_store.h"
#include "profiles/profile_store.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Key scheme: "sav.<profile_id_4hex>.<blob_id_4hex>"
// Max key length: 4 + 1 + 4 + 1 + 4 = 14 bytes (well within KV_MAX_KEY_LEN=48)
// ---------------------------------------------------------------------------

void SaveStore::make_key(char* out, size_t out_size,
                         uint16_t profile_id, uint16_t blob_id)
{
    snprintf(out, out_size, "sav.%04x.%04x",
             static_cast<unsigned>(profile_id),
             static_cast<unsigned>(blob_id));
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void SaveStore::init(KvStore& kv, ProfileStore& ps)
{
    kv_  = &kv;
    ps_  = &ps;
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        handles_[i].in_use = false;
    }
    initialized_ = true;
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------
//
// Scans all live KV keys with "sav.<profile_hex>." prefix.
// kind is currently unused (v1 stores a single kind; kept for API forward-compat).

uint8_t SaveStore::list(uint16_t profile_id, uint8_t kind,
                        BlobInfo* out, uint8_t max_out) const
{
    if (!initialized_) return 0u;
    (void)kind; // reserved for future multi-kind support

    // Build the per-profile prefix: "sav.<profile_hex>."
    char prefix[12];
    snprintf(prefix, sizeof(prefix), "sav.%04x.",
             static_cast<unsigned>(profile_id));
    const size_t plen = strlen(prefix);

    uint8_t count = 0;

    // Walk the KV index via repeated get() attempts on known keys is impractical;
    // instead we iterate blob_id 0x0000..0xFFFF, which would be too slow.
    // SaveStore instead exposes a prefix-scan via a helper that iterates the index.
    //
    // Since KvStore does not expose its index publicly, we iterate via the
    // contains() + get() approach only for IDs that are registered in a separate
    // blob-list key.  For Stage 19 simplicity we maintain a per-profile list key
    // "sav.<profile_hex>.list" that stores an array of {blob_id, flags, size}.
    //
    // However, that would require update on every write/delete.  Even simpler:
    // encode a blob registry into a dedicated registry key per profile.
    //
    // For v1 we take a practical shortcut: store each blob at a predictable key
    // and maintain a compact registry key "sav.<profile_hex>._idx" that contains
    // a list of uint16_t blob IDs currently in use.

    char reg_key[16];
    snprintf(reg_key, sizeof(reg_key), "sav.%04x._idx",
             static_cast<unsigned>(profile_id));
    (void)plen; // suppress unused-variable warning

    uint8_t  idx_buf[SAVE_MAX_BLOB];
    uint16_t idx_len = 0;
    DiagStatus s = kv_->get(reg_key, idx_buf, &idx_len, sizeof(idx_buf));
    if (!s.ok() || idx_len < 4u) {
        return 0u; // no blobs
    }

    // Registry format: u16 count, then count × { u16 blob_id, u16 flags, u16 size }
    uint16_t n;
    memcpy(&n, idx_buf, 2u);
    const uint16_t entry_size = 6u; // blob_id + flags + size
    if (idx_len < static_cast<uint16_t>(2u + n * entry_size)) {
        return 0u; // corrupt index
    }

    for (uint16_t i = 0; i < n && count < max_out; ++i) {
        const uint8_t* e = idx_buf + 2u + i * entry_size;
        BlobInfo& bi = out[count++];
        memcpy(&bi.blob_id,   e + 0, 2);
        memcpy(&bi.flags,     e + 2, 2);
        memcpy(&bi.size,      e + 4, 2);
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

    char key[20];
    make_key(key, sizeof(key), profile_id, blob_id);

    uint8_t  val[SAVE_MAX_BLOB];
    uint16_t val_len = 0;
    DiagStatus s = kv_->get(key, val, &val_len, sizeof(val));
    if (!s.ok()) return s;

    if (offset >= val_len) {
        // Offset beyond blob end — return empty read (not an error).
        return DiagStatus::success();
    }

    uint16_t avail = static_cast<uint16_t>(val_len - static_cast<uint16_t>(offset));
    if (len > avail) len = avail;
    memcpy(buf, val + offset, len);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// write_begin
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

// ---------------------------------------------------------------------------
// write_chunk
// ---------------------------------------------------------------------------

DiagStatus SaveStore::write_chunk(uint8_t handle, uint32_t offset,
                                   const uint8_t* buf, uint16_t len)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !handles_[handle].in_use) {
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    WriteHandle& h = handles_[handle];
    if (offset + len > h.total_len) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    memcpy(h.staging + offset, buf, len);
    if (offset + len > h.bytes_written) {
        h.bytes_written = static_cast<uint16_t>(offset + len);
    }
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// write_commit
// ---------------------------------------------------------------------------

DiagStatus SaveStore::write_commit(uint8_t handle)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !handles_[handle].in_use) {
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    WriteHandle& h = handles_[handle];

    // Write the blob data.
    char key[20];
    make_key(key, sizeof(key), h.profile_id, h.blob_id);
    DiagStatus s = kv_->put(key, h.staging, h.total_len);
    if (!s.ok()) {
        h.in_use = false;
        return s;
    }

    // Update the registry for this profile.
    char reg_key[16];
    snprintf(reg_key, sizeof(reg_key), "sav.%04x._idx",
             static_cast<unsigned>(h.profile_id));

    uint8_t  idx_buf[SAVE_MAX_BLOB];
    uint16_t idx_len = 0;
    kv_->get(reg_key, idx_buf, &idx_len, sizeof(idx_buf));

    // Parse existing registry.
    uint16_t n = 0;
    if (idx_len >= 2u) memcpy(&n, idx_buf, 2u);

    constexpr uint16_t entry_size = 6u;
    const uint16_t max_entries = static_cast<uint16_t>(
        (SAVE_MAX_BLOB - 2u) / entry_size);

    // Check if blob_id is already in registry.
    bool found = false;
    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = idx_buf + 2u + i * entry_size;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == h.blob_id) {
            // Update size and flags in place.
            memcpy(e + 2, &h.flags,     2u);
            memcpy(e + 4, &h.total_len, 2u);
            found = true;
            break;
        }
    }

    if (!found && n < max_entries) {
        // Append new entry.
        uint8_t* e = idx_buf + 2u + n * entry_size;
        memcpy(e + 0, &h.blob_id,   2u);
        memcpy(e + 2, &h.flags,     2u);
        memcpy(e + 4, &h.total_len, 2u);
        ++n;
        idx_len = static_cast<uint16_t>(2u + n * entry_size);
    }

    if (idx_len < 2u) idx_len = 2u;
    memcpy(idx_buf, &n, 2u);
    kv_->put(reg_key, idx_buf, idx_len); // best-effort; don't fail commit on registry error

    h.in_use = false;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// delete_blob
// ---------------------------------------------------------------------------

DiagStatus SaveStore::delete_blob(uint16_t profile_id, uint16_t blob_id)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char key[20];
    make_key(key, sizeof(key), profile_id, blob_id);
    DiagStatus s = kv_->del(key);
    if (!s.ok()) return s;

    // Remove from registry.
    char reg_key[16];
    snprintf(reg_key, sizeof(reg_key), "sav.%04x._idx",
             static_cast<unsigned>(profile_id));

    uint8_t  idx_buf[SAVE_MAX_BLOB];
    uint16_t idx_len = 0;
    DiagStatus rs = kv_->get(reg_key, idx_buf, &idx_len, sizeof(idx_buf));
    if (!rs.ok() || idx_len < 2u) return DiagStatus::success();

    uint16_t n = 0;
    memcpy(&n, idx_buf, 2u);
    constexpr uint16_t entry_size = 6u;

    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = idx_buf + 2u + i * entry_size;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == blob_id) {
            // Shift remaining entries left.
            uint16_t remaining = static_cast<uint16_t>(n - i - 1u);
            if (remaining > 0) {
                memmove(e, e + entry_size, remaining * entry_size);
            }
            --n;
            idx_len = static_cast<uint16_t>(2u + n * entry_size);
            memcpy(idx_buf, &n, 2u);
            kv_->put(reg_key, idx_buf, idx_len);
            break;
        }
    }

    return DiagStatus::success();
}
