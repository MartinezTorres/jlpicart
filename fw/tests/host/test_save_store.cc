// test_save_store.cc — host tests for SaveStore and Storage service (Stage 19).

#include "store/save_store.h"
#include "store/profile_store.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "platform/platform.h"
#include "spine/driver_descriptor.h"

#include "fat_test_env.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

struct SaveFixture {
    FatTestEnv   env;
    ProfileStore ps;
    SaveStore    ss;

    SaveFixture() {
        ps.init();
        ss.init(ps);
    }

    uint8_t begin(uint16_t profile_id, uint16_t blob_id,
                  uint16_t total_len, uint16_t flags = 0u)
    {
        uint8_t h = 0xFF;
        ss.write_begin(profile_id, blob_id, total_len, flags, &h);
        return h;
    }

    DiagStatus commit_with_data(uint16_t profile_id, uint16_t blob_id,
                                const uint8_t* data, uint16_t len)
    {
        uint8_t h = begin(profile_id, blob_id, len);
        ss.write_chunk(h, 0, data, len);
        return ss.write_commit(h);
    }
};

// ---------------------------------------------------------------------------
// test_save_write_read — write blob, read back exact bytes
// ---------------------------------------------------------------------------

static void test_save_write_read()
{
    SaveFixture f;

    uint8_t data[8] = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4};
    DiagStatus s = f.commit_with_data(1u, 1u, data, sizeof(data));
    CHECK(s.ok());

    uint8_t out[8] = {};
    DiagStatus r = f.ss.read(1u, 1u, 0, out, sizeof(out));
    CHECK(r.ok());
    CHECK(memcmp(out, data, sizeof(data)) == 0);
}

// ---------------------------------------------------------------------------
// test_save_list — write two blobs; list returns both with correct sizes
// ---------------------------------------------------------------------------

static void test_save_list()
{
    SaveFixture f;

    uint8_t d1[4] = {1, 2, 3, 4};
    uint8_t d2[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    CHECK(f.commit_with_data(1u, 1u, d1, sizeof(d1)).ok());
    CHECK(f.commit_with_data(1u, 2u, d2, sizeof(d2)).ok());

    BlobInfo infos[4];
    uint8_t n = f.ss.list(1u, 0u, infos, 4u);
    CHECK(n == 2u);

    // Check that both blob IDs are present (order may vary).
    bool found1 = false, found2 = false;
    for (uint8_t i = 0; i < n; ++i) {
        if (infos[i].blob_id == 1u && infos[i].size == 4u)  found1 = true;
        if (infos[i].blob_id == 2u && infos[i].size == 8u)  found2 = true;
    }
    CHECK(found1);
    CHECK(found2);
}

// ---------------------------------------------------------------------------
// test_save_delete — write, delete, list returns 0; read returns NOT_FOUND
// ---------------------------------------------------------------------------

static void test_save_delete()
{
    SaveFixture f;

    uint8_t data[4] = {1, 2, 3, 4};
    CHECK(f.commit_with_data(1u, 1u, data, sizeof(data)).ok());

    CHECK(f.ss.delete_blob(1u, 1u).ok());

    BlobInfo infos[4];
    uint8_t n = f.ss.list(1u, 0u, infos, 4u);
    CHECK(n == 0u);

    uint8_t out[4];
    DiagStatus r = f.ss.read(1u, 1u, 0, out, sizeof(out));
    CHECK(r.code == DiagCode::STORAGE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// test_save_profile_isolation — blob under profile A not visible under profile B
// ---------------------------------------------------------------------------

static void test_save_profile_isolation()
{
    SaveFixture f;

    uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    CHECK(f.commit_with_data(1u, 1u, data, sizeof(data)).ok());

    // Profile 2 should see no blobs.
    BlobInfo infos[4];
    uint8_t n = f.ss.list(2u, 0u, infos, 4u);
    CHECK(n == 0u);

    // Read from profile 2 should fail.
    uint8_t out[4];
    DiagStatus r = f.ss.read(2u, 1u, 0, out, sizeof(out));
    CHECK(r.code == DiagCode::STORAGE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// test_save_concurrent_writes — SAVE_WRITE_HANDLES simultaneous writes
// ---------------------------------------------------------------------------

static void test_save_concurrent_writes()
{
    SaveFixture f;

    uint8_t handles[SAVE_WRITE_HANDLES];
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        uint8_t data[4] = {static_cast<uint8_t>(i), 0, 0, 0};
        uint8_t h = 0xFF;
        DiagStatus s = f.ss.write_begin(1u, static_cast<uint16_t>(i + 1),
                                        4u, 0u, &h);
        CHECK(s.ok());
        handles[i] = h;
        f.ss.write_chunk(h, 0, data, 4u);
    }

    // One more should fail.
    uint8_t h_extra = 0xFF;
    DiagStatus extra = f.ss.write_begin(1u, 99u, 4u, 0u, &h_extra);
    CHECK(extra.code == DiagCode::STORAGE_FULL);

    // Commit all.
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        CHECK(f.ss.write_commit(handles[i]).ok());
    }
}

// ---------------------------------------------------------------------------
// test_storage_api_roundtrip — write/read via ApiWindow framing
// ---------------------------------------------------------------------------

struct ApiFixture2 {
    FatTestEnv        env;
    ProfileStore      ps;
    SaveStore         ss;
    SecurityPosture   posture;
    PolicyStore       policy_store;
    CapabilityRegistry registry;
    ApiWindow         win;

    ApiFixture2() {
        ps.init();
        ss.init(ps);
        posture = {};
        policy_store.load(posture);
        registry.init(BoardDescriptor::for_current_board(),
                      kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        win.bind_profile_store(ps);
        win.bind_save_store(ss);
    }

    void push_req(uint16_t seq, uint8_t method,
                  const uint8_t* payload, uint16_t plen)
    {
        uint8_t frame[sizeof(MsgHeader) + 64];
        MsgHeader hdr = {};
        hdr.seq         = seq;
        hdr.service     = SVC_STORAGE;
        hdr.method      = method;
        hdr.payload_len = plen;
        hdr.scratch_ofs = 0xFFFFu;
        memcpy(frame, &hdr, sizeof(hdr));
        if (plen > 0 && payload) memcpy(frame + sizeof(hdr), payload, plen);
        win.ring_push_msg(API_REQ_RING_OFS, frame,
                          static_cast<uint16_t>(sizeof(hdr) + plen));
    }

    uint16_t pop_status() {
        uint8_t frame[sizeof(MsgHeader) + 32];
        uint16_t len = 0;
        bool got = win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &len);
        if (!got || len < sizeof(MsgHeader)) return 0xFFFFu;
        MsgHeader rsp;
        memcpy(&rsp, frame, sizeof(rsp));
        return rsp.status;
    }
};

static void test_storage_api_roundtrip()
{
    ApiFixture2 f;

    // WRITE_BEGIN: blob_id=1, total_len=4, flags=0
    {
        uint8_t p[6] = {};
        uint16_t blob_id = 1u, total_len = 4u, flags = 0u;
        memcpy(p + 0, &blob_id,   2);
        memcpy(p + 2, &total_len, 2);
        memcpy(p + 4, &flags,     2);
        f.push_req(1u, STG_WRITE_BLOB_BEGIN, p, 6u);
        f.win.service_once();
        CHECK(f.pop_status() == API_OK);
    }

    // WRITE_CHUNK: handle=0, offset=0, len=4; data in h2c scratch
    {
        uint8_t* scratch = f.win.buf() + API_H2C_SCRATCH_OFS;
        scratch[0] = 0xCA;
        scratch[1] = 0xFE;
        scratch[2] = 0xBA;
        scratch[3] = 0xBE;

        uint8_t frame[sizeof(MsgHeader) + 8];
        MsgHeader hdr = {};
        hdr.seq         = 2u;
        hdr.service     = SVC_STORAGE;
        hdr.method      = STG_WRITE_BLOB_CHUNK;
        hdr.payload_len = 8u;
        hdr.scratch_ofs = 0u;
        hdr.scratch_len = 4u;
        uint8_t p[8] = {};
        uint16_t handle = 0u, len = 4u; uint32_t offset = 0u;
        memcpy(p + 0, &handle, 2);
        memcpy(p + 2, &offset, 4);
        memcpy(p + 6, &len,    2);
        memcpy(frame, &hdr, sizeof(hdr));
        memcpy(frame + sizeof(hdr), p, 8u);
        f.win.ring_push_msg(API_REQ_RING_OFS, frame,
                            static_cast<uint16_t>(sizeof(hdr) + 8u));
        f.win.service_once();
        CHECK(f.pop_status() == API_OK);
    }

    // WRITE_COMMIT: handle=0
    {
        uint8_t p[2] = {0, 0};
        f.push_req(3u, STG_WRITE_BLOB_COMMIT, p, 2u);
        f.win.service_once();
        CHECK(f.pop_status() == API_OK);
    }

    // READ_BLOB: blob_id=1, offset=0, len=4
    {
        uint8_t p[8] = {};
        uint16_t blob_id = 1u, len = 4u; uint32_t offset = 0u;
        memcpy(p + 0, &blob_id, 2);
        memcpy(p + 2, &offset,  4);
        memcpy(p + 6, &len,     2);
        f.push_req(4u, STG_READ_BLOB, p, 8u);
        f.win.service_once();

        uint8_t frame[sizeof(MsgHeader)];
        uint16_t flen = 0;
        bool got = f.win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &flen);
        CHECK(got);
        MsgHeader rsp;
        memcpy(&rsp, frame, sizeof(rsp));
        CHECK(rsp.status == API_OK);
        CHECK(rsp.scratch_len == 4u);

        // Verify data in c2h scratch.
        const uint8_t* out = f.win.buf() + API_C2H_SCRATCH_OFS;
        CHECK(out[0] == 0xCA);
        CHECK(out[1] == 0xFE);
        CHECK(out[2] == 0xBA);
        CHECK(out[3] == 0xBE);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_save_write_read();
    test_save_list();
    test_save_delete();
    test_save_profile_isolation();
    test_save_concurrent_writes();
    test_storage_api_roundtrip();

    return test_summary();
}
