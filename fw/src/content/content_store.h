#pragma once
// content_store.h — Read-side view of the installed collection from FAT.
//
// FAT file layout:
//   1:/collections/active.txt                       — "active" or absent/other
//   1:/collections/{col_id}/collection.bin          — CollectionRecord
//   1:/collections/{col_id}/payload_{id}.bin        — PayloadRecord
//
// FatVolume must be mounted. Thread safety: NOT thread-safe.

#include "content/collection_format.h"
#include "diag/diag.h"

class ContentStore {
public:
    ContentStore() = default;

    // Returns true if the active collection marker is present.
    bool has_active_collection() const;

    // Read the installed CollectionRecord from FAT.
    DiagStatus load_collection(CollectionRecord& out) const;

    // Read a PayloadRecord by payload_id.
    DiagStatus load_payload(const char* payload_id, PayloadRecord& out) const;

    // Load the default payload for the active collection.
    DiagStatus load_default_payload(PayloadRecord& out) const;

private:
    static constexpr const char* ACTIVE_PATH = "1:/collections/active.txt";
    static constexpr const char* COL_DIR     = "1:/collections";

    // Build paths for the active collection (reads active.txt first).
    static bool active_col_id(char* id_out, size_t id_sz);
    static void col_record_path(const char* col_id, char* out, size_t sz);
    static void payload_path(const char* col_id, const char* payload_id,
                              char* out, size_t sz);
};
