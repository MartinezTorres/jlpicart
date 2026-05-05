#pragma once
// installer.h — Collection installer: abstract file reader + install engine.
//
// The Installer is source-agnostic: it reads bundle files through the
// InstallReader interface so the same logic can be tested with in-memory
// buffers and later wired to the USB host filesystem.
//
// Atomicity model (FAT):
//   1. Write all files into 1:/collections/.installing/
//   2. Write 1:/collections/active.txt with the collection_id (commit point)
//
//   Power loss before step 2 → active.txt absent; no active collection.

#include "diag/diag.h"
#include "content/collection_format.h"
#include "crypto/sha256.h"
#include "spine/policy_store.h"
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// InstallReader — abstract file source for a single Install Intent directory
// ---------------------------------------------------------------------------

class InstallReader {
public:
    virtual ~InstallReader() = default;

    virtual DiagStatus read_file(const char* path, uint8_t* buf,
                                  size_t max_len, size_t* out_len) = 0;

    virtual DiagStatus hash_file(const char* path,
                                  uint8_t digest[SHA256_DIGEST_SIZE]) = 0;

    virtual bool file_exists(const char* path) = 0;

    // Copy a payload file to a destination FAT path.
    // out_size receives the byte count written.
    // Default: no-op (returns success with *out_size=0).
    virtual DiagStatus copy_to_fat(const char* src_path, const char* dst_path,
                                    size_t* out_size) {
        (void)src_path; (void)dst_path;
        *out_size = 0;
        return DiagStatus::success();
    }
};

// ---------------------------------------------------------------------------
// InstallResult
// ---------------------------------------------------------------------------

struct InstallResult {
    bool     installed;
    DiagCode reason;
    char     collection_id[COL_ID_MAX];
    char     version[COL_VERSION_MAX];
    uint8_t  manifest_sha256[32];
};

// ---------------------------------------------------------------------------
// Installer
// ---------------------------------------------------------------------------

class Installer {
public:
    // Run one install attempt.
    // Writes collection metadata and payload ROM files to FAT.
    // Returns success only when the commit point (active.txt) is written.
    DiagStatus run(InstallReader& reader,
                   const PolicyStore& policy,
                   InstallResult& result_out);
};

// ---------------------------------------------------------------------------
// Bundle signature verification
// ---------------------------------------------------------------------------

// Load the publisher's ed25519 public key from 1:/system/pub_anchor.bin.
// Returns true if the file exists and is exactly 32 bytes.
bool policy_get_publisher_anchor(uint8_t anchor_out[32]);

// Verify the ed25519 signature in env against pubkey[32].
// manifest_sha256 is the SHA-256 of the manifest.json bytes (the signed message).
bool bundle_sig_verify(const BundleSigEnvelope& env,
                       const uint8_t pubkey[32],
                       const uint8_t manifest_sha256[32]);
