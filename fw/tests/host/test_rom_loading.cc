// test_rom_loading.cc — Stage 28: ROM write-through during USB install.
//
// Verifies that Installer::run() with a non-null FlashDevice:
//   1. Calls copy_to_flash() for each payload with a path.
//   2. Stores data_size > 0 in the PayloadRecord after install.
//   3. mapping_plan_from_payload_record() produces a valid entry.
//
// Also verifies that flash=nullptr leaves data_size == 0 (old behaviour,
// backwards-compatible with test suites that pass no flash device).

#include "content/installer.h"
#include "content/content_store.h"
#include "bus/mapping_plan.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "security/otp_reader.h"
#include "crypto/sha256.h"

#include "test_helpers.h"
#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------

static constexpr uint32_t TEST_KV_SIZE    = FLASH_SECTOR_SIZE * 4;
static constexpr uint32_t TEST_LOG_SIZE   = FLASH_SECTOR_SIZE * 4;
// Content flash must be large enough to hold CONTENT_DATA at 0x600000.
static constexpr size_t   CONTENT_FLASH_SIZE =
    static_cast<size_t>(FLASH_CONTENT_DATA_OFS) + FLASH_SECTOR_SIZE * 4u;

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
// RomInstallReader — in-memory files + real copy_to_flash() implementation.
//
// Unlike the plain MemoryInstallReader used in other tests, this version
// overrides copy_to_flash() to actually erase and write sectors so that
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

    // Stream the file to flash in FLASH_SECTOR_SIZE chunks (erase + write).
    DiagStatus copy_to_flash(const char* path, FlashDevice& flash,
                              uint32_t flash_offset,
                              size_t* out_size) override {
        const uint8_t* d = nullptr; size_t n = 0;
        if (!find(path, &d, &n)) {
            *out_size = 0;
            return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
        }

        uint8_t sector_buf[FLASH_SECTOR_SIZE];
        size_t  written   = 0;
        size_t  remaining = n;
        uint32_t offset   = flash_offset;

        while (remaining > 0) {
            size_t chunk = (remaining < FLASH_SECTOR_SIZE) ? remaining
                                                            : FLASH_SECTOR_SIZE;
            memcpy(sector_buf, d + written, chunk);
            if (chunk < FLASH_SECTOR_SIZE)
                memset(sector_buf + chunk, 0xFF, FLASH_SECTOR_SIZE - chunk);

            DiagStatus s = flash.erase(offset, 1u);
            if (!s.ok()) { *out_size = written; return s; }

            s = flash.write(offset, sector_buf, chunk);
            if (!s.ok()) { *out_size = written; return s; }

            written   += chunk;
            offset    += FLASH_SECTOR_SIZE;
            remaining -= chunk;
        }

        *out_size = written;
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
// test_payload_written_to_flash
//
// Installing with a non-null FlashDevice must:
//   - Write ROM bytes to FLASH_CONTENT_DATA_OFS.
//   - Store data_size == kRomSize in the PayloadRecord.
//   - Store data_flash_offset == FLASH_CONTENT_DATA_OFS.
// ---------------------------------------------------------------------------

static void test_payload_written_to_flash() {
    FlashDevice kv_flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    FlashDevice content_flash(CONTENT_FLASH_SIZE);

    KvStore   kv;        kv.init(kv_flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(kv_flash, TEST_KV_SIZE, TEST_LOG_SIZE);
    PolicyStore policy = make_open_policy();

    RomInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    reader.add_bytes("main.rom", kFakeRom, kRomSize);

    Installer    installer;
    InstallResult result = {};
    installer.run(reader, kv, event_log, policy, result, &content_flash);

    CHECK(result.installed);

    ContentStore cs(kv);
    CHECK(cs.has_active_collection());

    PayloadRecord pr = {};
    DiagStatus s = cs.load_default_payload(pr);
    CHECK(s.ok());

    CHECK(pr.data_size         == kRomSize);
    CHECK(pr.data_flash_offset == FLASH_CONTENT_DATA_OFS);

    // Verify bytes were actually written at the correct flash offset.
    uint8_t readback[kRomSize] = {};
    DiagStatus rs = content_flash.read(FLASH_CONTENT_DATA_OFS, readback, kRomSize);
    CHECK(rs.ok());
    CHECK(memcmp(readback, kFakeRom, kRomSize) == 0);
}

// ---------------------------------------------------------------------------
// test_no_flash_leaves_data_size_zero
//
// Installing with flash=nullptr (backward-compatible path used by host tests
// that don't care about ROM data) must leave data_size == 0.
// ---------------------------------------------------------------------------

static void test_no_flash_leaves_data_size_zero() {
    FlashDevice kv_flash(TEST_KV_SIZE + TEST_LOG_SIZE);

    KvStore   kv;        kv.init(kv_flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(kv_flash, TEST_KV_SIZE, TEST_LOG_SIZE);
    PolicyStore policy = make_open_policy();

    RomInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    reader.add_bytes("main.rom", kFakeRom, kRomSize);

    Installer    installer;
    InstallResult result = {};
    installer.run(reader, kv, event_log, policy, result, /*flash=*/nullptr);

    CHECK(result.installed);

    ContentStore cs(kv);
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
    FlashDevice kv_flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    FlashDevice content_flash(CONTENT_FLASH_SIZE);

    KvStore   kv;        kv.init(kv_flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(kv_flash, TEST_KV_SIZE, TEST_LOG_SIZE);
    PolicyStore policy = make_open_policy();

    RomInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    reader.add_bytes("main.rom", kFakeRom, kRomSize);

    Installer    installer;
    InstallResult result = {};
    installer.run(reader, kv, event_log, policy, result, &content_flash);
    CHECK(result.installed);

    ContentStore cs(kv);
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
    FlashDevice kv_flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    FlashDevice content_flash(CONTENT_FLASH_SIZE);

    KvStore   kv;        kv.init(kv_flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(kv_flash, TEST_KV_SIZE, TEST_LOG_SIZE);
    PolicyStore policy = make_open_policy();

    // Reader has the manifest but NOT the ROM file — copy_to_flash returns
    // STORAGE_NOT_FOUND; installer must treat this as non-fatal.
    RomInstallReader reader;
    reader.add_text("manifest.json", kManifestMissingFile);
    // Intentionally NOT adding "missing.rom".

    Installer    installer;
    InstallResult result = {};
    installer.run(reader, kv, event_log, policy, result, &content_flash);

    CHECK(result.installed);

    ContentStore cs(kv);
    PayloadRecord pr = {};
    DiagStatus s = cs.load_default_payload(pr);
    CHECK(s.ok());

    // data_size must be 0 — no ROM was written.
    CHECK(pr.data_size == 0u);

    // Flash at CONTENT_DATA should remain erased (0xFF).
    uint8_t check_byte = 0x00;
    content_flash.read(FLASH_CONTENT_DATA_OFS, &check_byte, 1u);
    CHECK(check_byte == 0xFF);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_payload_written_to_flash();
    test_no_flash_leaves_data_size_zero();
    test_mapping_plan_from_loaded_payload();
    test_missing_file_leaves_data_size_zero();

    return test_summary();
}
