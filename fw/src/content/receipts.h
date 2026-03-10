#pragma once
// receipts.h — Install receipt logging to EVENT_LOG.
// See spec.md §11.1 "Install receipts (audit trail)".
//
// An Install Receipt records each install decision (installed/rejected)
// including the collection identity, result, and a digest.
// Receipts are diagnostic data and MUST NOT be required to boot.

#include "diag/diag.h"
#include "content/collection_format.h"
#include "storage/append_log.h"
#include <cstdint>

// Payload written to EVENT_LOG as ALOG_TYPE_INSTALL.
#pragma pack(push, 1)
struct InstallReceiptData {
    char     collection_id[COL_ID_MAX];   // 64 bytes — from manifest.collection_id
    char     version[COL_VERSION_MAX];    // 32 bytes — from manifest.version
    char     publisher_id[PUB_ID_MAX];    // 64 bytes — from manifest.publisher_id
    uint16_t diag_code;                   //  2 bytes — 0 on success, DiagCode on reject
    uint8_t  installed;                   //  1 byte  — 1=installed, 0=rejected
    uint8_t  manifest_sha256[32];         // 32 bytes — SHA-256 of manifest.json bytes
    uint8_t  _pad[1];                     //  1 byte  — alignment
    // Total: 64+32+64+2+1+32+1 = 196 bytes — fits in ALOG_MAX_PAYLOAD (256)
};
#pragma pack(pop)
static_assert(sizeof(InstallReceiptData) == 196, "InstallReceiptData layout changed");
static_assert(sizeof(InstallReceiptData) <= ALOG_MAX_PAYLOAD,
              "InstallReceiptData must fit in ALOG_MAX_PAYLOAD");

// Append one install receipt to the event log.  The log's append() failure
// is silently ignored because receipts must not affect boot (spec §11.1).
void append_install_receipt(AppendLog& log, const InstallReceiptData& rec);
