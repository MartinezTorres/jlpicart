#pragma once
// content_store.h — Read-side view of the installed collection and its payloads.
//
// ContentStore wraps a KvStore and provides typed accessors for the records
// written by Installer::run() and populate_flash.py.
//
// Key layout in SYSTEM_KV:
//   KV_COL_STATE      → "active" | "pending"
//   KV_COL_RECORD     → CollectionRecord (357 bytes, packed)
//   "pl.<payload_id>" → PayloadRecord    (100 bytes, packed)
//
// Thread safety: NOT thread-safe. Use only from the boot/preflight path.
// See bootstrapping.md Stage 10.

#include "content/collection_format.h"
#include "storage/kv_store.h"
#include "diag/diag.h"

class ContentStore {
public:
    explicit ContentStore(KvStore& kv) : kv_(kv) {}

    // Returns true if KV_COL_STATE == "active" (a committed collection is present).
    bool has_active_collection() const;

    // Read the installed CollectionRecord.
    // Returns STORAGE_NOT_FOUND if no active collection or record is absent.
    DiagStatus load_collection(CollectionRecord& out) const;

    // Read a PayloadRecord by payload_id.
    // Returns STORAGE_NOT_FOUND if the key "pl.<payload_id>" is absent.
    DiagStatus load_payload(const char* payload_id, PayloadRecord& out) const;

    // Load the default payload for the active collection.
    // Reads CollectionRecord.default_payload_id, then calls load_payload().
    // Returns STORAGE_NOT_FOUND if there is no active collection, no default
    // payload id is set, or the PayloadRecord has not been written yet.
    DiagStatus load_default_payload(PayloadRecord& out) const;

private:
    KvStore& kv_;
};
