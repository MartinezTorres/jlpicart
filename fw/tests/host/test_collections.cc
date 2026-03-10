// test_collections.cc — Host tests for Stage 7 collection format, manifest
// parsing, install pipeline, and receipts.

#include "content/collection_format.h"
#include "content/manifest.h"
#include "content/manifest_parser.h"
#include "content/installer.h"
#include "content/receipts.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "crypto/sha256.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_OK(s)    CHECK((s).ok())
#define CHECK_FAIL(s)  CHECK(!(s).ok())

static constexpr uint32_t TEST_KV_SIZE  = FLASH_SECTOR_SIZE * 4;
static constexpr uint32_t TEST_LOG_SIZE = FLASH_SECTOR_SIZE * 4;

// ---------------------------------------------------------------------------
// Minimal manifests for testing
// ---------------------------------------------------------------------------

static const char kMinimalManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.example.test\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {"
    "    \"publisher_id\": \"com.example\","
    "    \"name\": \"Example Studio\""
    "  },"
    "  \"title\": \"Test Collection\","
    "  \"payloads\": ["
    "    {\"payload_id\": \"main\", \"path\": \"payloads/game.rom\","
    "     \"title\": \"Main Game\"}"
    "  ]"
    "}";

static const char kDirectBootManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.example.direct\","
    "  \"version\": \"2.0.0\","
    "  \"publisher\": {"
    "    \"publisher_id\": \"com.direct\","
    "    \"name\": \"Direct Publisher\""
    "  },"
    "  \"boot\": {\"mode\": \"direct\", \"payload_id\": \"game\"},"
    "  \"payloads\": ["
    "    {\"payload_id\": \"game\", \"path\": \"payloads/g.rom\"}"
    "  ]"
    "}";

static const char kTwoPayloadManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.example.multi\","
    "  \"version\": \"1.2.3\","
    "  \"publisher\": {\"publisher_id\": \"com.pub\", \"name\": \"Pub\"},"
    "  \"payloads\": ["
    "    {\"payload_id\": \"a\", \"path\": \"payloads/a.rom\"},"
    "    {\"payload_id\": \"b\", \"path\": \"payloads/b.rom\"}"
    "  ]"
    "}";

// Unknown extra fields must be silently ignored.
static const char kExtraFieldsManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.example.extra\","
    "  \"version\": \"0.1.0\","
    "  \"publisher\": {\"publisher_id\": \"pub\", \"name\": \"P\", \"website\": \"http://x\"},"
    "  \"unknown_key\": {\"nested\": [1, 2, 3]},"
    "  \"payloads\": [{\"payload_id\": \"x\", \"path\": \"p/x.rom\", \"extra\": true}]"
    "}";

static const char kMissingCollectionIdManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.x\", \"name\": \"X\"},"
    "  \"payloads\": [{\"payload_id\": \"x\", \"path\": \"p/x.rom\"}]"
    "}";

static const char kWrongFormatVersionManifest[] =
    "{"
    "  \"format_version\": \"2.0\","
    "  \"collection_id\": \"com.example.v2\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"pub\", \"name\": \"P\"},"
    "  \"payloads\": [{\"payload_id\": \"x\", \"path\": \"p.rom\"}]"
    "}";

// ---------------------------------------------------------------------------
// MemoryInstallReader — in-memory InstallReader for host tests
// ---------------------------------------------------------------------------

struct MemFile {
    const char*    path;
    const uint8_t* data;
    size_t         len;
};

class MemoryInstallReader : public InstallReader {
public:
    void add_file(const char* path, const uint8_t* data, size_t len) {
        assert(count_ < 16 && "MemoryInstallReader: too many files");
        files_[count_++] = {path, data, len};
    }
    void add_text(const char* path, const char* text) {
        add_file(path, reinterpret_cast<const uint8_t*>(text), strlen(text));
    }

    DiagStatus read_file(const char* path, uint8_t* buf,
                          size_t max_len, size_t* out_len) override {
        const MemFile* f = find(path);
        if (!f) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        if (f->len > max_len) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
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
// Helpers to stand up an open-policy PolicyStore for tests
// ---------------------------------------------------------------------------

#include "security/otp_reader.h"

static PolicyStore make_open_policy() {
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    SecurityPosture posture = SecurityPosture::read(otp);
    PolicyStore ps;
    ps.load(posture); // ignores failure; returns safe defaults (no sig required)
    return ps;
}

// ---------------------------------------------------------------------------
// Manifest parsing tests
// ---------------------------------------------------------------------------

static void test_manifest_parse_minimal() {
    CollectionManifest m = {};
    DiagStatus s = parse_collection_manifest(kMinimalManifest,
                                              strlen(kMinimalManifest), m);
    CHECK_OK(s);
    CHECK(strcmp(m.collection_id, "com.example.test") == 0);
    CHECK(strcmp(m.version,       "1.0.0") == 0);
    CHECK(strcmp(m.publisher_id,  "com.example") == 0);
    CHECK(strcmp(m.publisher_name,"Example Studio") == 0);
    CHECK(strcmp(m.title,         "Test Collection") == 0);
    CHECK_EQ(m.boot_mode, 0u);  // default: menu_first
    CHECK_EQ(m.payload_count, 1u);
    CHECK(strcmp(m.payloads[0].payload_id, "main") == 0);
    CHECK(strcmp(m.payloads[0].path, "payloads/game.rom") == 0);
    CHECK(strcmp(m.payloads[0].title, "Main Game") == 0);
}

static void test_manifest_parse_direct_boot() {
    CollectionManifest m = {};
    CHECK_OK(parse_collection_manifest(kDirectBootManifest,
                                        strlen(kDirectBootManifest), m));
    CHECK_EQ(m.boot_mode, 1u);  // direct
    CHECK(strcmp(m.default_payload_id, "game") == 0);
}

static void test_manifest_parse_two_payloads() {
    CollectionManifest m = {};
    CHECK_OK(parse_collection_manifest(kTwoPayloadManifest,
                                        strlen(kTwoPayloadManifest), m));
    CHECK_EQ(m.payload_count, 2u);
    CHECK(strcmp(m.payloads[0].payload_id, "a") == 0);
    CHECK(strcmp(m.payloads[1].payload_id, "b") == 0);
}

static void test_manifest_parse_unknown_fields_ignored() {
    CollectionManifest m = {};
    CHECK_OK(parse_collection_manifest(kExtraFieldsManifest,
                                        strlen(kExtraFieldsManifest), m));
    CHECK(strcmp(m.collection_id, "com.example.extra") == 0);
    CHECK_EQ(m.payload_count, 1u);
}

static void test_manifest_parse_missing_required_field() {
    CollectionManifest m = {};
    DiagStatus s = parse_collection_manifest(kMissingCollectionIdManifest,
                                              strlen(kMissingCollectionIdManifest), m);
    CHECK_FAIL(s);
    CHECK(s.code == DiagCode::COLLECTION_BAD_MANIFEST);
}

static void test_manifest_parse_wrong_format_version() {
    CollectionManifest m = {};
    DiagStatus s = parse_collection_manifest(kWrongFormatVersionManifest,
                                              strlen(kWrongFormatVersionManifest), m);
    CHECK_FAIL(s);
    CHECK(s.code == DiagCode::COLLECTION_BAD_MANIFEST);
}

static void test_manifest_parse_invalid_json() {
    const char* bad = "{\"format_version\": \"1.0\", BROKEN";
    CollectionManifest m = {};
    CHECK_FAIL(parse_collection_manifest(bad, strlen(bad), m));
}

// ---------------------------------------------------------------------------
// bundle.sig parsing tests
// ---------------------------------------------------------------------------

static void compute_sha256_hex(const uint8_t* data, size_t len, char hex_out[65]) {
    uint8_t digest[32];
    sha256(data, len, digest);
    for (int i = 0; i < 32; ++i)
        snprintf(hex_out + 2*i, 3, "%02x", digest[i]);
    hex_out[64] = '\0';
}

static void test_bundle_sig_parse() {
    // Build a bundle.sig with two file entries.
    char manifest_hex[65], rom_hex[65];
    const char rom_bytes[] = { 0x41, 0x42, 0x43 };
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(kMinimalManifest),
                        strlen(kMinimalManifest), manifest_hex);
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(rom_bytes),
                        sizeof(rom_bytes), rom_hex);

    char sig_json[1024];
    snprintf(sig_json, sizeof(sig_json),
        "{"
        "  \"sig_schema\": \"jlpicart.signature.v1\","
        "  \"alg\": \"ecdsa_secp256k1_sha256\","
        "  \"key_id\": \"pub-key-2026\","
        "  \"files\": ["
        "    {\"path\": \"manifest.json\", \"sha256\": \"%s\"},"
        "    {\"path\": \"payloads/game.rom\", \"sha256\": \"%s\"}"
        "  ],"
        "  \"manifest_path\": \"manifest.json\","
        "  \"signature\": \"AAAA\""
        "}",
        manifest_hex, rom_hex);

    BundleSigEnvelope env = {};
    CHECK_OK(parse_bundle_sig(sig_json, strlen(sig_json), env));
    CHECK(env.has_envelope);
    CHECK(strcmp(env.sig_schema, "jlpicart.signature.v1") == 0);
    CHECK(strcmp(env.alg, "ecdsa_secp256k1_sha256") == 0);
    CHECK(strcmp(env.key_id, "pub-key-2026") == 0);
    CHECK_EQ(env.file_count, 2u);
    CHECK(strcmp(env.files[0].path, "manifest.json") == 0);
    CHECK(strcmp(env.files[1].path, "payloads/game.rom") == 0);

    // Verify the decoded sha256 bytes match the computed digest.
    uint8_t expected[32];
    sha256(reinterpret_cast<const uint8_t*>(kMinimalManifest),
           strlen(kMinimalManifest), expected);
    CHECK(memcmp(env.files[0].sha256, expected, 32) == 0);
}

// ---------------------------------------------------------------------------
// Hash verification tests
// ---------------------------------------------------------------------------

static void test_bundle_hash_verification_pass() {
    // Build a reader with manifest.json and a ROM; build matching bundle.sig.
    const char rom_bytes[] = { 0x01, 0x02, 0x03 };
    char manifest_hex[65], rom_hex[65];
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(kMinimalManifest),
                        strlen(kMinimalManifest), manifest_hex);
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(rom_bytes),
                        sizeof(rom_bytes), rom_hex);

    char sig_json[1024];
    snprintf(sig_json, sizeof(sig_json),
        "{"
        "  \"sig_schema\": \"jlpicart.signature.v1\","
        "  \"alg\": \"ecdsa_secp256k1_sha256\","
        "  \"key_id\": \"k\","
        "  \"files\": ["
        "    {\"path\": \"manifest.json\",      \"sha256\": \"%s\"},"
        "    {\"path\": \"payloads/game.rom\",  \"sha256\": \"%s\"}"
        "  ],"
        "  \"signature\": \"AAAA\""
        "}",
        manifest_hex, rom_hex);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kMinimalManifest);
    reader.add_file("payloads/game.rom",
                    reinterpret_cast<const uint8_t*>(rom_bytes), sizeof(rom_bytes));
    reader.add_text("bundle.sig", sig_json);

    // Use open policy so signature check is skipped.
    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;
    kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log;
    log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    CHECK_OK(installer.run(reader, kv, log, policy, result));
    CHECK(result.installed);
}

static void test_bundle_hash_verification_fail() {
    // Corrupt the ROM so its hash mismatches what bundle.sig declares.
    const char real_rom[]   = { 0x01, 0x02, 0x03 };
    const uint8_t decoy_rom[]  = { 0xFFu, 0xFFu, 0xFFu }; // wrong content
    char manifest_hex[65], rom_hex[65];
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(kMinimalManifest),
                        strlen(kMinimalManifest), manifest_hex);
    compute_sha256_hex(reinterpret_cast<const uint8_t*>(real_rom),
                        sizeof(real_rom), rom_hex);

    char sig_json[1024];
    snprintf(sig_json, sizeof(sig_json),
        "{"
        "  \"sig_schema\": \"jlpicart.signature.v1\","
        "  \"alg\": \"ecdsa_secp256k1_sha256\","
        "  \"key_id\": \"k\","
        "  \"files\": ["
        "    {\"path\": \"manifest.json\",     \"sha256\": \"%s\"},"
        "    {\"path\": \"payloads/game.rom\", \"sha256\": \"%s\"}"
        "  ],"
        "  \"signature\": \"AAAA\""
        "}",
        manifest_hex, rom_hex);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kMinimalManifest);
    // Serve the wrong ROM bytes (hash mismatch).
    reader.add_file("payloads/game.rom",
                    reinterpret_cast<const uint8_t*>(decoy_rom), sizeof(decoy_rom));
    reader.add_text("bundle.sig", sig_json);

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    DiagStatus s = installer.run(reader, kv, log, policy, result);
    CHECK_FAIL(s);
    CHECK(!result.installed);
    CHECK(s.code == DiagCode::COLLECTION_HASH_MISMATCH);
}

// ---------------------------------------------------------------------------
// Installer integration tests
// ---------------------------------------------------------------------------

static void test_install_success() {
    MemoryInstallReader reader;
    reader.add_text("manifest.json", kMinimalManifest);

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    CHECK_OK(installer.run(reader, kv, log, policy, result));
    CHECK(result.installed);
    CHECK(strcmp(result.collection_id, "com.example.test") == 0);
    CHECK(strcmp(result.version, "1.0.0") == 0);

    // col.state must be "active".
    uint8_t state_val[16] = {}; uint16_t state_len = 0;
    CHECK_OK(kv.get(KV_COL_STATE, state_val, &state_len, sizeof(state_val)));
    CHECK(memcmp(state_val, COL_STATE_ACTIVE, strlen(COL_STATE_ACTIVE)) == 0);

    // col.record must be present and correctly populated.
    CollectionRecord rec = {};
    uint16_t rec_len = 0;
    CHECK_OK(kv.get(KV_COL_RECORD,
                    reinterpret_cast<uint8_t*>(&rec), &rec_len, sizeof(rec)));
    CHECK_EQ(rec_len, sizeof(rec));
    CHECK(strcmp(rec.collection_id, "com.example.test") == 0);
    CHECK(strcmp(rec.version, "1.0.0") == 0);
    CHECK(strcmp(rec.publisher_id, "com.example") == 0);
    CHECK_EQ(rec.boot_mode, 0u);
    CHECK_EQ(rec.payload_count, 1u);
}

static void test_install_survives_reinit() {
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);

    // Install.
    {
        MemoryInstallReader reader;
        reader.add_text("manifest.json", kMinimalManifest);
        PolicyStore policy = make_open_policy();
        KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
        AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);
        Installer installer;
        InstallResult result = {};
        CHECK_OK(installer.run(reader, kv, log, policy, result));
    }

    // Reinit — same flash device.
    KvStore kv2;
    CHECK_OK(kv2.init(flash, 0, TEST_KV_SIZE));

    uint8_t state_val[16] = {}; uint16_t state_len = 0;
    CHECK_OK(kv2.get(KV_COL_STATE, state_val, &state_len, sizeof(state_val)));
    CHECK(memcmp(state_val, COL_STATE_ACTIVE, strlen(COL_STATE_ACTIVE)) == 0);

    CollectionRecord rec = {}; uint16_t rec_len = 0;
    CHECK_OK(kv2.get(KV_COL_RECORD,
                     reinterpret_cast<uint8_t*>(&rec), &rec_len, sizeof(rec)));
    CHECK(strcmp(rec.collection_id, "com.example.test") == 0);
}

static void test_install_power_loss_before_commit() {
    // Inject power loss after writing the CollectionRecord (step b) but before
    // writing the "active" state (step c). The install must not be visible on reinit.
    //
    // Record sizes in the KvStore log:
    //   "col.state" = 9 chars key + "pending"(7) + hdr(8) = 24 bytes  → first write
    //   "col.record" = 10 chars key + 357 bytes val + hdr(8) = 375 bytes → second write
    //   "col.state" update = another 24 bytes → third write (we cut here)
    //
    // We allow 24 + 375 = 399 bytes, cutting off just before the commit.
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);

    // Allow all bytes for "col.state"="pending" (24 B) + "col.record" (375 B),
    // then cut before the "active" commit write (third put, another 24 B).
    //   "col.state"="pending": hdr(8) + key(9) + val(7) = 24 bytes
    //   "col.record"=record:   hdr(8) + key(10) + val(357) = 375 bytes
    {
        flash.inject_power_loss_after(399);

        MemoryInstallReader reader;
        reader.add_text("manifest.json", kMinimalManifest);
        PolicyStore policy = make_open_policy();
        KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
        AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);
        Installer installer;
        InstallResult result = {};
        installer.run(reader, kv, log, policy, result); // may fail — that's expected
    }

    // Reinit — must see NO active collection.
    KvStore kv3;
    CHECK_OK(kv3.init(flash, 0, TEST_KV_SIZE));

    uint8_t state_val[16] = {}; uint16_t state_len = 0;
    DiagStatus s = kv3.get(KV_COL_STATE, state_val, &state_len, sizeof(state_val));
    // Either key is absent (STORAGE_NOT_FOUND) or state is "pending" — never "active".
    bool no_active_collection =
        !s.ok() || memcmp(state_val, COL_STATE_ACTIVE, strlen(COL_STATE_ACTIVE)) != 0;
    CHECK(no_active_collection);
}

static void test_install_receipt_appended() {
    MemoryInstallReader reader;
    reader.add_text("manifest.json", kMinimalManifest);

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    CHECK_OK(installer.run(reader, kv, log, policy, result));

    // The event log must have at least one INSTALL record.
    struct Ctx { int install_count; };
    Ctx ctx = {};
    log.iterate([](uint8_t type, uint32_t, const uint8_t* data, uint16_t len, void* cx) -> bool {
        auto* c = static_cast<Ctx*>(cx);
        if (type == ALOG_TYPE_INSTALL && data && len == sizeof(InstallReceiptData)) {
            const auto* rec = reinterpret_cast<const InstallReceiptData*>(data);
            if (rec->installed == 1u) ++c->install_count;
        }
        return true;
    }, &ctx);
    CHECK(ctx.install_count >= 1);
}

static void test_install_rejected_bad_manifest() {
    MemoryInstallReader reader;
    reader.add_text("manifest.json", kMissingCollectionIdManifest);

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    DiagStatus s = installer.run(reader, kv, log, policy, result);
    CHECK_FAIL(s);
    CHECK(!result.installed);

    // A failure receipt should be in the log.
    struct Ctx { int reject_count; };
    Ctx ctx = {};
    log.iterate([](uint8_t type, uint32_t, const uint8_t* data, uint16_t len, void* cx) -> bool {
        auto* c = static_cast<Ctx*>(cx);
        if (type == ALOG_TYPE_INSTALL && data && len == sizeof(InstallReceiptData)) {
            const auto* rec = reinterpret_cast<const InstallReceiptData*>(data);
            if (rec->installed == 0u) ++c->reject_count;
        }
        return true;
    }, &ctx);
    CHECK(ctx.reject_count >= 1);
}

static void test_install_direct_boot_persisted() {
    MemoryInstallReader reader;
    reader.add_text("manifest.json", kDirectBootManifest);

    PolicyStore policy = make_open_policy();
    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    Installer installer;
    InstallResult result = {};
    CHECK_OK(installer.run(reader, kv, log, policy, result));

    CollectionRecord rec = {}; uint16_t rec_len = 0;
    CHECK_OK(kv.get(KV_COL_RECORD,
                    reinterpret_cast<uint8_t*>(&rec), &rec_len, sizeof(rec)));
    CHECK_EQ(rec.boot_mode, 1u);  // direct
    CHECK(strcmp(rec.default_payload_id, "game") == 0);
    CHECK(strcmp(rec.version, "2.0.0") == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    // Manifest parsing
    test_manifest_parse_minimal();
    test_manifest_parse_direct_boot();
    test_manifest_parse_two_payloads();
    test_manifest_parse_unknown_fields_ignored();
    test_manifest_parse_missing_required_field();
    test_manifest_parse_wrong_format_version();
    test_manifest_parse_invalid_json();

    // bundle.sig parsing
    test_bundle_sig_parse();

    // Hash verification
    test_bundle_hash_verification_pass();
    test_bundle_hash_verification_fail();

    // Installer integration
    test_install_success();
    test_install_survives_reinit();
    test_install_power_loss_before_commit();
    test_install_receipt_appended();
    test_install_rejected_bad_manifest();
    test_install_direct_boot_persisted();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
