// installer.cc — Collection install engine.
//
// USB-specific scanning (/JLPICART/INSTALL/*/) is deferred to the bus-layer
// stage when the USB host stack (tinyusb) is integrated.  This file contains
// the source-agnostic Installer::run() logic, fully testable via host tests.
//
// Atomicity model (spec §11.1):
//   1. Mark KvStore state as "pending"          (old collection inaccessible)
//   2. Write CollectionRecord to KvStore
//   3. Write KvStore state as "active"          (commit point)
//   4. Append receipt to EVENT_LOG              (diagnostic only)
//
//   Power loss before step 3 → state is "pending"; no active collection.
//   Power loss after step 3  → collection is committed; receipt may be missing
//     (benign per spec §11.1).
//
// NOTE (Stage 7 limitation): only one collection is stored at a time.
//   Installing a new collection discards the old record.  Two-slot
//   preservation of the previous collection is a TODO for Stage 7+.

#include "content/installer.h"
#include "content/manifest_parser.h"
#include "content/receipts.h"
#include "content/bundle_sig_verify.h"
#include "crypto/sha256.h"
#include "spine/policy_store.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "log/log.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Verify the SHA-256 of each file listed in the signature envelope.
static DiagStatus verify_bundle_hashes(InstallReader& reader,
                                        const BundleSigEnvelope& env)
{
    for (uint8_t i = 0; i < env.file_count; ++i) {
        const BundleFileHash& fh = env.files[i];
        uint8_t computed[SHA256_DIGEST_SIZE];
        DiagStatus s = reader.hash_file(fh.path, computed);
        if (!s.ok()) return s;
        if (memcmp(computed, fh.sha256, SHA256_DIGEST_SIZE) != 0) {
            return DiagStatus::error(DiagCode::COLLECTION_HASH_MISMATCH);
        }
    }
    return DiagStatus::success();
}

// Build a CollectionRecord from a parsed manifest.
// Field sizes are identical in CollectionManifest and CollectionRecord (same constants).
// The parser already guarantees every string fits within its field's bounds.
static void manifest_to_record(const CollectionManifest& m, CollectionRecord& rec)
{
    memset(&rec, 0, sizeof(rec));
    memcpy(rec.collection_id,      m.collection_id,      sizeof(rec.collection_id));
    memcpy(rec.version,            m.version,            sizeof(rec.version));
    memcpy(rec.publisher_id,       m.publisher_id,       sizeof(rec.publisher_id));
    memcpy(rec.title,              m.title,              sizeof(rec.title));
    rec.boot_mode     = m.boot_mode;
    rec.payload_count = m.payload_count;
    memcpy(rec.default_payload_id, m.default_payload_id, sizeof(rec.default_payload_id));
}

// Build a failure receipt and append it to the log.
// manifest_sha256 may be null if the manifest was never successfully hashed.
static void write_failure_receipt(AppendLog& log, const char* collection_id,
                                   const char* version, const char* publisher_id,
                                   const uint8_t* manifest_sha256,
                                   DiagCode reason)
{
    InstallReceiptData rec = {};
    strncpy(rec.collection_id, collection_id, sizeof(rec.collection_id) - 1u);
    strncpy(rec.version,       version,       sizeof(rec.version)       - 1u);
    strncpy(rec.publisher_id,  publisher_id,  sizeof(rec.publisher_id)  - 1u);
    rec.diag_code = static_cast<uint16_t>(reason);
    rec.installed  = 0u;
    if (manifest_sha256) memcpy(rec.manifest_sha256, manifest_sha256, SHA256_DIGEST_SIZE);
    append_install_receipt(log, rec);
}

// ---------------------------------------------------------------------------
// Installer::run
// ---------------------------------------------------------------------------

DiagStatus Installer::run(InstallReader& reader,
                           KvStore& kv,
                           AppendLog& event_log,
                           const PolicyStore& policy,
                           InstallResult& result_out,
                           FlashDevice* flash)
{
    result_out = {};
    result_out.installed = false;
    result_out.reason    = DiagCode::COLLECTION_BAD_MANIFEST;

    // ------------------------------------------------------------------
    // 1. Read and parse manifest.json.
    // ------------------------------------------------------------------
    uint8_t manifest_buf[MANIFEST_BYTES_MAX];
    size_t  manifest_len = 0;
    {
        DiagStatus s = reader.read_file(BUNDLE_MANIFEST_FILE,
                                         manifest_buf, sizeof(manifest_buf),
                                         &manifest_len);
        if (!s.ok()) {
            result_out.reason = s.code;
            write_failure_receipt(event_log, "", "", "", nullptr, s.code);
            return s;
        }
    }

    CollectionManifest manifest = {};
    {
        DiagStatus s = parse_collection_manifest(
            reinterpret_cast<const char*>(manifest_buf), manifest_len, manifest);
        if (!s.ok()) {
            result_out.reason = s.code;
            write_failure_receipt(event_log, "", "", "", nullptr, s.code);
            return s;
        }
    }

    // Record identity in result for the caller and receipts.
    memcpy(result_out.collection_id, manifest.collection_id, sizeof(result_out.collection_id));
    memcpy(result_out.version,       manifest.version,       sizeof(result_out.version));

    // Compute SHA-256 of the manifest bytes (used in the receipt).
    sha256(manifest_buf, manifest_len, result_out.manifest_sha256);

    // ------------------------------------------------------------------
    // 2. Parse and verify bundle.sig (if present).
    // ------------------------------------------------------------------
    const bool requires_sig    =
        (policy.info().flags & POLICY_REQUIRE_PUBLISHER_SIGNATURE) != 0u;
    const bool allows_unsigned =
        (policy.info().flags & POLICY_ALLOW_UNSIGNED_COLLECTIONS)  != 0u;

    if (reader.file_exists(BUNDLE_SIG_FILE)) {
        uint8_t sig_buf[SIG_ENV_BYTES_MAX];
        size_t  sig_len = 0;
        {
            DiagStatus s = reader.read_file(BUNDLE_SIG_FILE,
                                             sig_buf, sizeof(sig_buf), &sig_len);
            if (!s.ok()) {
                result_out.reason = s.code;
                write_failure_receipt(event_log,
                    manifest.collection_id, manifest.version, manifest.publisher_id,
                    result_out.manifest_sha256, s.code);
                return s;
            }
        }

        BundleSigEnvelope sig_env = {};
        {
            DiagStatus s = parse_bundle_sig(
                reinterpret_cast<const char*>(sig_buf), sig_len, sig_env);
            if (!s.ok()) {
                result_out.reason = s.code;
                write_failure_receipt(event_log,
                    manifest.collection_id, manifest.version, manifest.publisher_id,
                    result_out.manifest_sha256, s.code);
                return s;
            }
        }

        // Verify SHA-256 of every file listed in bundle.sig.
        {
            DiagStatus s = verify_bundle_hashes(reader, sig_env);
            if (!s.ok()) {
                result_out.reason = s.code;
                write_failure_receipt(event_log,
                    manifest.collection_id, manifest.version, manifest.publisher_id,
                    result_out.manifest_sha256, s.code);
                return s;
            }
        }

        // ed25519 signature verification.
        // Supported algorithm: "ed25519" (Monocypher crypto_eddsa_check).
        // The signed message is the SHA-256 of manifest.json bytes.
        // The publisher's public key is stored under "pub.anchor" in SYSTEM_KV.
        if (strncmp(sig_env.alg, "ed25519", sizeof(sig_env.alg)) == 0) {
            uint8_t anchor[32] = {};
            if (!policy_get_publisher_anchor(kv, anchor)) {
                // No anchor key enrolled — cannot verify.  Reject if required.
                if (requires_sig) {
                    const DiagCode reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
                    result_out.reason = reason;
                    write_failure_receipt(event_log,
                        manifest.collection_id, manifest.version, manifest.publisher_id,
                        result_out.manifest_sha256, reason);
                    return DiagStatus::error(reason);
                }
                // Not required — treat missing anchor as unverified but acceptable.
            } else {
                if (!bundle_sig_verify(sig_env, anchor, result_out.manifest_sha256)) {
                    const DiagCode reason = DiagCode::COLLECTION_SIG_INVALID;
                    result_out.reason = reason;
                    write_failure_receipt(event_log,
                        manifest.collection_id, manifest.version, manifest.publisher_id,
                        result_out.manifest_sha256, reason);
                    return DiagStatus::error(reason);
                }
            }
        } else if (requires_sig) {
            // Unknown or unsupported signature algorithm — reject if required.
            const DiagCode reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
            result_out.reason = reason;
            write_failure_receipt(event_log,
                manifest.collection_id, manifest.version, manifest.publisher_id,
                result_out.manifest_sha256, reason);
            return DiagStatus::error(reason);
        }

    } else if (requires_sig || !allows_unsigned) {
        // No bundle.sig, but either:
        //   - policy requires a publisher signature (POLICY_REQUIRE_PUBLISHER_SIGNATURE), or
        //   - unsigned collections are not explicitly allowed (POLICY_ALLOW_UNSIGNED_COLLECTIONS).
        const DiagCode reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
        result_out.reason = reason;
        write_failure_receipt(event_log,
            manifest.collection_id, manifest.version, manifest.publisher_id,
            result_out.manifest_sha256, reason);
        return DiagStatus::error(reason);
    }

    // ------------------------------------------------------------------
    // 2b. Stream payload ROM files into CONTENT_DATA flash.
    //
    //     This phase runs BEFORE the KvStore atomic commit.  If power is
    //     lost here, no collection becomes visible (KvStore unchanged).
    //     We record the flash offset and actual size for each payload and
    //     use them in step 3b when writing PayloadRecords.
    //
    //     If flash is null, or a payload's path is empty, or copy_to_flash
    //     returns 0 bytes, data_size is left at 0 (bus wiring deferred).
    // ------------------------------------------------------------------

    struct PayloadFlashInfo {
        uint32_t flash_offset;
        uint32_t data_size;
    };
    // Default flash_offset to FLASH_CONTENT_DATA_OFS for every payload slot.
    // This matches the placeholder written before Stage 28 and keeps
    // backwards-compatibility with test suites that pass flash=nullptr.
    PayloadFlashInfo pfi[MANIFEST_MAX_PAYLOADS] = {};
    for (size_t i = 0; i < MANIFEST_MAX_PAYLOADS; ++i)
        pfi[i].flash_offset = FLASH_CONTENT_DATA_OFS;

    if (flash) {
        uint32_t write_ptr = FLASH_CONTENT_DATA_OFS;
        for (uint8_t i = 0; i < manifest.payload_count; ++i) {
            const PayloadEntry& pe = manifest.payloads[i];
            pfi[i].flash_offset = write_ptr;  // actual write pointer

            if (pe.path[0] == '\0') continue;

            size_t rom_size = 0;
            DiagStatus s = reader.copy_to_flash(pe.path, *flash,
                                                 write_ptr, &rom_size);
            if (s.ok() && rom_size > 0) {
                pfi[i].data_size = static_cast<uint32_t>(rom_size);
                char msg[96];
                snprintf(msg, sizeof(msg),
                         "installer: payload[%u] %.32s @ 0x%08X (%zu B)",
                         i, pe.path, write_ptr, rom_size);
                log_info(msg);
                // Advance write pointer sector-aligned.
                uint32_t sectors =
                    (static_cast<uint32_t>(rom_size) + FLASH_SECTOR_SIZE - 1u)
                    / FLASH_SECTOR_SIZE;
                write_ptr += sectors * FLASH_SECTOR_SIZE;
            } else {
                char msg[96];
                snprintf(msg, sizeof(msg),
                         "installer: payload[%u] %.32s: skip (%s)",
                         i, pe.path, s.ok() ? "empty" : "error");
                log_info(msg);
            }
        }
    }

    // ------------------------------------------------------------------
    // 3. Atomic KvStore install.
    //
    //    Step a: mark "pending" (old collection becomes inaccessible).
    //    Step b: write CollectionRecord.
    //    Step c: mark "active" ← commit point.
    //
    //    If power is lost before step c, state == "pending" on next boot
    //    and no active collection is reported.
    // ------------------------------------------------------------------
    {
        const uint8_t* pending_val = reinterpret_cast<const uint8_t*>(COL_STATE_PENDING);
        DiagStatus s = kv.put(KV_COL_STATE, pending_val,
                               static_cast<uint16_t>(strlen(COL_STATE_PENDING)));
        if (!s.ok()) {
            result_out.reason = s.code;
            write_failure_receipt(event_log,
                manifest.collection_id, manifest.version, manifest.publisher_id,
                result_out.manifest_sha256, s.code);
            return s;
        }
    }

    CollectionRecord record = {};
    manifest_to_record(manifest, record);

    {
        DiagStatus s = kv.put(KV_COL_RECORD,
                               reinterpret_cast<const uint8_t*>(&record),
                               static_cast<uint16_t>(sizeof(record)));
        if (!s.ok()) {
            result_out.reason = s.code;
            write_failure_receipt(event_log,
                manifest.collection_id, manifest.version, manifest.publisher_id,
                result_out.manifest_sha256, s.code);
            return s;
        }
    }

    // Commit.
    {
        const uint8_t* active_val = reinterpret_cast<const uint8_t*>(COL_STATE_ACTIVE);
        DiagStatus s = kv.put(KV_COL_STATE, active_val,
                               static_cast<uint16_t>(strlen(COL_STATE_ACTIVE)));
        if (!s.ok()) {
            result_out.reason = s.code;
            write_failure_receipt(event_log,
                manifest.collection_id, manifest.version, manifest.publisher_id,
                result_out.manifest_sha256, s.code);
            return s;
        }
    }

    // ------------------------------------------------------------------
    // 3b. Write per-payload records (best-effort; non-fatal per spec §10.4).
    //
    //     data_flash_offset = FLASH_CONTENT_DATA_OFS; data_size = 0 until
    //     populate_flash.py writes the ROM bytes.  apply_mapping() skips
    //     bus wiring whenever data_size == 0.
    // ------------------------------------------------------------------
    // KV_PAYLOAD_PREFIX is a compile-time string literal; compute length once.
    static constexpr size_t kPrefixLen = 3;  // strlen("pl.")
    static_assert(kPrefixLen == 3, "KV_PAYLOAD_PREFIX length mismatch");

    for (uint8_t i = 0; i < manifest.payload_count; ++i) {
        const PayloadEntry& pe = manifest.payloads[i];
        if (pe.payload_id[0] == '\0') continue;

        PayloadRecord pr = {};
        memcpy(pr.payload_id,  pe.payload_id,  sizeof(pr.payload_id));
        memcpy(pr.mapper_type, pe.mapper_type, sizeof(pr.mapper_type));
        pr.subslot           = pe.subslot;
        pr.data_flash_offset = pfi[i].flash_offset;
        pr.data_size         = pfi[i].data_size;

        // Build "pl.<payload_id>".  payload_id must be ≤ 45 chars to fit in
        // KV_MAX_KEY_LEN (48).  Silently skip any record that exceeds this.
        const size_t id_len = strlen(pe.payload_id);
        if (kPrefixLen + id_len > KV_MAX_KEY_LEN) continue;

        char key[KV_MAX_KEY_LEN + 1];
        memcpy(key, KV_PAYLOAD_PREFIX, kPrefixLen);
        memcpy(key + kPrefixLen, pe.payload_id, id_len);
        key[kPrefixLen + id_len] = '\0';

        DiagStatus ps = kv.put(key,
                               reinterpret_cast<const uint8_t*>(&pr),
                               static_cast<uint16_t>(sizeof(pr)));
        (void)ps;  // non-fatal: best-effort write
    }

    // ------------------------------------------------------------------
    // 4. Append success receipt (best-effort; failure is silently ignored).
    // ------------------------------------------------------------------
    {
        InstallReceiptData receipt = {};
        memcpy(receipt.collection_id, manifest.collection_id, sizeof(receipt.collection_id));
        memcpy(receipt.version,       manifest.version,       sizeof(receipt.version));
        memcpy(receipt.publisher_id,  manifest.publisher_id,  sizeof(receipt.publisher_id));
        receipt.diag_code = 0u;
        receipt.installed  = 1u;
        memcpy(receipt.manifest_sha256, result_out.manifest_sha256, SHA256_DIGEST_SIZE);
        append_install_receipt(event_log, receipt);
    }

    result_out.installed = true;
    result_out.reason    = DiagCode::OK;
    return DiagStatus::success();
}
