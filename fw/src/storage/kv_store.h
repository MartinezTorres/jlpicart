#pragma once
// kv_store.h — Log-structured key-value store for SYSTEM_KV partition.
//
// Records are appended to a flat flash partition.  Overwrites append a new
// LIVE record; tombstones append a DEAD record.  Both leave old records
// intact — power loss mid-write leaves the old record valid (CRC protects).
//
// On init(), the partition is scanned forward to build an in-memory index.
// get() and del() operate on the index; put() appends to flash then updates
// the index.
//
// Constraints (Stage 6):
//   - Maximum 64 distinct live keys at any time.
//   - Key length: 1..KV_MAX_KEY_LEN (48) bytes.
//   - Value length: 0..KV_MAX_VAL_LEN (512) bytes.
//   - No in-place compaction in this stage; put() returns STORAGE_FULL when
//     the write pointer approaches the end of the partition.
//
// Thread safety: NOT thread-safe.

#include "diag/diag.h"
#include "storage/flash_device.h"
#include <cstddef>
#include <cstdint>

static constexpr size_t    KV_MAX_ENTRIES  = 64u;
static constexpr size_t    KV_MAX_KEY_LEN  = 48u;
static constexpr uint16_t  KV_MAX_VAL_LEN  = 512u;

static constexpr uint8_t   KV_TYPE_LIVE  = 0x01u;
static constexpr uint8_t   KV_TYPE_TOMB  = 0x02u;
static constexpr uint8_t   KV_TYPE_ERASED = 0xFFu;  // unwritten flash

#pragma pack(push, 1)
struct KvRecordHdr {
    uint8_t  type;      // KV_TYPE_LIVE or KV_TYPE_TOMB
    uint8_t  key_len;   // 1..KV_MAX_KEY_LEN
    uint16_t val_len;   // 0..KV_MAX_VAL_LEN (0 for tombstones)
    uint32_t crc32;     // CRC32 over {type, key_len, val_len_le, key[], val[]}
};
#pragma pack(pop)
static_assert(sizeof(KvRecordHdr) == 8, "KvRecordHdr must be 8 bytes");

class KvStore {
public:
    // Scan `part_size` bytes starting at `part_ofs` in `dev`, build index.
    DiagStatus init(FlashDevice& dev, uint32_t part_ofs, uint32_t part_size);

    bool initialized() const { return initialized_; }

    // Look up `key` and copy its value into `val_out[0..max_val-1]`.
    // Sets *val_len_out to the actual value length (even on buffer-too-small error).
    // Returns STORAGE_NOT_FOUND if key is absent, STORAGE_IO_ERROR if key is
    // invalid or val_out is too small (needed size reported via *val_len_out).
    DiagStatus get(const char* key, uint8_t* val_out,
                   uint16_t* val_len_out, uint16_t max_val) const;

    // Append a LIVE record for `key`=`val[val_len]`.  Overwrites a previous
    // value by appending (old record stays in flash but is removed from index).
    DiagStatus put(const char* key,
                   const uint8_t* val, uint16_t val_len);

    // Append a TOMB record for `key`, removing it from the index.
    // Returns success even if the key does not exist.
    DiagStatus del(const char* key);

    bool     contains(const char* key) const;
    size_t   live_count() const { return live_count_; }
    uint32_t bytes_used() const { return write_ptr_; }
    uint32_t bytes_free() const;

    // Delete all live keys whose name begins with the given prefix.
    // Returns success even if no keys match; returns the first error if any
    // individual deletion fails (all matching keys are still attempted).
    DiagStatus del_prefix(const char* prefix);

private:
    struct IndexEntry {
        char     key[KV_MAX_KEY_LEN];
        uint8_t  key_len;
        uint16_t val_len;
        uint32_t record_ofs;  // offset of this record within the partition
        bool     used;
    };

    FlashDevice* dev_        = nullptr;
    uint32_t     part_ofs_   = 0;
    uint32_t     part_size_  = 0;
    uint32_t     write_ptr_  = 0;
    bool         initialized_= false;
    size_t       live_count_ = 0;

    IndexEntry   index_[KV_MAX_ENTRIES];

    // Internal helpers.
    IndexEntry* find_entry(const char* key, uint8_t key_len);
    const IndexEntry* find_entry(const char* key, uint8_t key_len) const;
    DiagStatus append_record(uint8_t type, const char* key,
                             const uint8_t* val, uint16_t val_len);
};
