#pragma once
// append_log.h — Append-only event log for EVENT_LOG partition.
//
// Records are written sequentially.  Each record has a monotonic sequence
// number and a CRC32.  Corruption at the tail (torn write) is detected and
// silently ignored; all prior records remain readable.
//
// Thread safety: NOT thread-safe.

#include "diag/diag.h"
#include "storage/flash_device.h"
#include <cstddef>
#include <cstdint>

// Application-defined record types.
static constexpr uint8_t ALOG_TYPE_BOOT    = 0x01u;  // written at every boot
static constexpr uint8_t ALOG_TYPE_INSTALL = 0x02u;  // written on collection install (Stage 7+)
static constexpr uint8_t ALOG_TYPE_ERASED  = 0xFFu;  // unwritten flash (end marker)

static constexpr uint16_t ALOG_MAX_PAYLOAD = 256u;   // maximum payload per record

#pragma pack(push, 1)
struct AppendLogHdr {
    uint8_t  type;      // ALOG_TYPE_*
    uint8_t  reserved;  // MUST be 0
    uint16_t len;       // payload length (0..ALOG_MAX_PAYLOAD)
    uint32_t seq;       // monotonic, starts at 1, increments per record
    uint32_t crc32;     // CRC32 over {type, reserved, len_le, seq_le, data[]}
};
#pragma pack(pop)
static_assert(sizeof(AppendLogHdr) == 12, "AppendLogHdr must be 12 bytes");

// Boot record payload — stored in the data area of every ALOG_TYPE_BOOT record.
#pragma pack(push, 1)
struct BootRecord {
    char     build_id[32];  // FW_BUILD_ID, NUL-padded
    uint32_t boot_seq;      // mirrors the AppendLogHdr.seq of this record
};
#pragma pack(pop)
static_assert(sizeof(BootRecord) == 36, "BootRecord must be 36 bytes");

class AppendLog {
public:
    DiagStatus init(FlashDevice& dev, uint32_t part_ofs, uint32_t part_size);

    bool initialized() const { return initialized_; }

    // Append one record.  Returns STORAGE_FULL if partition is exhausted.
    DiagStatus append(uint8_t type, const uint8_t* data, uint16_t len);

    // Iterate all valid records in sequence order.
    // Callback receives (type, seq, data, len, ctx).
    // Return false from the callback to stop early.
    using IterCb = bool (*)(uint8_t type, uint32_t seq,
                            const uint8_t* data, uint16_t len, void* ctx);
    DiagStatus iterate(IterCb cb, void* ctx) const;

    uint32_t record_count() const { return record_count_; }
    uint32_t next_seq()     const { return next_seq_; }

private:
    FlashDevice* dev_          = nullptr;
    uint32_t     part_ofs_     = 0;
    uint32_t     part_size_    = 0;
    uint32_t     write_ptr_    = 0;
    uint32_t     next_seq_     = 1;
    uint32_t     record_count_ = 0;
    bool         initialized_  = false;

    static uint32_t compute_crc(uint8_t type, uint16_t len, uint32_t seq,
                                 const uint8_t* data);
};
