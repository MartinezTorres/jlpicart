// test_policy_enforcement.cc — host tests for Stage 26 policy flag enforcement.
//
// Verifies that the three policy gates added in Stage 26 actually block or
// permit operations according to the active PolicyStore flags:
//
//   POLICY_EXPOSE_STABLE_DEVICE_ID  — GET_DEVICE_ID scope=0 (core_service)
//   POLICY_ALLOW_USB_COLLECTION_INSTALL — UsbInstallScanner::run_scan()
//   POLICY_ALLOW_UNSIGNED_COLLECTIONS   — Installer::run() unsigned path

#include "identity/device_identity.h"
#include "msx/api/api_window.h"
#include "msx/api/services/core_service.h"
#include "msx/api/api_types.h"
#include "spine/capability_registry.h"
#include "spine/policy_store.h"
#include "spine/security_posture.h"
#include "policy/policy_types.h"
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"
#include "security/otp_reader.h"
#include "content/installer.h"
#include "content/manifest.h"
#include "content/collection_format.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
#include "usb/usb_host.h"
#include "usb/usb_install_scanner.h"
#include "crypto/sha256.h"
#include "profiles/profile_store.h"

#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Common sizes
// ---------------------------------------------------------------------------

static constexpr uint32_t TEST_FLASH_SIZE = FLASH_SECTOR_SIZE * 8u;
static constexpr uint32_t TEST_KV_SIZE    = FLASH_SECTOR_SIZE * 4u;
static constexpr uint32_t TEST_LOG_SIZE   = FLASH_SECTOR_SIZE * 4u;

// ---------------------------------------------------------------------------
// PolicyStore factory — DEV posture, explicit flags (HMAC skipped in DEV mode)
// ---------------------------------------------------------------------------

static SecurityPosture make_dev_posture() {
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    return SecurityPosture::read(otp);
}

static PolicyStore make_policy(PolicyFlags flags) {
    SecurityPosture posture = make_dev_posture();
    PolicyDocument doc = {};
    doc.version = POLICY_VERSION_V1;
    doc.flags   = flags;
    // hmac_tag zeroed — accepted in DEV mode (secure_boot_enabled = false)
    PolicyStore ps;
    ps.load_from_buffer(reinterpret_cast<const uint8_t*>(&doc), sizeof(doc), posture);
    return ps;
}

// ---------------------------------------------------------------------------
// MemoryInstallReader — in-memory InstallReader for installer tests
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
private:
    MemFile files_[8] = {};
    size_t  count_ = 0;
    const MemFile* find(const char* path) const {
        for (size_t i = 0; i < count_; ++i)
            if (strcmp(files_[i].path, path) == 0) return &files_[i];
        return nullptr;
    }
};

// MemInstallDirSource — single-reader InstallDirSource for scanner tests
class MemInstallDirSource : public InstallDirSource {
public:
    void add_dir(const char* name, MemoryInstallReader* r) {
        names_[count_] = name; readers_[count_] = r; ++count_;
    }
    size_t count() const override { return count_; }
    const char* dir_name(size_t i) const override { return i < count_ ? names_[i] : ""; }
    InstallReader& open_reader(size_t i) override { return *readers_[i]; }
private:
    const char*          names_[8]   = {};
    MemoryInstallReader* readers_[8] = {};
    size_t               count_ = 0;
};

// FakeUsbHost — always reports MSC as mounted
class FakeUsbHost : public UsbHost {
public:
    bool is_msc_mounted() const override { return true; }
};

// ---------------------------------------------------------------------------
// Minimal manifest (no bundle.sig — unsigned collection)
// ---------------------------------------------------------------------------

static const char kManifest[] =
    "{"
    "  \"format_version\": \"1.0\","
    "  \"collection_id\": \"com.test.enforce\","
    "  \"version\": \"1.0.0\","
    "  \"publisher\": {\"publisher_id\": \"com.test\", \"name\": \"Test\"},"
    "  \"payloads\": [{\"payload_id\": \"main\", \"path\": \"main.rom\"}]"
    "}";

// ---------------------------------------------------------------------------
// ApiWindow fixture for GET_DEVICE_ID tests
// ---------------------------------------------------------------------------

struct PolicyApiFixture {
    FlashDevice        kv_flash;
    FlashDevice        ps_flash;
    KvStore            kv;
    ProfileStore       ps;
    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;
    DeviceIdentity     dik;

    explicit PolicyApiFixture(PolicyFlags flags)
        : kv_flash(TEST_FLASH_SIZE)
        , ps_flash(TEST_FLASH_SIZE)
    {
        kv.init(kv_flash, 0u, TEST_FLASH_SIZE);
        ps.init(ps_flash, 0u, TEST_FLASH_SIZE);
        posture      = {};
        policy_store = make_policy(flags);
        registry.init(BoardDescriptor::for_current_board(),
                      kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        win.bind_profile_store(ps);
        dik.init_or_load(kv);
        win.bind_device_identity(dik);
    }

    uint16_t get_device_id(uint8_t scope, uint8_t* out_16 = nullptr) {
        uint8_t frame[sizeof(MsgHeader) + 1];
        MsgHeader hdr = {};
        hdr.seq         = 1u;
        hdr.service     = SVC_SYSTEM;
        hdr.method      = SYS_GET_DEVICE_ID;
        hdr.payload_len = 1u;
        hdr.scratch_ofs = 0xFFFFu;
        memcpy(frame, &hdr, sizeof(hdr));
        frame[sizeof(hdr)] = scope;
        win.ring_push_msg(API_REQ_RING_OFS, frame,
                          static_cast<uint16_t>(sizeof(hdr) + 1u));
        win.service_once();

        uint8_t rsp_frame[sizeof(MsgHeader) + 32];
        uint16_t rlen = 0u;
        if (!win.ring_pop_msg(API_RSP_RING_OFS, rsp_frame,
                               sizeof(rsp_frame), &rlen))
            return 0xFFFFu;
        MsgHeader rsp;
        memcpy(&rsp, rsp_frame, sizeof(rsp));
        if (rsp.status == API_OK && rsp.payload_len == 16u && out_16)
            memcpy(out_16, rsp_frame + sizeof(MsgHeader), 16u);
        return rsp.status;
    }
};

// ---------------------------------------------------------------------------
// test_scope0_blocked_without_flag
//   POLICY_EXPOSE_STABLE_DEVICE_ID absent → GET_DEVICE_ID(scope=0) = API_E_POLICY
// ---------------------------------------------------------------------------

static void test_scope0_blocked_without_flag()
{
    // No POLICY_EXPOSE_STABLE_DEVICE_ID in policy flags.
    PolicyApiFixture f(POLICY_ALLOW_UNSIGNED_COLLECTIONS |
                       POLICY_ALLOW_USB_COLLECTION_INSTALL);

    CHECK(f.get_device_id(0u) == API_E_POLICY);
}

// ---------------------------------------------------------------------------
// test_scope0_allowed_with_flag
//   POLICY_EXPOSE_STABLE_DEVICE_ID set → GET_DEVICE_ID(scope=0) = API_OK
// ---------------------------------------------------------------------------

static void test_scope0_allowed_with_flag()
{
    PolicyApiFixture f(POLICY_EXPOSE_STABLE_DEVICE_ID |
                       POLICY_ALLOW_UNSIGNED_COLLECTIONS |
                       POLICY_ALLOW_USB_COLLECTION_INSTALL);

    uint8_t id[16] = {};
    CHECK(f.get_device_id(0u, id) == API_OK);
}

// ---------------------------------------------------------------------------
// test_scope1_always_allowed
//   scope=1 is not gated by POLICY_EXPOSE_STABLE_DEVICE_ID
// ---------------------------------------------------------------------------

static void test_scope1_always_allowed()
{
    // No POLICY_EXPOSE_STABLE_DEVICE_ID — scope 1 must still succeed.
    PolicyApiFixture f(POLICY_ALLOW_UNSIGNED_COLLECTIONS |
                       POLICY_ALLOW_USB_COLLECTION_INSTALL);

    uint8_t id[16] = {};
    CHECK(f.get_device_id(1u, id) == API_OK);
}

// ---------------------------------------------------------------------------
// test_usb_install_blocked_without_flag
//   POLICY_ALLOW_USB_COLLECTION_INSTALL absent → run_scan() skips all dirs
// ---------------------------------------------------------------------------

static void test_usb_install_blocked_without_flag()
{
    PolicyStore policy = make_policy(POLICY_ALLOW_UNSIGNED_COLLECTIONS);
    // No POLICY_ALLOW_USB_COLLECTION_INSTALL

    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0u, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifest);

    MemInstallDirSource dirs;
    dirs.add_dir("test", &reader);

    FakeUsbHost usb_host;
    UsbInstallScanner scanner(usb_host);
    scanner.run_scan(dirs, kv, log, policy);

    // Nothing installed — KV_COL_STATE must be absent.
    CHECK(!kv.contains(KV_COL_STATE));
    CHECK(!kv.contains(KV_COL_RECORD));
}

// ---------------------------------------------------------------------------
// test_usb_install_allowed_with_flag
//   POLICY_ALLOW_USB_COLLECTION_INSTALL set → run_scan() installs collection
// ---------------------------------------------------------------------------

static void test_usb_install_allowed_with_flag()
{
    PolicyStore policy = make_policy(POLICY_ALLOW_USB_COLLECTION_INSTALL |
                                     POLICY_ALLOW_UNSIGNED_COLLECTIONS);

    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0u, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifest);

    MemInstallDirSource dirs;
    dirs.add_dir("test", &reader);

    FakeUsbHost usb_host;
    UsbInstallScanner scanner(usb_host);
    scanner.run_scan(dirs, kv, log, policy);

    // Collection should be installed and active.
    CHECK(kv.contains(KV_COL_STATE));
    CHECK(kv.contains(KV_COL_RECORD));

    uint8_t state[16] = {}; uint16_t slen = 0;
    kv.get(KV_COL_STATE, state, &slen, sizeof(state));
    CHECK(memcmp(state, COL_STATE_ACTIVE, strlen(COL_STATE_ACTIVE)) == 0);
}

// ---------------------------------------------------------------------------
// test_unsigned_collection_blocked_without_flag
//   POLICY_ALLOW_UNSIGNED_COLLECTIONS absent and no bundle.sig → UNSATISFIED_REQ
// ---------------------------------------------------------------------------

static void test_unsigned_collection_blocked_without_flag()
{
    PolicyStore policy = make_policy(POLICY_ALLOW_USB_COLLECTION_INSTALL);
    // No POLICY_ALLOW_UNSIGNED_COLLECTIONS

    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0u, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    // No bundle.sig added to reader

    Installer installer;
    InstallResult result = {};
    DiagStatus s = installer.run(reader, kv, log, policy, result);

    CHECK(!s.ok());
    CHECK(!result.installed);
    CHECK(result.reason == DiagCode::COLLECTION_UNSATISFIED_REQ);
}

// ---------------------------------------------------------------------------
// test_unsigned_collection_allowed_with_flag
//   POLICY_ALLOW_UNSIGNED_COLLECTIONS set → unsigned install succeeds
// ---------------------------------------------------------------------------

static void test_unsigned_collection_allowed_with_flag()
{
    PolicyStore policy = make_policy(POLICY_ALLOW_UNSIGNED_COLLECTIONS |
                                     POLICY_ALLOW_USB_COLLECTION_INSTALL);

    FlashDevice flash(TEST_KV_SIZE + TEST_LOG_SIZE);
    KvStore kv;  kv.init(flash, 0u, TEST_KV_SIZE);
    AppendLog log; log.init(flash, TEST_KV_SIZE, TEST_LOG_SIZE);

    MemoryInstallReader reader;
    reader.add_text("manifest.json", kManifest);
    // No bundle.sig — should be allowed by policy

    Installer installer;
    InstallResult result = {};
    DiagStatus s = installer.run(reader, kv, log, policy, result);

    CHECK(s.ok());
    CHECK(result.installed);
    CHECK(strcmp(result.collection_id, "com.test.enforce") == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_scope0_blocked_without_flag();
    test_scope0_allowed_with_flag();
    test_scope1_always_allowed();
    test_usb_install_blocked_without_flag();
    test_usb_install_allowed_with_flag();
    test_unsigned_collection_blocked_without_flag();
    test_unsigned_collection_allowed_with_flag();

    return test_summary();
}
