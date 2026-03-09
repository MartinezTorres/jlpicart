// append_log.cc — Append-only event log implementation.

#include "storage/append_log.h"
#include "crypto/crc32.h"
#include <cstring>

// ---------------------------------------------------------------------------
// CRC helper: over {type(1), reserved(1)=0, len_lo(1), len_hi(1),
//                   seq_b0..b3, data[len]}
// ---------------------------------------------------------------------------

uint32_t AppendLog::compute_crc(uint8_t type, uint16_t len, uint32_t seq,
                                 const uint8_t* data)
{
    uint8_t header_bytes[8];
    header_bytes[0] = type;
    header_bytes[1] = 0u;
    header_bytes[2] = static_cast<uint8_t>(len & 0xFFu);
    header_bytes[3] = static_cast<uint8_t>(len >> 8u);
    header_bytes[4] = static_cast<uint8_t>(seq & 0xFFu);
    header_bytes[5] = static_cast<uint8_t>((seq >> 8u)  & 0xFFu);
    header_bytes[6] = static_cast<uint8_t>((seq >> 16u) & 0xFFu);
    header_bytes[7] = static_cast<uint8_t>((seq >> 24u) & 0xFFu);

    uint32_t crc = 0xFFFFFFFFu;
    crc = crc32_update(crc, header_bytes, sizeof(header_bytes));
    if (data && len > 0) crc = crc32_update(crc, data, len);
    return crc ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// init — scan partition and establish write_ptr + next_seq
// ---------------------------------------------------------------------------

DiagStatus AppendLog::init(FlashDevice& dev, uint32_t part_ofs, uint32_t part_size)
{
    dev_          = &dev;
    part_ofs_     = part_ofs;
    part_size_    = part_size;
    write_ptr_    = 0;
    next_seq_     = 1;
    record_count_ = 0;
    initialized_  = false;

    uint32_t ptr = 0;
    while (ptr + sizeof(AppendLogHdr) <= part_size_) {
        AppendLogHdr hdr;
        DiagStatus s = dev_->read(part_ofs_ + ptr,
                                   reinterpret_cast<uint8_t*>(&hdr),
                                   sizeof(hdr));
        if (!s.ok()) return s;

        if (hdr.type == ALOG_TYPE_ERASED) break;  // end of written log

        if (hdr.reserved != 0 || hdr.len > ALOG_MAX_PAYLOAD) {
            // Corrupted tail — stop.
            break;
        }
        if (ptr + sizeof(AppendLogHdr) + hdr.len > part_size_) break;

        // Read payload for CRC check.
        uint8_t payload[ALOG_MAX_PAYLOAD];
        if (hdr.len > 0) {
            s = dev_->read(part_ofs_ + ptr + sizeof(AppendLogHdr),
                           payload, hdr.len);
            if (!s.ok()) return s;
        }

        uint32_t expected = compute_crc(hdr.type, hdr.len, hdr.seq,
                                         hdr.len > 0 ? payload : nullptr);
        if (expected != hdr.crc32) {
            // CRC mismatch — torn write at tail: stop.
            break;
        }

        ++record_count_;
        if (hdr.seq >= next_seq_) next_seq_ = hdr.seq + 1u;
        ptr += static_cast<uint32_t>(sizeof(AppendLogHdr)) + hdr.len;
    }

    write_ptr_   = ptr;
    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// append
// ---------------------------------------------------------------------------

DiagStatus AppendLog::append(uint8_t type, const uint8_t* data, uint16_t len)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (len > ALOG_MAX_PAYLOAD)  return DiagStatus::error(DiagCode::STORAGE_FULL);

    uint32_t total = sizeof(AppendLogHdr) + len;
    if (write_ptr_ + total > part_size_) {
        return DiagStatus::error(DiagCode::STORAGE_FULL);
    }

    AppendLogHdr hdr;
    hdr.type     = type;
    hdr.reserved = 0;
    hdr.len      = len;
    hdr.seq      = next_seq_;
    hdr.crc32    = compute_crc(type, len, next_seq_, len > 0 ? data : nullptr);

    // Write header first, then payload, as two writes.
    // Even if power is lost between them, the CRC will not match on next init.
    DiagStatus s = dev_->write(part_ofs_ + write_ptr_,
                                reinterpret_cast<const uint8_t*>(&hdr),
                                sizeof(hdr));
    if (!s.ok()) return s;
    if (len > 0) {
        s = dev_->write(part_ofs_ + write_ptr_ + sizeof(hdr), data, len);
        if (!s.ok()) return s;
    }

    write_ptr_    += total;
    ++next_seq_;
    ++record_count_;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// iterate
// ---------------------------------------------------------------------------

DiagStatus AppendLog::iterate(IterCb cb, void* ctx) const
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    uint32_t ptr = 0;
    while (ptr + sizeof(AppendLogHdr) <= part_size_) {
        AppendLogHdr hdr;
        DiagStatus s = dev_->read(part_ofs_ + ptr,
                                   reinterpret_cast<uint8_t*>(&hdr),
                                   sizeof(hdr));
        if (!s.ok()) return s;

        if (hdr.type == ALOG_TYPE_ERASED) break;
        if (hdr.reserved != 0 || hdr.len > ALOG_MAX_PAYLOAD) break;
        if (ptr + sizeof(AppendLogHdr) + hdr.len > part_size_) break;

        uint8_t payload[ALOG_MAX_PAYLOAD];
        if (hdr.len > 0) {
            s = dev_->read(part_ofs_ + ptr + sizeof(AppendLogHdr),
                           payload, hdr.len);
            if (!s.ok()) return s;
        }

        uint32_t expected = compute_crc(hdr.type, hdr.len, hdr.seq,
                                         hdr.len > 0 ? payload : nullptr);
        if (expected != hdr.crc32) break;  // corrupted tail

        if (cb) {
            bool cont = cb(hdr.type, hdr.seq,
                           hdr.len > 0 ? payload : nullptr, hdr.len, ctx);
            if (!cont) break;
        }

        ptr += static_cast<uint32_t>(sizeof(AppendLogHdr)) + hdr.len;
    }

    return DiagStatus::success();
}
