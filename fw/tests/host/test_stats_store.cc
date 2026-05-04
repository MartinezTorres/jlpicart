// test_stats_store.cc — host tests for StatsStore and UserStats service (Stage 21).

#include "stats/stats_store.h"
#include "identity/device_identity.h"
#include <openssl/evp.h>
#include "profiles/profile_store.h"
#include "storage/store.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"

#include "fat_test_env.h"
#include "storage/fat_util.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

struct StatsFixture {
    FatTestEnv   env;
    Store        store;
    ProfileStore ps;
    StatsStore   ss;

    StatsFixture() {
        store.init();
        ps.bind_store(store);
        ps.init();
        ss.bind_store(store);
        ss.init(ps);
    }
};

// ---------------------------------------------------------------------------
// test_stat_set_get — set stat 1=42; get returns 42; absent stat returns 0
// ---------------------------------------------------------------------------

static void test_stat_set_get()
{
    StatsFixture f;

    DiagStatus s = f.ss.stat_set(1u, "game-001", 1u, 42, 0u);
    CHECK(s.ok());

    int32_t val = -1;
    CHECK(f.ss.stat_get(1u, "game-001", 1u, &val).ok());
    CHECK(val == 42);

    // Absent stat returns 0 without error.
    int32_t absent = -1;
    CHECK(f.ss.stat_get(1u, "game-001", 99u, &absent).ok());
    CHECK(absent == 0);
}

// ---------------------------------------------------------------------------
// test_stat_add — set 10, add 5 → 15; add negative → 10
// ---------------------------------------------------------------------------

static void test_stat_add()
{
    StatsFixture f;

    f.ss.stat_set(1u, "game-001", 1u, 10, 0u);

    CHECK(f.ss.stat_set(1u, "game-001", 1u, 5, 1u).ok()); // add 5
    int32_t val = 0;
    CHECK(f.ss.stat_get(1u, "game-001", 1u, &val).ok());
    CHECK(val == 15);

    CHECK(f.ss.stat_set(1u, "game-001", 1u, -5, 1u).ok()); // add -5
    CHECK(f.ss.stat_get(1u, "game-001", 1u, &val).ok());
    CHECK(val == 10);
}

// ---------------------------------------------------------------------------
// test_stat_max — set 10, max(8) → stays 10; max(20) → 20
// ---------------------------------------------------------------------------

static void test_stat_max()
{
    StatsFixture f;

    f.ss.stat_set(1u, "game-001", 1u, 10, 0u);

    CHECK(f.ss.stat_set(1u, "game-001", 1u, 8, 2u).ok());
    int32_t val = 0;
    CHECK(f.ss.stat_get(1u, "game-001", 1u, &val).ok());
    CHECK(val == 10);

    CHECK(f.ss.stat_set(1u, "game-001", 1u, 20, 2u).ok());
    CHECK(f.ss.stat_get(1u, "game-001", 1u, &val).ok());
    CHECK(val == 20);
}

// ---------------------------------------------------------------------------
// test_ach_unlock — unlock; re-unlock is idempotent; absent returns 0
// ---------------------------------------------------------------------------

static void test_ach_unlock()
{
    StatsFixture f;

    // Unlock achievement 5.
    CHECK(f.ss.ach_unlock(1u, "game-001", 5u).ok());

    // Verify via ach_get (achievements are Store-backed, not FAT files).
    bool unlocked = false;
    CHECK(f.ss.ach_get(1u, "game-001", 5u, &unlocked).ok());
    CHECK(unlocked);

    // Re-unlock is idempotent.
    CHECK(f.ss.ach_unlock(1u, "game-001", 5u).ok());
    unlocked = false;
    CHECK(f.ss.ach_get(1u, "game-001", 5u, &unlocked).ok());
    CHECK(unlocked);
}

// ---------------------------------------------------------------------------
// test_leaderboard_submit — begin run → token; submit with score; entry written
// ---------------------------------------------------------------------------

static void test_leaderboard_submit()
{
    StatsFixture f;

    uint8_t token[STATS_TOKEN_LEN] = {};
    uint8_t handle = 0xFFu;
    CHECK(f.ss.leader_begin(1u, "game-001", 2u, token, &handle).ok());
    CHECK(handle < STATS_TOKEN_SLOTS);

    CHECK(f.ss.leader_submit(handle, 9999u, 0u, nullptr, 0u).ok());

    // Entry should be stored on FAT.
    LeaderEntry entry = {};
    size_t actual = 0u;
    CHECK(fat_read_file("1:/saves/0001/st_game-001_l_0002.bin", &entry, sizeof(entry), &actual));
    CHECK(actual == sizeof(LeaderEntry));
    CHECK(entry.score == 9999u);

    // Handle should be freed: try to allocate it again.
    uint8_t handle2 = 0xFFu;
    CHECK(f.ss.leader_begin(1u, "game-001", 2u, nullptr, &handle2).ok());
    CHECK(handle2 < STATS_TOKEN_SLOTS);
}

// ---------------------------------------------------------------------------
// test_stats_profile_isolation — stat under profile A not visible under profile B
// ---------------------------------------------------------------------------

static void test_stats_profile_isolation()
{
    StatsFixture f;

    f.ss.stat_set(1u, "game-001", 1u, 100, 0u);

    // Profile 2 should see 0 for the same stat/payload.
    int32_t val = -1;
    CHECK(f.ss.stat_get(2u, "game-001", 1u, &val).ok());
    CHECK(val == 0);
}

// ---------------------------------------------------------------------------
// test_stats_api_stat_roundtrip — UST_STAT_SET / UST_STAT_GET via ApiWindow
// ---------------------------------------------------------------------------

struct StatsApiFixture {
    FatTestEnv         env;
    Store              store;
    ProfileStore       ps;
    StatsStore         ss;
    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;

    StatsApiFixture() {
        store.init();
        ps.bind_store(store);
        ps.init();
        ss.bind_store(store);
        ss.init(ps);
        posture = {};
        policy_store.load(posture);
        registry.init(BoardDescriptor::for_current_board(),
                      kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        win.bind_profile_store(ps);
        win.bind_stats_store(ss);
        win.set_active_payload("test-payload");
    }

    void push_req(uint16_t seq, uint8_t method,
                  const uint8_t* payload, uint16_t plen)
    {
        uint8_t frame[sizeof(MsgHeader) + 32];
        MsgHeader hdr = {};
        hdr.seq         = seq;
        hdr.service     = SVC_USERSTATS;
        hdr.method      = method;
        hdr.payload_len = plen;
        hdr.scratch_ofs = 0xFFFFu;
        memcpy(frame, &hdr, sizeof(hdr));
        if (plen > 0 && payload) memcpy(frame + sizeof(hdr), payload, plen);
        win.ring_push_msg(API_REQ_RING_OFS, frame,
                          static_cast<uint16_t>(sizeof(hdr) + plen));
    }

    uint16_t pop_status(const uint8_t** payload_out = nullptr, uint16_t* plen_out = nullptr) {
        static uint8_t frame[sizeof(MsgHeader) + 32];
        uint16_t len = 0;
        bool got = win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &len);
        if (!got || len < sizeof(MsgHeader)) return 0xFFFFu;
        MsgHeader rsp;
        memcpy(&rsp, frame, sizeof(rsp));
        if (payload_out) *payload_out = frame + sizeof(MsgHeader);
        if (plen_out)    *plen_out    = rsp.payload_len;
        return rsp.status;
    }
};

static void test_stats_api_stat_roundtrip()
{
    StatsApiFixture f;

    // UST_STAT_SET: stat_id=3, value=77, op=0
    {
        uint8_t p[7] = {};
        uint16_t stat_id = 3u;
        int32_t  value   = 77;
        uint8_t  op      = 0u;
        memcpy(p + 0, &stat_id, 2u);
        memcpy(p + 2, &value,   4u);
        p[6] = op;
        f.push_req(1u, UST_STAT_SET, p, 7u);
        f.win.service_once();
        CHECK(f.pop_status() == API_OK);
    }

    // UST_STAT_GET: stat_id=3 → expect 77
    {
        uint8_t p[2] = {};
        uint16_t stat_id = 3u;
        memcpy(p, &stat_id, 2u);
        f.push_req(2u, UST_STAT_GET, p, 2u);
        f.win.service_once();

        const uint8_t* payload = nullptr;
        uint16_t       plen    = 0u;
        CHECK(f.pop_status(&payload, &plen) == API_OK);
        CHECK(plen == 4u);
        int32_t val = 0;
        memcpy(&val, payload, 4u);
        CHECK(val == 77);
    }
}

// ---------------------------------------------------------------------------
// ed25519_verify — OpenSSL helper (host-test only)
// ---------------------------------------------------------------------------

static bool ed25519_verify(const uint8_t* msg, size_t msg_len,
                            const uint8_t* sig_64, const uint8_t* pub_32)
{
    EVP_PKEY* key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                  pub_32, 32u);
    if (!key) return false;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) { EVP_PKEY_free(key); return false; }
    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, key) == 1)
        ok = (EVP_DigestVerify(ctx, sig_64, 64u, msg, msg_len) == 1);
    EVP_MD_CTX_free(ctx);
    EVP_PKEY_free(key);
    return ok;
}

// ---------------------------------------------------------------------------
// test_leaderboard_signed — leader_submit with DIK produces a valid signature
// ---------------------------------------------------------------------------

static void test_leaderboard_signed()
{
    FatTestEnv   env;
    Store        store;
    ProfileStore ps;
    StatsStore   ss;
    store.init();
    ps.bind_store(store);
    ps.init();
    ss.bind_store(store);
    ss.init(ps);

    DeviceIdentity dik;
    CHECK(dik.init_or_load().ok());
    CHECK(dik.initialized());

    uint8_t token[STATS_TOKEN_LEN] = {};
    uint8_t handle = 0xFFu;
    const uint16_t profile_id = 1u;
    const uint16_t lb_id      = 7u;
    CHECK(ss.leader_begin(profile_id, "game-002", lb_id, token, &handle).ok());
    CHECK(handle < STATS_TOKEN_SLOTS);

    const uint32_t score = 88888u;
    CHECK(ss.leader_submit(handle, score, 0u, nullptr, 0u, &dik).ok());

    // Read back the stored entry from FAT.
    char path[64];
    snprintf(path, sizeof(path), "1:/saves/%04x/st_game-002_l_%04x.bin",
             static_cast<unsigned>(profile_id),
             static_cast<unsigned>(lb_id));
    LeaderEntry entry = {};
    size_t actual = 0u;
    CHECK(fat_read_file(path, &entry, sizeof(entry), &actual));
    CHECK(actual == sizeof(LeaderEntry));
    CHECK(entry.score == score);

    // Signature must be non-zero.
    bool any_nonzero = false;
    for (size_t i = 0; i < LEADER_SIG_LEN; ++i)
        any_nonzero |= (entry.signature[i] != 0u);
    CHECK(any_nonzero);

    // Rebuild canonical message and verify with DIK public key.
    // Canonical layout: score(4) || timestamp(4) || token(16) || profile_id(2) || lb_id(2)
    uint8_t msg[28];
    memcpy(msg + 0,  &entry.score,     4u);
    memcpy(msg + 4,  &entry.timestamp, 4u);
    memcpy(msg + 8,  token,            STATS_TOKEN_LEN);
    memcpy(msg + 24, &profile_id,      2u);
    memcpy(msg + 26, &lb_id,           2u);

    uint8_t pub[DIK_PUB_KEY_LEN];
    dik.public_key(pub);
    CHECK(ed25519_verify(msg, sizeof(msg), entry.signature, pub));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_stat_set_get();
    test_stat_add();
    test_stat_max();
    test_ach_unlock();
    test_leaderboard_submit();
    test_stats_profile_isolation();
    test_stats_api_stat_roundtrip();
    test_leaderboard_signed();

    return test_summary();
}
