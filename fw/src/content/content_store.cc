// content_store.cc — ContentStore over FAT files.

#include "content/content_store.h"
#include "storage/fat_util.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Static path helpers
// ---------------------------------------------------------------------------

bool ContentStore::active_col_id(char* id_out, size_t id_sz)
{
    // active.txt contains the collection_id string (NUL-terminated or newline).
    char buf[COL_ID_MAX + 4];
    size_t actual = 0;
    if (!fat_read_file(ACTIVE_PATH, buf, sizeof(buf) - 1u, &actual) || actual == 0)
        return false;
    buf[actual] = '\0';
    // Strip trailing newline if present.
    for (size_t i = 0; i < actual; ++i) {
        if (buf[i] == '\n' || buf[i] == '\r') { buf[i] = '\0'; break; }
    }
    if (buf[0] == '\0') return false;
    size_t n = strlen(buf);
    if (n >= id_sz) n = id_sz - 1u;
    memcpy(id_out, buf, n);
    id_out[n] = '\0';
    return true;
}

void ContentStore::col_record_path(const char* col_id, char* out, size_t sz)
{
    snprintf(out, sz, "1:/collections/%s/collection.bin", col_id);
}

void ContentStore::payload_path(const char* col_id, const char* payload_id,
                                 char* out, size_t sz)
{
    snprintf(out, sz, "1:/collections/%s/payload_%s.bin", col_id, payload_id);
}

// ---------------------------------------------------------------------------
// has_active_collection
// ---------------------------------------------------------------------------

bool ContentStore::has_active_collection() const
{
    char id[COL_ID_MAX];
    return active_col_id(id, sizeof(id));
}

// ---------------------------------------------------------------------------
// load_collection
// ---------------------------------------------------------------------------

DiagStatus ContentStore::load_collection(CollectionRecord& out) const
{
    char col_id[COL_ID_MAX];
    if (!active_col_id(col_id, sizeof(col_id)))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    char path[128];
    col_record_path(col_id, path, sizeof(path));

    size_t actual = 0;
    if (!fat_read_file(path, &out, sizeof(out), &actual))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    if (actual != sizeof(CollectionRecord))
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// load_payload
// ---------------------------------------------------------------------------

DiagStatus ContentStore::load_payload(const char* payload_id, PayloadRecord& out) const
{
    char col_id[COL_ID_MAX];
    if (!active_col_id(col_id, sizeof(col_id)))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    char path[192];
    payload_path(col_id, payload_id, path, sizeof(path));

    size_t actual = 0;
    if (!fat_read_file(path, &out, sizeof(out), &actual))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    if (actual != sizeof(PayloadRecord))
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// load_default_payload
// ---------------------------------------------------------------------------

DiagStatus ContentStore::load_default_payload(PayloadRecord& out) const
{
    CollectionRecord col = {};
    DiagStatus s = load_collection(col);
    if (!s.ok()) return s;

    if (col.default_payload_id[0] == '\0')
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    return load_payload(col.default_payload_id, out);
}
