// test_content_store.cc — Host tests for Stage 10: ContentStore and
// mapping_plan_from_payload_record.

#include "content/collection_format.h"
#include "content/manifest.h"
#include "content/manifest_parser.h"
#include "content/content_store.h"
#include "content/installer.h"
#include "bus/mapping_plan.h"
#include "storage/fat_util.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "spine/otp_reader.h"
#include "crypto/sha256.h"
#include "fat_test_env.h"

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

// ---------------------------------------------------------------------------
// FAT write helpers
// ---------------------------------------------------------------------------

static void fat_put_active(const char* col_id) {
    fat_ensure_dir("1:/collections");
    fat_write_file("1:/collections/active.txt", col_id, strlen(col_id));
}

static void fat_put_collection(const CollectionRecord& rec) {
    char dir[128];
    snprintf(dir, sizeof(dir), "1:/collections/%s", rec.collection_id);
    fat_ensure_dir("1:/collections");
    fat_ensure_dir(dir);
    char path[192];
    snprintf(path, sizeof(path), "%s/collection.bin", dir);
    fat_write_file(path, &rec, sizeof(rec));
}

static void fat_put_payload(const char* col_id, const PayloadRecord& pr) {
    char dir[256];
    snprintf(dir, sizeof(dir), "1:/collections/%s", col_id);
    fat_ensure_dir("1:/collections");
    fat_ensure_dir(dir);
    char path[320];
    snprintf(path, sizeof(path), "1:/collections/%s/payload_%s.bin",
             col_id, pr.payload_id);
    fat_write_file(path, &pr, sizeof(pr));
}

// ---------------------------------------------------------------------------
// Record builder helpers
// ---------------------------------------------------------------------------

// Build a minimal CollectionRecord for testing.
static CollectionRecord make_col_record(const char* col_id,
                                         const char* default_payload_id) {
    CollectionRecord rec = {};
    strncpy(rec.collection_id,      col_id,             sizeof(rec.collection_id) - 1u);
    strncpy(rec.version,            "1.0.0",            sizeof(rec.version) - 1u);
    strncpy(rec.publisher_id,       "com.test",         sizeof(rec.publisher_id) - 1u);
    strncpy(rec.title,              "Test",             sizeof(rec.title) - 1u);
    strncpy(rec.default_payload_id, default_payload_id, sizeof(rec.default_payload_id) - 1u);
    rec.boot_mode     = 0;
    rec.payload_count = 1;
    return rec;
}

// Build a PayloadRecord for testing.
static PayloadRecord make_payload_record(const char* payload_id,
                                          const char* mapper_type,
                                          uint8_t subslot,
                                          uint32_t data_offset,
                                          uint32_t data_size) {
    PayloadRecord pr = {};
    strncpy(pr.payload_id,  payload_id,  sizeof(pr.payload_id) - 1u);
    strncpy(pr.mapper_type, mapper_type, sizeof(pr.mapper_type) - 1u);
    pr.subslot           = subslot;
    pr.data_flash_offset = data_offset;
    pr.data_size         = data_size;
    return pr;
}

// ---------------------------------------------------------------------------
// Tests: has_active_collection
// ---------------------------------------------------------------------------

static void test_has_active_empty_fat() {
    FatTestEnv env;
    ContentStore cs;
    CHECK(!cs.has_active_collection());
}

static void test_has_active_no_active_txt() {
    FatTestEnv env;
    // No active.txt written — collection dir may exist but commit marker absent.
    ContentStore cs;
    CHECK(!cs.has_active_collection());
}

static void test_has_active_true() {
    FatTestEnv env;
    fat_ensure_dir("1:/collections");
    fat_write_file("1:/collections/active.txt", "com.test.col", strlen("com.test.col"));
    ContentStore cs;
    CHECK(cs.has_active_collection());
}

// ---------------------------------------------------------------------------
// Tests: load_collection / load_payload round-trip
// ---------------------------------------------------------------------------

static void test_load_collection_roundtrip() {
    FatTestEnv env;
    CollectionRecord written = make_col_record("com.test.col", "main");
    fat_put_collection(written);
    fat_put_active("com.test.col");

    ContentStore cs;
    CollectionRecord read = {};
    CHECK_OK(cs.load_collection(read));
    CHECK(strcmp(read.collection_id, "com.test.col") == 0);
    CHECK(strcmp(read.default_payload_id, "main") == 0);
    CHECK_EQ(read.payload_count, 1);
}

static void test_load_payload_roundtrip() {
    FatTestEnv env;
    fat_put_active("com.test.col");
    PayloadRecord written = make_payload_record("main", "konami", 1, 0x200000u, 32768u);
    fat_put_payload("com.test.col", written);

    ContentStore cs;
    PayloadRecord read = {};
    CHECK_OK(cs.load_payload("main", read));
    CHECK(strcmp(read.payload_id, "main") == 0);
    CHECK(strcmp(read.mapper_type, "konami") == 0);
    CHECK_EQ(read.subslot, 1u);
    CHECK_EQ(read.data_flash_offset, 0x200000u);
    CHECK_EQ(read.data_size, 32768u);
}

static void test_load_payload_not_found() {
    FatTestEnv env;
    ContentStore cs;
    PayloadRecord pr = {};
    CHECK_FAIL(cs.load_payload("missing", pr));
}

// ---------------------------------------------------------------------------
// Tests: load_default_payload
// ---------------------------------------------------------------------------

static void test_load_default_payload() {
    FatTestEnv env;
    CollectionRecord col = make_col_record("com.test.col", "main");
    PayloadRecord pr     = make_payload_record("main", "ascii8", 0, 0x200000u, 65536u);
    fat_put_collection(col);
    fat_put_active("com.test.col");
    fat_put_payload("com.test.col", pr);

    ContentStore cs;
    PayloadRecord result = {};
    CHECK_OK(cs.load_default_payload(result));
    CHECK(strcmp(result.payload_id, "main") == 0);
    CHECK(strcmp(result.mapper_type, "ascii8") == 0);
    CHECK_EQ(result.data_size, 65536u);
}

static void test_load_default_payload_no_collection() {
    FatTestEnv env;
    ContentStore cs;
    PayloadRecord pr = {};
    CHECK_FAIL(cs.load_default_payload(pr));
}

static void test_load_default_payload_empty_id() {
    // Collection record present but default_payload_id is empty ("").
    FatTestEnv env;
    CollectionRecord col = {};  // default_payload_id[0] == '\0'
    strncpy(col.collection_id, "com.test.noid", sizeof(col.collection_id) - 1u);
    col.payload_count = 1;
    fat_put_collection(col);
    fat_put_active("com.test.noid");

    ContentStore cs;
    PayloadRecord pr = {};
    CHECK_FAIL(cs.load_default_payload(pr));
}

// ---------------------------------------------------------------------------
// Tests: mapping_plan_from_payload_record
// ---------------------------------------------------------------------------

static void test_mapping_plan_from_record_with_data() {
    PayloadRecord pr = make_payload_record("main", "konami", 2, 0x200000u, 32768u);
    MappingPlan plan = mapping_plan_from_payload_record(pr);
    CHECK_EQ(plan.entry_count, 1u);
    CHECK(plan.entries[0].mapper_type == MapperType::KONAMI);
    CHECK_EQ(plan.entries[0].subslot,    2u);
    CHECK_EQ(plan.entries[0].rom_size,   32768u);
    CHECK(!plan.expanded);
    // rom_data is always nullptr on host (XIP not available).
    CHECK(plan.entries[0].rom_data == nullptr);
}

static void test_mapping_plan_from_record_data_size_zero() {
    PayloadRecord pr = make_payload_record("main", "konami", 0, 0x200000u, 0u);
    MappingPlan plan = mapping_plan_from_payload_record(pr);
    CHECK_EQ(plan.entry_count, 0u);
}

static void test_mapping_plan_from_record_empty_mapper() {
    PayloadRecord pr = make_payload_record("main", "", 0, 0x200000u, 32768u);
    MappingPlan plan = mapping_plan_from_payload_record(pr);
    CHECK_EQ(plan.entry_count, 0u);
}

static void test_mapping_plan_all_mapper_types() {
    const struct { const char* name; MapperType expected; } cases[] = {
        { "rom",              MapperType::ROM              },
        { "rom_32k_mirrored", MapperType::ROM_32K_MIRRORED },
        { "konami",           MapperType::KONAMI           },
        { "konami_z",         MapperType::KONAMI_Z         },
        { "ascii8",           MapperType::ASCII8           },
        { "ascii16",          MapperType::ASCII16          },
        { "ram",              MapperType::RAM              },
    };
    for (const auto& c : cases) {
        PayloadRecord pr = make_payload_record("x", c.name, 0, 0x200000u, 8192u);
        MappingPlan plan = mapping_plan_from_payload_record(pr);
        CHECK_EQ(plan.entry_count, 1u);
        CHECK(plan.entries[0].mapper_type == c.expected);
    }
}

// ---------------------------------------------------------------------------
// Tests: Installer integration — PayloadRecord written to FAT
// ---------------------------------------------------------------------------

// Minimal InstallReader backed by in-memory files.
class MemInstallReader : public InstallReader {
    struct File { const char* path; const char* text; };
    File files_[8] = {};
    size_t count_ = 0;
public:
    void add(const char* path, const char* text) {
        files_[count_++] = {path, text};
    }
    DiagStatus read_file(const char* path, uint8_t* buf,
                          size_t max_len, size_t* out_len) override {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(files_[i].path, path) == 0) {
                size_t len = strlen(files_[i].text);
                if (len > max_len) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
                memcpy(buf, files_[i].text, len);
                *out_len = len;
                return DiagStatus::success();
            }
        }
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    DiagStatus hash_file(const char* path, uint8_t digest[SHA256_DIGEST_SIZE]) override {
        uint8_t buf[4096]; size_t len = 0;
        DiagStatus s = read_file(path, buf, sizeof(buf), &len);
        if (!s.ok()) return s;
        sha256(buf, len, digest);
        return DiagStatus::success();
    }
    bool file_exists(const char* path) override {
        for (size_t i = 0; i < count_; ++i) {
            if (strcmp(files_[i].path, path) == 0) return true;
        }
        return false;
    }
};

static const char kMapperManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.mapper\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"title\": \"Mapper Test\","
    "  \"payloads\": ["
    "    {\"payload_id\": \"main\", \"path\": \"payloads/game.rom\","
    "     \"title\": \"Main\", \"mapper_type\": \"konami\", \"subslot\": 1}"
    "  ]"
    "}";

static void test_installer_writes_payload_record() {
    FatTestEnv env;

    // Policy (open mode — accepts all; FakeOtpReader returns zeroed OTP).
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    SecurityPosture posture = SecurityPosture::read(otp);
    PolicyStore policy;
    policy.load(posture);  // fails gracefully; safe open defaults applied

    // Run installer.
    MemInstallReader reader;
    reader.add(BUNDLE_MANIFEST_FILE, kMapperManifest);

    InstallResult result = {};
    Installer installer;
    CHECK_OK(installer.run(reader, policy, result));
    CHECK(result.installed);

    // ContentStore should see the active collection.
    ContentStore cs;
    CHECK(cs.has_active_collection());

    // PayloadRecord must be present with mapper_type="konami", subslot=1.
    PayloadRecord pr = {};
    CHECK_OK(cs.load_payload("main", pr));
    CHECK(strcmp(pr.payload_id,  "main")   == 0);
    CHECK(strcmp(pr.mapper_type, "konami") == 0);
    CHECK_EQ(pr.subslot,           1u);
    CHECK_EQ(pr.data_size,         0u);  // ROM not yet written

    // load_default_payload via CollectionRecord.default_payload_id.
    // (Manifest has no explicit default_payload_id, so parser leaves it empty;
    //  first payload "main" is also the only payload — test load_payload instead.)
    MappingPlan plan = mapping_plan_from_payload_record(pr);
    // data_size == 0 → empty plan (bus wiring skipped).
    CHECK_EQ(plan.entry_count, 0u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_has_active_empty_fat();
    test_has_active_no_active_txt();
    test_has_active_true();
    test_load_collection_roundtrip();
    test_load_payload_roundtrip();
    test_load_payload_not_found();
    test_load_default_payload();
    test_load_default_payload_no_collection();
    test_load_default_payload_empty_id();
    test_mapping_plan_from_record_with_data();
    test_mapping_plan_from_record_data_size_zero();
    test_mapping_plan_from_record_empty_mapper();
    test_mapping_plan_all_mapper_types();
    test_installer_writes_payload_record();

    printf("RESULTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
