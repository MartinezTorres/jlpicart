// content_store.cc — ContentStore implementation.

#include "content/content_store.h"
#include <cstring>
#include <cstdio>

bool ContentStore::has_active_collection() const {
    uint8_t  val[16];
    uint16_t val_len = 0;
    DiagStatus s = kv_.get(KV_COL_STATE, val, &val_len, static_cast<uint16_t>(sizeof(val)));
    if (!s.ok() || val_len == 0) return false;
    // val is not NUL-terminated from KvStore; compare by length + content.
    const size_t active_len = strlen(COL_STATE_ACTIVE);
    return val_len == active_len &&
           memcmp(val, COL_STATE_ACTIVE, active_len) == 0;
}

DiagStatus ContentStore::load_collection(CollectionRecord& out) const {
    uint16_t val_len = 0;
    DiagStatus s = kv_.get(KV_COL_RECORD,
                            reinterpret_cast<uint8_t*>(&out),
                            &val_len,
                            static_cast<uint16_t>(sizeof(out)));
    if (!s.ok()) return s;
    if (val_len != sizeof(CollectionRecord)) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }
    return DiagStatus::success();
}

DiagStatus ContentStore::load_payload(const char* payload_id, PayloadRecord& out) const {
    char key[KV_MAX_KEY_LEN + 1];
    // Build "pl.<payload_id>" — truncate silently if payload_id is too long.
    snprintf(key, sizeof(key), "%s%s", KV_PAYLOAD_PREFIX, payload_id);

    uint16_t val_len = 0;
    DiagStatus s = kv_.get(key,
                            reinterpret_cast<uint8_t*>(&out),
                            &val_len,
                            static_cast<uint16_t>(sizeof(out)));
    if (!s.ok()) return s;
    if (val_len != sizeof(PayloadRecord)) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }
    return DiagStatus::success();
}

DiagStatus ContentStore::load_default_payload(PayloadRecord& out) const {
    CollectionRecord col = {};
    DiagStatus s = load_collection(col);
    if (!s.ok()) return s;

    if (col.default_payload_id[0] == '\0') {
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    return load_payload(col.default_payload_id, out);
}
