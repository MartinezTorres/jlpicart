#pragma once
// save_store.h — Blob CRUD interface for game save data backed by FAT.
//
// FAT file layout:
//   1:/saves/{prof_4hex}/{blob_4hex}.sav    — save blob (raw bytes)
//   1:/saves/{prof_4hex}/_idx.bin           — registry: u16 count + entries[]
//
// Blobs are ≤ SAVE_MAX_BLOB bytes.  Multi-chunk writes use staging handles.
//
// FatVolume must be mounted before calling init().
// Thread safety: NOT thread-safe.

#include "diag/diag.h"
#include <cstddef>
#include <cstdint>

static constexpr int     SAVE_WRITE_HANDLES = 4;
static constexpr uint16_t SAVE_MAX_BLOB     = 512u;

class ProfileStore;
class Store;

struct BlobInfo {
    uint16_t blob_id;
    uint16_t size;
    uint16_t max_bytes;  // always SAVE_MAX_BLOB
    uint16_t flags;
};

class SaveStore {
public:
    // Bind a Store for sync tracking.  Call before init().
    void bind_store(Store& s) { store_ = &s; }

    void init(ProfileStore& ps);

    bool initialized() const { return initialized_; }

    uint8_t list(uint16_t profile_id, uint8_t kind,
                 BlobInfo* out, uint8_t max_out) const;

    DiagStatus read(uint16_t profile_id, uint16_t blob_id,
                    uint32_t offset, uint8_t* buf, uint16_t len) const;

    DiagStatus write_begin(uint16_t profile_id, uint16_t blob_id,
                           uint16_t total_len, uint16_t flags,
                           uint8_t* handle_out);

    DiagStatus write_chunk(uint8_t handle, uint32_t offset,
                           const uint8_t* buf, uint16_t len);

    DiagStatus write_commit(uint8_t handle);

    DiagStatus delete_blob(uint16_t profile_id, uint16_t blob_id);

private:
    struct WriteHandle {
        bool     in_use;
        uint16_t profile_id;
        uint16_t blob_id;
        uint16_t total_len;
        uint16_t flags;
        uint16_t bytes_written;
        uint8_t  staging[SAVE_MAX_BLOB];
    };

    ProfileStore*  ps_          = nullptr;
    Store*         store_       = nullptr;
    bool           initialized_ = false;
    WriteHandle    handles_[SAVE_WRITE_HANDLES] = {};

    // Build per-profile directory path into buf.
    static void make_prof_dir(char* buf, size_t sz, uint16_t profile_id);
    // Build blob file path.
    static void make_blob_path(char* buf, size_t sz,
                                uint16_t profile_id, uint16_t blob_id);
    // Build registry file path.
    static void make_idx_path(char* buf, size_t sz, uint16_t profile_id);

    // Update registry for a new/updated blob.
    void update_registry(uint16_t profile_id, uint16_t blob_id,
                         uint16_t flags, uint16_t size);
    // Remove blob_id from registry.
    void remove_from_registry(uint16_t profile_id, uint16_t blob_id);
};
