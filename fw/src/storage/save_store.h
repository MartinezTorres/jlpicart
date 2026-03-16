#pragma once
// save_store.h — Blob CRUD interface for game save data (Stage 19).
//
// SaveStore wraps a KvStore (SAVES_KV partition) to provide named blob storage.
// Blobs are keyed by profile_id + blob_id: "sav.<profile_id_hex>.<blob_id_hex>".
// Blobs are small (≤ KV_MAX_VAL_LEN = 512 bytes) and written atomically.
//
// Multi-chunk writes use an in-progress handle table (SAVE_WRITE_HANDLES slots).
// Call write_begin(), then one or more write_chunk() calls, then write_commit().
//
// Spec reference: §7.2 Storage service (0x01).

#include "diag/diag.h"
#include <cstddef>
#include <cstdint>

static constexpr int SAVE_WRITE_HANDLES = 4;  // max concurrent in-progress writes
static constexpr uint16_t SAVE_MAX_BLOB = 512u; // KV_MAX_VAL_LEN

class KvStore;
class ProfileStore;

struct BlobInfo {
    uint16_t blob_id;
    uint16_t size;       // actual stored bytes
    uint16_t max_bytes;  // always SAVE_MAX_BLOB for v1
    uint16_t flags;      // user-defined flags; stored as written
};

class SaveStore {
public:
    // Attach to a KvStore and optional ProfileStore reference.
    // Both must outlive SaveStore.
    void init(KvStore& kv, ProfileStore& ps);

    bool initialized() const { return initialized_; }

    // List blobs owned by profile_id of the given kind (0 = all kinds).
    // Fills out[0..max_out-1] with BlobInfo; returns actual count written.
    uint8_t list(uint16_t profile_id, uint8_t kind,
                 BlobInfo* out, uint8_t max_out) const;

    // Read blob bytes into buf[offset .. offset+len-1].
    // Returns STORAGE_NOT_FOUND if the blob does not exist.
    DiagStatus read(uint16_t profile_id, uint16_t blob_id,
                    uint32_t offset, uint8_t* buf, uint16_t len) const;

    // Begin a multi-chunk write.  Allocates a write handle.
    // total_len must be ≤ SAVE_MAX_BLOB.
    // Returns STORAGE_FULL if no handle slots are free.
    DiagStatus write_begin(uint16_t profile_id, uint16_t blob_id,
                           uint16_t total_len, uint16_t flags,
                           uint8_t* handle_out);

    // Accumulate chunk data into the staging buffer.
    // offset + len must be ≤ total_len declared in write_begin.
    DiagStatus write_chunk(uint8_t handle, uint32_t offset,
                           const uint8_t* buf, uint16_t len);

    // Commit the staged blob to KV atomically.  Releases the handle.
    DiagStatus write_commit(uint8_t handle);

    // Delete blob.  Returns success if the blob does not exist.
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

    KvStore*       kv_          = nullptr;
    ProfileStore*  ps_          = nullptr;
    bool           initialized_ = false;
    WriteHandle    handles_[SAVE_WRITE_HANDLES] = {};

    // Build the KV key for a (profile_id, blob_id) pair.
    static void make_key(char* out, size_t out_size,
                         uint16_t profile_id, uint16_t blob_id);
};
