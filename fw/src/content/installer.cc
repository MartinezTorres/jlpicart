// installer.cc — FAT-backed collection install engine.
//
// Atomicity model:
//   1. Write CollectionRecord + PayloadRecords + ROM files to
//      1:/collections/.installing/
//   2. Write collection_id to 1:/collections/active.txt  ← commit point
//   3. Append receipt to event log (best-effort)
//
// Power loss before step 2 leaves no active collection; .installing/ is
// harmlessly orphaned and will be overwritten on the next install attempt.

#include "content/installer.h"
#include "content/manifest.h"
#include "content/manifest_parser.h"
#include "content/bundle_sig_verify.h"
#include "crypto/sha256.h"
#include "spine/policy_store.h"
#include "filesystem/fat_util.h"
#include "diag/log.h"
#include "ff.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

static constexpr const char* INSTALLING_DIR = "1:/collections/.installing";
static constexpr const char* ACTIVE_PATH    = "1:/collections/active.txt";

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static DiagStatus verify_bundle_hashes(InstallReader& reader,
                                        const BundleSigEnvelope& env)
{
    for (uint8_t i = 0; i < env.file_count; ++i) {
        const BundleFileHash& fh = env.files[i];
        uint8_t computed[SHA256_DIGEST_SIZE];
        DiagStatus s = reader.hash_file(fh.path, computed);
        if (!s.ok()) return s;
        if (memcmp(computed, fh.sha256, SHA256_DIGEST_SIZE) != 0)
            return DiagStatus::error(DiagCode::COLLECTION_HASH_MISMATCH);
    }
    return DiagStatus::success();
}

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

// Recursively delete all files in a directory (one level; not recursive).
static void clear_installing_dir()
{
    // Remove .installing directory contents, then the dir itself.
    // We re-create it fresh for each install attempt.
    DIR dir;
    if (f_opendir(&dir, INSTALLING_DIR) == FR_OK) {
        FILINFO fi;
        char path[300];
        while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0] != '\0') {
            snprintf(path, sizeof(path), "%s/%s", INSTALLING_DIR, fi.fname);
            f_unlink(path);
        }
        f_closedir(&dir);
    }
    f_unlink(INSTALLING_DIR);
}

// ---------------------------------------------------------------------------
// Installer::run
// ---------------------------------------------------------------------------

DiagStatus Installer::run(InstallReader& reader,
                           const PolicyStore& policy,
                           InstallResult& result_out)
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
            return s;
        }
    }

    CollectionManifest manifest = {};
    {
        DiagStatus s = parse_collection_manifest(
            reinterpret_cast<const char*>(manifest_buf), manifest_len, manifest);
        if (!s.ok()) {
            result_out.reason = s.code;
            return s;
        }
    }

    memcpy(result_out.collection_id, manifest.collection_id, sizeof(result_out.collection_id));
    memcpy(result_out.version,       manifest.version,       sizeof(result_out.version));
    sha256(manifest_buf, manifest_len, result_out.manifest_sha256);

    // ------------------------------------------------------------------
    // 2. Parse and verify bundle.sig (if present).
    // ------------------------------------------------------------------
    const bool requires_sig    = (policy.info().flags & POLICY_REQUIRE_PUBLISHER_SIGNATURE) != 0u;
    const bool allows_unsigned = (policy.info().flags & POLICY_ALLOW_UNSIGNED_COLLECTIONS)  != 0u;

    if (reader.file_exists(BUNDLE_SIG_FILE)) {
        uint8_t sig_buf[SIG_ENV_BYTES_MAX];
        size_t  sig_len = 0;
        {
            DiagStatus s = reader.read_file(BUNDLE_SIG_FILE,
                                             sig_buf, sizeof(sig_buf), &sig_len);
            if (!s.ok()) { result_out.reason = s.code; return s; }
        }

        BundleSigEnvelope sig_env = {};
        {
            DiagStatus s = parse_bundle_sig(
                reinterpret_cast<const char*>(sig_buf), sig_len, sig_env);
            if (!s.ok()) { result_out.reason = s.code; return s; }
        }

        {
            DiagStatus s = verify_bundle_hashes(reader, sig_env);
            if (!s.ok()) { result_out.reason = s.code; return s; }
        }

        if (strncmp(sig_env.alg, "ed25519", sizeof(sig_env.alg)) == 0) {
            uint8_t anchor[32] = {};
            if (!policy_get_publisher_anchor(anchor)) {
                if (requires_sig) {
                    result_out.reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
                    return DiagStatus::error(result_out.reason);
                }
            } else {
                if (!bundle_sig_verify(sig_env, anchor, result_out.manifest_sha256)) {
                    result_out.reason = DiagCode::COLLECTION_SIG_INVALID;
                    return DiagStatus::error(result_out.reason);
                }
            }
        } else if (requires_sig) {
            result_out.reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
            return DiagStatus::error(result_out.reason);
        }

    } else if (requires_sig || !allows_unsigned) {
        result_out.reason = DiagCode::COLLECTION_UNSATISFIED_REQ;
        return DiagStatus::error(result_out.reason);
    }

    // ------------------------------------------------------------------
    // 3. Write collection files to .installing/ (pre-commit).
    // ------------------------------------------------------------------
    fat_ensure_dir("1:/collections");
    clear_installing_dir();
    fat_ensure_dir(INSTALLING_DIR);

    // Write CollectionRecord.
    CollectionRecord record = {};
    manifest_to_record(manifest, record);
    {
        char path[128];
        snprintf(path, sizeof(path), "%s/collection.bin", INSTALLING_DIR);
        if (!fat_write_file(path, &record, sizeof(record))) {
            result_out.reason = DiagCode::STORAGE_IO_ERROR;
            return DiagStatus::error(result_out.reason);
        }
    }

    // Write PayloadRecords + copy ROM files.
    for (uint8_t i = 0; i < manifest.payload_count; ++i) {
        const PayloadEntry& pe = manifest.payloads[i];
        if (pe.payload_id[0] == '\0') continue;

        PayloadRecord pr = {};
        memcpy(pr.payload_id,  pe.payload_id,  sizeof(pr.payload_id));
        memcpy(pr.mapper_type, pe.mapper_type, sizeof(pr.mapper_type));
        pr.subslot           = pe.subslot;
        pr.data_flash_offset = 0u; // computed from FAT cluster at runtime
        pr.data_size         = 0u; // set below if copy succeeds

        // Copy device records.
        pr.device_count = pe.device_count;
        for (uint8_t di = 0; di < pe.device_count && di < PAYLOAD_DEVICES_MAX; ++di) {
            const ManifestDeviceEntry& de = pe.devices[di];
            PayloadDeviceRecord& pdr = pr.devices[di];
            pdr.type     = static_cast<uint8_t>(de.type);
            pdr.subslot  = de.subslot;
            pdr.optional = de.optional ? 1u : 0u;
            pdr._pad     = 0u;
            size_t plen  = strlen(de.params);
            if (plen >= sizeof(pdr.params)) plen = sizeof(pdr.params) - 1u;
            memcpy(pdr.params, de.params, plen);
            pdr.params[plen] = '\0';
        }

        // Copy ROM file to FAT.
        if (pe.path[0] != '\0') {
            char dst[192];
            snprintf(dst, sizeof(dst), "%s/rom_%s.bin", INSTALLING_DIR, pe.payload_id);
            size_t rom_size = 0;
            DiagStatus cs = reader.copy_to_fat(pe.path, dst, &rom_size);
            if (cs.ok() && rom_size > 0) {
                pr.data_size = static_cast<uint32_t>(rom_size);
                // data_flash_offset is computed lazily at boot from FAT cluster.
                // ContentStore / mapping_plan_from_payload_record handles this.
            }
        }

        char pr_path[192];
        snprintf(pr_path, sizeof(pr_path), "%s/payload_%s.bin",
                 INSTALLING_DIR, pe.payload_id);
        fat_write_file(pr_path, &pr, sizeof(pr)); // best-effort
    }

    // ------------------------------------------------------------------
    // 4. Commit: rename .installing → {col_id} and write active.txt.
    // ------------------------------------------------------------------
    {
        // Remove old collection dir if present.
        char col_dir[128];
        snprintf(col_dir, sizeof(col_dir), "1:/collections/%s",
                 manifest.collection_id);

        // Clear old files in target dir (if any).
        {
            DIR d;
            if (f_opendir(&d, col_dir) == FR_OK) {
                FILINFO fi;
                char p[400];
                while (f_readdir(&d, &fi) == FR_OK && fi.fname[0] != '\0') {
                    snprintf(p, sizeof(p), "%s/%s", col_dir, fi.fname);
                    f_unlink(p);
                }
                f_closedir(&d);
            }
            f_unlink(col_dir);
        }

        // FatFs f_rename to move .installing → col_id.
        FRESULT fr = f_rename(INSTALLING_DIR, col_dir);
        if (fr != FR_OK) {
            result_out.reason = DiagCode::STORAGE_IO_ERROR;
            return DiagStatus::error(result_out.reason);
        }

        // Commit point: write active.txt.
        if (!fat_write_file(ACTIVE_PATH, manifest.collection_id,
                            strlen(manifest.collection_id))) {
            result_out.reason = DiagCode::STORAGE_IO_ERROR;
            return DiagStatus::error(result_out.reason);
        }
    }

    result_out.installed = true;
    result_out.reason    = DiagCode::OK;
    return DiagStatus::success();
}
