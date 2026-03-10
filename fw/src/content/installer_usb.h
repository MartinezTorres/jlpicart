#pragma once
// installer_usb.h — Collection installer: abstract file reader + install engine.
//
// The Installer is source-agnostic: it reads bundle files through the
// InstallReader interface so the same logic can be tested with in-memory
// buffers and later wired to the USB host filesystem.
//
// USB-specific scanning (/JLPICART/INSTALL/*/) is stubbed pending USB host
// integration (see TODO(usb-host) in main.cc).
//
// See spec.md §11.1 (Update, initialization, and provisioning workflow contract).

#include "diag/diag.h"
#include "content/collection_format.h"
#include "crypto/sha256.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
#include "spine/policy_store.h"
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// InstallReader — abstract file source for a single Install Intent directory
// ---------------------------------------------------------------------------

class InstallReader {
public:
    virtual ~InstallReader() = default;

    // Read up to max_len bytes from a file within the bundle.
    // Writes actual byte count to *out_len.  Returns STORAGE_IO_ERROR if the
    // file is not found or if max_len is too small to hold the file.
    virtual DiagStatus read_file(const char* path, uint8_t* buf,
                                  size_t max_len, size_t* out_len) = 0;

    // Compute SHA-256 of the full file contents using streaming reads.
    // Implementations may read in chunks for large payload files.
    virtual DiagStatus hash_file(const char* path,
                                  uint8_t digest[SHA256_DIGEST_SIZE]) = 0;

    virtual bool file_exists(const char* path) = 0;
};

// ---------------------------------------------------------------------------
// InstallResult — outcome of a single install attempt
// ---------------------------------------------------------------------------

struct InstallResult {
    bool     installed;
    DiagCode reason;                    // DiagCode::OK if installed
    char     collection_id[COL_ID_MAX];
    char     version[COL_VERSION_MAX];
    uint8_t  manifest_sha256[32];       // SHA-256 of manifest.json bytes
};

// ---------------------------------------------------------------------------
// Installer — source-agnostic install engine
// ---------------------------------------------------------------------------

class Installer {
public:
    // Run one install attempt from the given reader.
    //
    // Atomicity guarantee (spec §11.1):
    //   The collection is visible as "active" only after the KvStore commit
    //   step.  If power is lost before the commit, col.state remains "pending"
    //   and no active collection is reported — the install is abandoned, not
    //   half-visible.  (Two-slot preservation of the previous collection is a
    //   Stage 7+ improvement; for now, only one collection is stored.)
    //
    // Receipt:
    //   An InstallReceiptData record is appended to event_log regardless of
    //   success or failure.  A receipt write failure is silently ignored
    //   (spec §11.1: "receipts MUST NOT be required to boot").
    DiagStatus run(InstallReader& reader,
                   KvStore& kv,
                   AppendLog& event_log,
                   const PolicyStore& policy,
                   InstallResult& result_out);
};
