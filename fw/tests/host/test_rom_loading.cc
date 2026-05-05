// test_rom_loading.cc — Stage 28: ROM write-through during USB install.
//
// Verifies that Installer::run() with a RomInstallReader (FAT-backed):
//   1. Calls copy_to_fat() for each payload with a path.
//   2. Stores data_size > 0 in the PayloadRecord after install.
//   3. mapping_plan_from_payload_record() produces a valid entry.
//
// Also verifies that a plain MemoryInstallReader (default copy_to_fat no-op)
// leaves data_size == 0.

#include "content/installer.h"
#include "content/content_store.h"
#include "bus/mapping_plan.h"
#include "filesystem/fat_util.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "spine/otp_reader.h"
#include "crypto/sha256.h"

#include "fat_test_env.h"
#include "test_helpers.h"
#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Fake ROM payload — 32 bytes with recognisable header bytes.
// ---------------------------------------------------------------------------

static constexpr size_t kRomSize = 32u;
static const uint8_t kFakeRom[kRomSize] = {
    0xF3, 0x3E, 0x04, 0x32, 0x01, 0xA0, 0xC3, 0x08,
    0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4A, 0x4C, 0x50, 0x69, 0x43, 0x61, 0x72, 0x74,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ---------------------------------------------------------------------------
// Test manifest — one payload with a ROM path and mapper_type.
// ---------------------------------------------------------------------------

static const char kManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.romload\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"boot\": {\"mode\": \"direct\", \"payload_id\": \"main\"},"
    "  \"payloads\": [{"
    "    \"payload_id\": \"main\","
    "    \"path\": \"main.rom\","
    "    \"mapper_type\": \"rom\""
    "  }]"
    "}";

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

static PolicyStore make_open_policy() {
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    SecurityPosture posture = SecurityPosture::read(otp);
    PolicyStore ps;
    ps.load(posture);
    return ps;
}

// ---------------------------------------------------------------------------
// RomInstallReader — in-memory files + real copy_to_fat() implementation.
//
// Unlike the plain MemoryInstallReader used in other tests, this version
// overrides copy_to_fat() to write the payload into the FAT volume so that
// the Installer sees a non-zero data_size in the PayloadRecord.
// ---------------------------------------------------------------------------

class RomInstallReader : public InstallReader {
public:
    void add_text(const char* path, const char* text) {
        add_bytes(path,
                  reinterpret_cast<const uint8_t*>(text),
                  strlen(text));
    }

    void add_bytes(const char* path, const uint8_t* data, size_t len) {
        assert(count_ < 16u);
        paths_[count_] = path;
        data_[count_]  = data;
        sizes_[count_] = len;
        ++count_;
    }

    DiagStatus read_file(const char* path, uint8_t* buf,
                         size_t max_len, size_t* out_len) override {
        const uint8_t* d = nullptr; size_t n = 0;
        if (!find(path, &d, &n)) {
            *out_len = 0;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        if (n > max_len) {
            *out_len = n;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        memcpy(buf, d, n);
        *out_len = n;
        return DiagStatus::success();
    }

    DiagStatus hash_file(const char* path,
                         uint8_t digest[SHA256_DIGEST_SIZE]) override {
        const uint8_t* d = nullptr; size_t n = 0;
        if (!find(path, &d, &n))
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        sha256(d, n, digest);
        return DiagStatus::success();
    }

    bool file_exists(const char* path) override {
        const uint8_t* d = nullptr; size_t n = 0;
        return find(path, &d, &n);
    }

    // Write the in-memory payload to the destination FAT path.
    DiagStatus copy_to_fat(const char* src_path, const char* dst_path,
                            size_t* out_size) override {
        const uint8_t* d = nullptr; size_t n = 0;
        if (!find(src_path, &d, &n)) {
            *out_size = 0;
            return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
        }
        if (!fat_write_file(dst_path, d, n)) {
            *out_size = 0;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        *out_size = n;
        return DiagStatus::success();
    }

private:
    const char*    paths_[16] = {};
    const uint8_t* data_[16]  = {};
    size_t         sizes_[16] = {};
    size_t         count_     = 0;

    bool find(const char* path, const uint8_t** out_data,
              size_t* out_len) const {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(paths_[i], path) == 0) {
                *out_data = data_[i];
                *out_len  = sizes_[i];
                return true;
            }
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// MemoryInstallReader — plain reader with default no-op copy_to_fat().
// ---------------------------------------------------------------------------

struct MemFile { const char* path; const uint8_t* data; size_t len; };

class MemoryInstallReader : public InstallReader {
public:
    void add_text(const char* path, const char* text) {
        files_[count_++] = {path,
                             reinterpret_cast<const uint8_t*>(text),
                             strlen(text)};
    }
    DiagStatus read_file(const char* path, uint8_t* buf,
                          size_t max_len, size_t* out_len) override {
        const MemFile* f = find(path);
        if (!f) { *out_len = 0; return DiagStatus::error(DiagCode::STORAGE_IO_ERROR); }
        if (f->len > max_len) { *out_len = f->len; return DiagStatus::error(DiagCode::STORAGE_IO_ERROR); }
        memcpy(buf, f->data, f->len);
        *out_len = f->len;
        return DiagStatus::success();
    }
    DiagStatus hash_file(const char* path, uint8_t digest[SHA256_DIGEST_SIZE]) override {
        const MemFile* f = find(path);
        if (!f) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        sha256(f->data, f->len, digest);
        return DiagStatus::success();
    }
    bool file_exists(const char* path) override { return find(path) != nullptr; }
    // copy_to_fat: inherits default no-op → *out_size=0
private:
    MemFile files_[8] = {};
    size_t  count_ = 0;
    const MemFile* find(const char* path) const {
        for (size_t i = 0; i < count_; ++i)
            if (strcmp(files_[i].path, path) == 0) return &files_[i];
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// test_payload_written_to_fat
//
// Installing with a RomInstallReader must:
//   - Write ROM bytes to 1:/collections/com.test.romload/rom_main.bin.
//   - Store data_size == kRomSize in the PayloadRecord.
// ---------------------------------------------------------------------------

static void test_payload_written_to_fat() {
    FatTestEnv env;
    PolicyStore policy = make_open_policy();

    RomInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    reader.add_bytes("main.rom", kFakeRom, kRomSize);

    Installer     installer;
    InstallResult result = {};
    installer.run(reader, policy, result);

    CHECK(result.installed);

    ContentStore cs;
    CHECK(cs.has_active_collection());

    PayloadRecord pr = {};
    DiagStatus s = cs.load_default_payload(pr);
    CHECK(s.ok());

    CHECK(pr.data_size == kRomSize);

    // Verify bytes were actually written to FAT.
    uint8_t readback[kRomSize] = {};
    size_t actual = 0;
    CHECK(fat_read_file("1:/collections/com.test.romload/rom_main.bin",
                         readback, kRomSize, &actual));
    CHECK(actual == kRomSize);
    CHECK(memcmp(readback, kFakeRom, kRomSize) == 0);
}

// ---------------------------------------------------------------------------
// test_no_copy_leaves_data_size_zero
//
// Installing with a plain MemoryInstallReader (default copy_to_fat no-op)
// must leave data_size == 0.
// ---------------------------------------------------------------------------

static void test_no_copy_leaves_data_size_zero() {
    FatTestEnv env;
    PolicyStore policy = make_open_policy();

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifest);

    Installer     installer;
    InstallResult result = {};
    installer.run(reader, policy, result);

    CHECK(result.installed);

    ContentStore cs;
    PayloadRecord pr = {};
    DiagStatus s = cs.load_default_payload(pr);
    CHECK(s.ok());

    CHECK(pr.data_size == 0u);
}

// ---------------------------------------------------------------------------
// test_mapping_plan_from_loaded_payload
//
// After a successful install (data_size > 0), mapping_plan_from_payload_record
// must return a plan with entry_count == 1 and the correct rom_size.
// On host, rom_data is always nullptr (no XIP); we check structure only.
// ---------------------------------------------------------------------------

static void test_mapping_plan_from_loaded_payload() {
    FatTestEnv env;
    PolicyStore policy = make_open_policy();

    RomInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    reader.add_bytes("main.rom", kFakeRom, kRomSize);

    Installer     installer;
    InstallResult result = {};
    installer.run(reader, policy, result);
    CHECK(result.installed);

    ContentStore cs;
    PayloadRecord pr = {};
    cs.load_default_payload(pr);

    MappingPlan plan = mapping_plan_from_payload_record(pr);
    CHECK(plan.entry_count         == 1u);
    CHECK(plan.entries[0].rom_size == kRomSize);
    CHECK(plan.entries[0].mapper_type == MapperType::ROM);
}

// ---------------------------------------------------------------------------
// test_missing_file_leaves_data_size_zero
//
// A payload whose path is present in the manifest but missing from the
// reader (file doesn't exist) must leave data_size == 0; the install
// itself must still succeed (non-fatal per spec §11.1 Phase 2b).
// ---------------------------------------------------------------------------

static const char kManifestMissingFile[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.missing\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"boot\": {\"mode\": \"direct\", \"payload_id\": \"main\"},"
    "  \"payloads\": [{\"payload_id\": \"main\", \"path\": \"missing.rom\","
    "                  \"mapper_type\": \"rom\"}]"
    "}";

static void test_missing_file_leaves_data_size_zero() {
    FatTestEnv env;
    PolicyStore policy = make_open_policy();

    // Reader has the manifest but NOT the ROM file — copy_to_fat returns
    // STORAGE_NOT_FOUND; installer must treat this as non-fatal.
    RomInstallReader reader;
    reader.add_text("manifest.json", kManifestMissingFile);
    // Intentionally NOT adding "missing.rom".

    Installer     installer;
    InstallResult result = {};
    installer.run(reader, policy, result);

    CHECK(result.installed);

    ContentStore cs;
    PayloadRecord pr = {};
    DiagStatus s = cs.load_default_payload(pr);
    CHECK(s.ok());

    // data_size must be 0 — no ROM was written.
    CHECK(pr.data_size == 0u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_payload_written_to_fat();
    test_no_copy_leaves_data_size_zero();
    test_mapping_plan_from_loaded_payload();
    test_missing_file_leaves_data_size_zero();

    return test_summary();
}
