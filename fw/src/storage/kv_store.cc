// kv_store.cc — Log-structured KV store implementation.

#include "storage/kv_store.h"
#include "crypto/crc32.h"
#include <cstring>
#include <algorithm>

// ---------------------------------------------------------------------------
// CRC helper: compute over {type, key_len, val_len (LE), key[], val[]}
// ---------------------------------------------------------------------------

static uint32_t record_crc(uint8_t type, uint8_t key_len, uint16_t val_len,
                            const char* key, const uint8_t* val)
{
    uint8_t  vlen_le[2] = { static_cast<uint8_t>(val_len & 0xFFu),
                             static_cast<uint8_t>(val_len >> 8u) };
    uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, &type,     1);
    crc = crc32_update(crc, &key_len,  1);
    crc = crc32_update(crc, vlen_le,   2);
    crc = crc32_update(crc, key,       key_len);
    if (val && val_len > 0) {
        crc = crc32_update(crc, val, val_len);
    }
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// init — scan partition and build in-memory index
// ---------------------------------------------------------------------------

DiagStatus KvStore::init(FlashDevice& dev, uint32_t part_ofs, uint32_t part_size)
{
    dev_        = &dev;
    part_ofs_   = part_ofs;
    part_size_  = part_size;
    write_ptr_  = 0;
    live_count_ = 0;
    initialized_= false;

    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        index_[i].used = false;
    }

    uint32_t ptr = 0;
    while (ptr + sizeof(KvRecordHdr) <= part_size_) {
        KvRecordHdr hdr;
        DiagStatus s = dev_->read(part_ofs_ + ptr, reinterpret_cast<uint8_t*>(&hdr),
                                  sizeof(hdr));
        if (!s.ok()) return s;

        if (hdr.type == KV_TYPE_ERASED) break;  // end of written log

        if (hdr.type != KV_TYPE_LIVE && hdr.type != KV_TYPE_TOMB) {
            // Unrecognised type: treat as corrupted tail — stop here.
            break;
        }
        if (hdr.key_len == 0 || hdr.key_len > KV_MAX_KEY_LEN) break;
        if (hdr.val_len > KV_MAX_VAL_LEN) break;

        uint32_t payload_len = static_cast<uint32_t>(hdr.key_len) + hdr.val_len;
        if (ptr + sizeof(KvRecordHdr) + payload_len > part_size_) break;

        // Read key + val for CRC verification.
        uint8_t  kv_buf[KV_MAX_KEY_LEN + KV_MAX_VAL_LEN];
        s = dev_->read(part_ofs_ + ptr + sizeof(KvRecordHdr), kv_buf, payload_len);
        if (!s.ok()) return s;

        const char*    key = reinterpret_cast<const char*>(kv_buf);
        const uint8_t* val = kv_buf + hdr.key_len;
        uint32_t expected_crc = record_crc(hdr.type, hdr.key_len, hdr.val_len,
                                            key,
                                            hdr.val_len > 0 ? val : nullptr);
        if (expected_crc != hdr.crc32) {
            // CRC mismatch — corrupted tail (torn write): stop scanning.
            break;
        }

        // Valid record — update the in-memory index.
        IndexEntry* entry = find_entry(key, hdr.key_len);
        if (!entry) {
            // New key: allocate a slot.
            for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
                if (!index_[i].used) { entry = &index_[i]; break; }
            }
        }
        if (!entry) {
            // More distinct keys than KV_MAX_ENTRIES — index overflow.
            // Continue scanning to advance write_ptr, but skip this record.
        } else {
            memcpy(entry->key, key, hdr.key_len);
            entry->key_len    = hdr.key_len;
            entry->val_len    = hdr.val_len;
            entry->record_ofs = ptr;
            entry->used       = true;
        }

        if (hdr.type == KV_TYPE_TOMB && entry) {
            entry->used = false;  // tombstone removes the key
        }

        ptr += static_cast<uint32_t>(sizeof(KvRecordHdr)) + payload_len;
    }

    write_ptr_ = ptr;

    // Count live entries.
    live_count_ = 0;
    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (index_[i].used) ++live_count_;
    }

    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// Index helpers
// ---------------------------------------------------------------------------

KvStore::IndexEntry* KvStore::find_entry(const char* key, uint8_t key_len)
{
    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (index_[i].used
            && index_[i].key_len == key_len
            && memcmp(index_[i].key, key, key_len) == 0) {
            return &index_[i];
        }
    }
    return nullptr;
}

const KvStore::IndexEntry* KvStore::find_entry(const char* key, uint8_t key_len) const
{
    for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
        if (index_[i].used
            && index_[i].key_len == key_len
            && memcmp(index_[i].key, key, key_len) == 0) {
            return &index_[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// get
// ---------------------------------------------------------------------------

DiagStatus KvStore::get(const char* key, uint8_t* val_out,
                        uint16_t* val_len_out, uint16_t max_val) const
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > KV_MAX_KEY_LEN) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }

    const IndexEntry* entry = find_entry(key, static_cast<uint8_t>(key_len));
    if (!entry) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    uint16_t copy_len = std::min(entry->val_len, max_val);
    if (copy_len > 0) {
        uint32_t val_ofs = part_ofs_ + entry->record_ofs
                         + sizeof(KvRecordHdr)
                         + entry->key_len;
        DiagStatus s = dev_->read(val_ofs, val_out, copy_len);
        if (!s.ok()) return s;
    }
    if (val_len_out) *val_len_out = entry->val_len;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// append_record — internal write helper
// ---------------------------------------------------------------------------

DiagStatus KvStore::append_record(uint8_t type, const char* key,
                                   const uint8_t* val, uint16_t val_len)
{
    size_t   key_len = strlen(key);
    uint32_t total   = sizeof(KvRecordHdr) + key_len + val_len;

    if (write_ptr_ + total > part_size_) {
        return DiagStatus::error(DiagCode::STORAGE_FULL);
    }

    // Build record in a single stack buffer.
    uint8_t buf[sizeof(KvRecordHdr) + KV_MAX_KEY_LEN + KV_MAX_VAL_LEN];
    auto*   hdr     = reinterpret_cast<KvRecordHdr*>(buf);
    char*   key_dst = reinterpret_cast<char*>(buf + sizeof(KvRecordHdr));
    uint8_t* val_dst = buf + sizeof(KvRecordHdr) + key_len;

    hdr->type    = type;
    hdr->key_len = static_cast<uint8_t>(key_len);
    hdr->val_len = val_len;
    memcpy(key_dst, key, key_len);
    if (val_len > 0) memcpy(val_dst, val, val_len);

    hdr->crc32 = record_crc(type, static_cast<uint8_t>(key_len), val_len,
                             key, val_len > 0 ? val : nullptr);

    DiagStatus s = dev_->write(part_ofs_ + write_ptr_, buf, total);
    if (!s.ok()) return s;

    write_ptr_ += total;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// put
// ---------------------------------------------------------------------------

DiagStatus KvStore::put(const char* key, const uint8_t* val, uint16_t val_len)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > KV_MAX_KEY_LEN) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }
    if (val_len > KV_MAX_VAL_LEN) return DiagStatus::error(DiagCode::STORAGE_FULL);

    uint32_t new_ofs = write_ptr_;
    DiagStatus s = append_record(KV_TYPE_LIVE, key, val, val_len);
    if (!s.ok()) return s;

    // Update or create index entry.
    IndexEntry* entry = find_entry(key, static_cast<uint8_t>(key_len));
    if (!entry) {
        for (size_t i = 0; i < KV_MAX_ENTRIES; ++i) {
            if (!index_[i].used) { entry = &index_[i]; break; }
        }
        if (!entry) return DiagStatus::error(DiagCode::STORAGE_FULL);
        ++live_count_;
    }
    memcpy(entry->key, key, key_len);
    entry->key_len    = static_cast<uint8_t>(key_len);
    entry->val_len    = val_len;
    entry->record_ofs = new_ofs;
    entry->used       = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// del
// ---------------------------------------------------------------------------

DiagStatus KvStore::del(const char* key)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > KV_MAX_KEY_LEN) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }

    IndexEntry* entry = find_entry(key, static_cast<uint8_t>(key_len));
    if (!entry) return DiagStatus::success();  // not present — success

    DiagStatus s = append_record(KV_TYPE_TOMB, key, nullptr, 0);
    if (!s.ok()) return s;

    entry->used = false;
    --live_count_;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// contains / bytes_free
// ---------------------------------------------------------------------------

bool KvStore::contains(const char* key) const
{
    if (!initialized_) return false;
    size_t key_len = strlen(key);
    if (key_len == 0 || key_len > KV_MAX_KEY_LEN) return false;
    return find_entry(key, static_cast<uint8_t>(key_len)) != nullptr;
}

uint32_t KvStore::bytes_free() const
{
    if (!initialized_ || write_ptr_ >= part_size_) return 0;
    return part_size_ - write_ptr_;
}
