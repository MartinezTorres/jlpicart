// test_usb_scanner.cc — host tests for UsbInstallScanner logic.
//
// Tests use:
//   FakeUsbHost        — override is_msc_mounted() to control mount state
//   MemInstallDirSource — inject in-memory MemoryInstallReader instances
//                         so scan logic runs without FatFs or real USB
//
// All USB / FatFs hardware paths are guarded by #ifndef JLPICART_HOST_TEST
// in the scanner; these tests exercise the portable run_scan() entry point.

#include "usb/usb_host.h"
#include "usb/usb_install_scanner.h"
#include "content/collection_format.h"
#include "content/manifest.h"
#include "content/installer.h"
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
// Harness
// ---------------------------------------------------------------------------

static constexpr uint32_t TEST_KV_SIZE  = FLASH_SECTOR_SIZE * 4;
static constexpr uint32_t TEST_LOG_SIZE = FLASH_SECTOR_SIZE * 4;

static PolicyStore make_open_policy() {
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    SecurityPosture posture = SecurityPosture::read(otp);
    PolicyStore ps;
    ps.load(posture);
    return ps;
}

// ---------------------------------------------------------------------------
// FakeUsbHost — controls is_msc_mounted() return value
// ---------------------------------------------------------------------------

class FakeUsbHost : public UsbHost {
public:
    explicit FakeUsbHost(bool mounted) : mounted_(mounted) {}
    bool is_msc_mounted() const override { return mounted_; }
private:
    bool mounted_;
};

// ---------------------------------------------------------------------------
// MemoryInstallReader — in-memory InstallReader for tests
// (mirrors the one in test_collections.cc)
// ---------------------------------------------------------------------------

struct MemFile {
    const char*    path;
    const uint8_t* data;
    size_t         len;
};

class MemoryInstallReader : public InstallReader {
public:
    void add_text(const char* path, const char* text) {
        assert(count_ < 16);
        files_[count_++] = {path,
                             reinterpret_cast<const uint8_t*>(text),
                             strlen(text)};
    }

    DiagStatus read_file(const char* path, uint8_t* buf,
                          size_t max_len, size_t* out_len) override {
        const MemFile* f = find(path);
        if (!f) {
            *out_len = 0;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        if (f->len > max_len) {
            *out_len = f->len;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        memcpy(buf, f->data, f->len);
        *out_len = f->len;
        return DiagStatus::success();
    }

    DiagStatus hash_file(const char* path,
                          uint8_t digest[SHA256_DIGEST_SIZE]) override {
        const MemFile* f = find(path);
        if (!f) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        sha256(f->data, f->len, digest);
        return DiagStatus::success();
    }

    bool file_exists(const char* path) override {
        return find(path) != nullptr;
    }

private:
    MemFile files_[16] = {};
    size_t  count_ = 0;

    const MemFile* find(const char* path) const {
        for (size_t i = 0; i < count_; ++i)
            if (strcmp(files_[i].path, path) == 0) return &files_[i];
        return nullptr;
    }
};

// ---------------------------------------------------------------------------
// MemInstallDirSource — in-memory InstallDirSource for tests
// ---------------------------------------------------------------------------

class MemInstallDirSource : public InstallDirSource {
public:
    // open_count tracks how many times open_reader() was called.
    size_t open_count = 0;

    void add_dir(const char* name, MemoryInstallReader* reader) {
        assert(count_ < 16);
        names_[count_]   = name;
        readers_[count_] = reader;
        ++count_;
    }

    size_t count() const override { return count_; }

    const char* dir_name(size_t idx) const override {
        return idx < count_ ? names_[idx] : "";
    }

    InstallReader& open_reader(size_t idx) override {
        ++open_count;
        return *readers_[idx];
    }

private:
    const char*           names_[16]   = {};
    MemoryInstallReader*  readers_[16] = {};
    size_t                count_ = 0;
};

// ---------------------------------------------------------------------------
// Manifest helpers
// ---------------------------------------------------------------------------

static const char kManifestA[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.alpha\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"payloads\": [{\"payload_id\": \"main\", \"path\": \"main.rom\"}]"
    "}";

static const char kManifestB[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.beta\","
    "  \"version\": \"2.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"payloads\": [{\"payload_id\": \"main\", \"path\": \"main.rom\"}]"
    "}";

// Build manifest with a unique collection_id (for the 9-dirs test).
static char g_manifests[12][256];
static void make_manifest(int idx, const char* col_id, const char* ver) {
    snprintf(g_manifests[idx], sizeof(g_manifests[idx]),
        "{"
        "\"format_version\":\"1.0\","
        "\"collection_id\":\"%s\","
        "\"version\":\"%s\","
        "\"publisher\":{\"publisher_id\":\"p\",\"name\":\"P\"},"
        "\"payloads\":[{\"payload_id\":\"m\",\"path\":\"m.rom\"}]"
        "}",
        col_id, ver);
}

// ---------------------------------------------------------------------------
// test_scan_not_mounted — early return when MSC not mounted
// ---------------------------------------------------------------------------

static void test_scan_not_mounted() {
    FakeUsbHost usb_host(/*mounted=*/false);
    FakeUsbHost* host_ptr = &usb_host;

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifestA);

    MemInstallDirSource dirs;
    dirs.add_dir("alpha", &reader);

    UsbInstallScanner scanner(*host_ptr);
    // scan() should check is_msc_mounted() and return without calling run_scan().
    scanner.scan(kv, event_log, policy);

    // Nothing was written to KvStore (open_count is only tracked by dirs
    // which we never passed to scan() — verify KvStore stays empty).
    CHECK(!kv.contains(KV_COL_STATE));
    CHECK(!kv.contains(KV_COL_RECORD));
}

// ---------------------------------------------------------------------------
// test_scan_installs_each_dir — run_scan calls Installer for each dir
// ---------------------------------------------------------------------------

static void test_scan_installs_each_dir() {
    MemoryInstallReader readerA, readerB;
    readerA.add_text("manifest.json", kManifestA);
    readerB.add_text("manifest.json", kManifestB);

    MemInstallDirSource dirs;
    dirs.add_dir("alpha", &readerA);
    dirs.add_dir("beta",  &readerB);

    FakeUsbHost usb_host(true);
    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    UsbInstallScanner scanner(usb_host);
    scanner.run_scan(dirs, kv, event_log, policy);

    // Both dirs should have been opened.
    CHECK(dirs.open_count == 2u);

    // Last install (beta) should be active.
    uint8_t state_val[16] = {}; uint16_t state_len = 0;
    kv.get(KV_COL_STATE, state_val, &state_len, sizeof(state_val));
    CHECK(memcmp(state_val, COL_STATE_ACTIVE, strlen(COL_STATE_ACTIVE)) == 0);

    CollectionRecord rec = {}; uint16_t rec_len = 0;
    kv.get(KV_COL_RECORD,
           reinterpret_cast<uint8_t*>(&rec), &rec_len, sizeof(rec));
    CHECK(strcmp(rec.collection_id, "com.test.beta") == 0);
}

// ---------------------------------------------------------------------------
// test_scan_skips_already_installed — second scan with same id+version skips
// ---------------------------------------------------------------------------

static void test_scan_skips_already_installed() {
    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifestA);

    FakeUsbHost usb_host(true);
    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemInstallDirSource dirs;
    dirs.add_dir("alpha", &reader);

    UsbInstallScanner scanner(usb_host);

    // First scan: installs the collection.
    scanner.run_scan(dirs, kv, event_log, policy);
    CHECK(dirs.open_count == 1u);
    CHECK(kv.contains(KV_COL_RECORD));

    size_t kv_count_after_first = kv.live_count();

    // Second scan: same dir, same manifest.
    // open_reader is still called (manifest is read for skip check),
    // but Installer::run() should NOT be called, so no new KvStore entries.
    MemInstallDirSource dirs2;
    dirs2.add_dir("alpha", &reader);
    scanner.run_scan(dirs2, kv, event_log, policy);

    CHECK(dirs2.open_count == 1u);  // open_reader was called once
    // No new keys should have been written.
    CHECK(kv.live_count() == kv_count_after_first);
}

// ---------------------------------------------------------------------------
// test_scan_max_dirs_limit — only INSTALL_SCAN_MAX_DIRS (8) dirs processed
// ---------------------------------------------------------------------------

static void test_scan_max_dirs_limit() {
    // Build 9 unique readers.
    static MemoryInstallReader readers[9];
    for (int i = 0; i < 9; ++i) {
        char col_id[32]; snprintf(col_id, sizeof(col_id), "com.test.game%d", i);
        char ver[16];    snprintf(ver,    sizeof(ver),    "1.0.%d", i);
        make_manifest(i, col_id, ver);
        readers[i] = MemoryInstallReader{};
        readers[i].add_text("manifest.json", g_manifests[i]);
    }

    MemInstallDirSource dirs;
    char names[9][8];
    for (int i = 0; i < 9; ++i) {
        snprintf(names[i], sizeof(names[i]), "game%d", i);
        dirs.add_dir(names[i], &readers[i]);
    }

    FakeUsbHost usb_host(true);
    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog event_log; event_log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    UsbInstallScanner scanner(usb_host);
    scanner.run_scan(dirs, kv, event_log, policy);

    // Exactly INSTALL_SCAN_MAX_DIRS readers should have been opened.
    CHECK(dirs.open_count == INSTALL_SCAN_MAX_DIRS);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_scan_not_mounted();
    test_scan_installs_each_dir();
    test_scan_skips_already_installed();
    test_scan_max_dirs_limit();

    return test_summary();
}
