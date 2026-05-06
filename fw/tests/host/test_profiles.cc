// test_profiles.cc — host tests for profile operations and Identity API service.
//
// Tests cover:
//   UserDataStore profiles: create / list / get / remove / set_active
//   Identity service 0x03: LIST_PROFILES, SET/GET_ACTIVE_PROFILE

#include "store/user_data_store.h"
#include "msx/api/api_types.h"
#include "msx/api/api_window.h"
#include "msx/api/services/services.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "platform/platform.h"
#include "spine/driver_descriptor.h"
#include "spine/otp_reader.h"

#include "fat_test_env.h"
#include "test_helpers.h"
#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Shared harness helpers
// ---------------------------------------------------------------------------

struct ProfileFixture {
    FatTestEnv    env;
    UserDataStore uds;

    ProfileFixture() {
        DiagStatus s = uds.init();
        assert(s.ok());
    }
};

// ---------------------------------------------------------------------------
// test_profile_create_list — create 3 profiles, list returns 3 distinct IDs
// ---------------------------------------------------------------------------

static void test_profile_create_list() {
    ProfileFixture f;

    uint16_t id1 = 0, id2 = 0, id3 = 0;
    CHECK(f.uds.profile_create("Alice", "en", &id1).ok());
    CHECK(f.uds.profile_create("Bob",   "fr", &id2).ok());
    CHECK(f.uds.profile_create("Carol", "de", &id3).ok());

    CHECK(id1 != 0u);
    CHECK(id2 != 0u);
    CHECK(id3 != 0u);
    CHECK(id1 != id2);
    CHECK(id2 != id3);
    CHECK(id1 != id3);

    CHECK(f.uds.profile_count() == 3u);

    ProfileRecord recs[8];
    uint8_t n = f.uds.profile_list(recs, 8);
    CHECK(n == 3u);
}

// ---------------------------------------------------------------------------
// test_profile_get — get by known ID returns correct name
// ---------------------------------------------------------------------------

static void test_profile_get() {
    ProfileFixture f;

    uint16_t id = 0;
    CHECK(f.uds.profile_create("Dave", "en", &id).ok());

    ProfileRecord rec = {};
    CHECK(f.uds.profile_get(id, &rec).ok());
    CHECK(strcmp(rec.name, "Dave") == 0);
    CHECK(strcmp(rec.lang, "en")   == 0);
    CHECK(rec.profile_id == id);
}

// ---------------------------------------------------------------------------
// test_profile_get_not_found — get with bad ID returns STORAGE_NOT_FOUND
// ---------------------------------------------------------------------------

static void test_profile_get_not_found() {
    ProfileFixture f;
    ProfileRecord rec = {};
    DiagStatus s = f.uds.profile_get(42u, &rec);
    CHECK(!s.ok());
    CHECK(s.code == DiagCode::STORAGE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// test_profile_active — set_active/active round-trip; PROF_ID_NONE on fresh store
// ---------------------------------------------------------------------------

static void test_profile_active() {
    ProfileFixture f;

    CHECK(f.uds.profile_active() == PROF_ID_NONE);

    uint16_t id = 0;
    CHECK(f.uds.profile_create("Eve", "ja", &id).ok());

    CHECK(f.uds.profile_set_active(id).ok());
    CHECK(f.uds.profile_active() == id);

    CHECK(f.uds.profile_set_active(PROF_ID_NONE).ok());
    CHECK(f.uds.profile_active() == PROF_ID_NONE);
}

// ---------------------------------------------------------------------------
// test_profile_active_persists — active ID survives a re-init of the store
// ---------------------------------------------------------------------------

static void test_profile_active_persists() {
    FatTestEnv env;
    uint16_t id = 0;

    {
        UserDataStore uds;
        CHECK(uds.init().ok());
        CHECK(uds.profile_create("Frank", "es", &id).ok());
        CHECK(uds.profile_set_active(id).ok());
    }

    {
        UserDataStore uds2;
        CHECK(uds2.init().ok());
        CHECK(uds2.profile_active() == id);
        ProfileRecord rec = {};
        CHECK(uds2.profile_get(id, &rec).ok());
        CHECK(strcmp(rec.name, "Frank") == 0);
    }
}

// ---------------------------------------------------------------------------
// test_profile_remove — remove shrinks list; removing active resets to NONE
// ---------------------------------------------------------------------------

static void test_profile_remove() {
    ProfileFixture f;

    uint16_t id1 = 0, id2 = 0;
    CHECK(f.uds.profile_create("Grace", "en", &id1).ok());
    CHECK(f.uds.profile_create("Heidi", "en", &id2).ok());

    CHECK(f.uds.profile_set_active(id1).ok());
    CHECK(f.uds.profile_remove(id1).ok());

    CHECK(f.uds.profile_count() == 1u);
    CHECK(f.uds.profile_active() == PROF_ID_NONE);

    ProfileRecord rec = {};
    CHECK(f.uds.profile_get(id2, &rec).ok());
    CHECK(strcmp(rec.name, "Heidi") == 0);

    CHECK(!f.uds.profile_get(id1, &rec).ok());
}

// ---------------------------------------------------------------------------
// test_profile_full — 9th create returns STORAGE_FULL
// ---------------------------------------------------------------------------

static void test_profile_full() {
    ProfileFixture f;

    for (uint8_t i = 0; i < PROF_MAX_PROFILES; ++i) {
        char name[8];
        snprintf(name, sizeof(name), "u%d", i);
        uint16_t id = 0;
        CHECK(f.uds.profile_create(name, "en", &id).ok());
    }
    CHECK(f.uds.profile_count() == PROF_MAX_PROFILES);

    uint16_t id = 0;
    DiagStatus s = f.uds.profile_create("overflow", "en", &id);
    CHECK(!s.ok());
    CHECK(s.code == DiagCode::STORAGE_FULL);
}

// ---------------------------------------------------------------------------
// Identity service helpers
// ---------------------------------------------------------------------------

static SecurityPosture   g_posture;
static PolicyStore       g_policy;
static CapabilityRegistry g_registry;
static bool g_spine_ready = false;

static void ensure_spine() {
    if (g_spine_ready) return;
    static const uint8_t kZeroOtp[256] = {};
    FakeOtpReader otp(kZeroOtp, sizeof(kZeroOtp));
    g_posture = SecurityPosture::read(otp);
    g_policy.load(g_posture);
    g_registry.init(BoardDescriptor::for_current_board(),
                    kDriverDescriptors, kDriverDescriptorCount,
                    g_policy.info());
    g_spine_ready = true;
}

static bool pop_response(ApiWindow& win, uint16_t* status_out,
                          uint8_t* payload_out, uint16_t payload_max,
                          uint16_t* payload_len_out)
{
    uint8_t  frame[API_MAX_MSG];
    uint16_t msg_len = 0;
    if (!win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &msg_len))
        return false;
    if (msg_len < sizeof(MsgHeader)) return false;

    MsgHeader hdr;
    memcpy(&hdr, frame, sizeof(MsgHeader));
    if (status_out)      *status_out      = hdr.status;
    if (payload_len_out) *payload_len_out  = hdr.payload_len;
    if (payload_out && hdr.payload_len > 0) {
        uint16_t copy = hdr.payload_len < payload_max ? hdr.payload_len : payload_max;
        memcpy(payload_out, frame + sizeof(MsgHeader), copy);
    }
    return true;
}

static void push_request(ApiWindow& win, uint16_t seq,
                          uint8_t service, uint8_t method,
                          const uint8_t* payload, uint16_t payload_len)
{
    uint8_t msg[API_MAX_MSG];
    MsgHeader hdr = {};
    hdr.seq         = seq;
    hdr.service     = service;
    hdr.method      = method;
    hdr.payload_len = payload_len;
    hdr.scratch_ofs = 0xFFFFu;
    memcpy(msg, &hdr, sizeof(MsgHeader));
    if (payload_len > 0 && payload)
        memcpy(msg + sizeof(MsgHeader), payload, payload_len);
    win.ring_push_msg(API_REQ_RING_OFS, msg,
                      static_cast<uint16_t>(sizeof(MsgHeader) + payload_len));
}

// ---------------------------------------------------------------------------
// test_identity_list — LIST_PROFILES encodes correct count and first entry name
// ---------------------------------------------------------------------------

static void test_identity_list() {
    ensure_spine();

    FatTestEnv    env;
    UserDataStore uds;
    CHECK(uds.init().ok());

    uint16_t id1 = 0, id2 = 0;
    CHECK(uds.profile_create("Ivan", "en", &id1).ok());
    CHECK(uds.profile_create("Judy", "fr", &id2).ok());

    static ApiWindow win;
    win.init(g_posture, g_policy, g_registry);
    win.bind_user_data(uds);

    CHECK(win.initialized());

    push_request(win, 1u, SVC_IDENTITY, IDN_LIST_PROFILES, nullptr, 0);
    CHECK(win.service_once());

    uint16_t status = 0xFFFFu;
    uint8_t  payload[64] = {};
    uint16_t payload_len  = 0;
    CHECK(pop_response(win, &status, payload, sizeof(payload), &payload_len));
    CHECK(status == API_OK);

    uint16_t count = static_cast<uint16_t>(payload[0] | (payload[1] << 8u));
    CHECK(count == 2u);

    uint16_t entry_id = static_cast<uint16_t>(payload[2] | (payload[3] << 8u));
    CHECK(entry_id == id1);
    uint8_t name_len = payload[4];
    CHECK(name_len == 4u);
    CHECK(memcmp(payload + 5, "Ivan", 4) == 0);
}

// ---------------------------------------------------------------------------
// test_identity_set_get_active — SET then GET round-trip via API rings
// ---------------------------------------------------------------------------

static void test_identity_set_get_active() {
    ensure_spine();

    FatTestEnv    env;
    UserDataStore uds;
    CHECK(uds.init().ok());

    uint16_t id = 0;
    CHECK(uds.profile_create("Karl", "de", &id).ok());

    static ApiWindow win2;
    win2.init(g_posture, g_policy, g_registry);
    win2.bind_user_data(uds);

    uint8_t set_payload[2];
    set_payload[0] = static_cast<uint8_t>(id & 0xFFu);
    set_payload[1] = static_cast<uint8_t>(id >> 8u);
    push_request(win2, 2u, SVC_IDENTITY, IDN_SET_ACTIVE_PROFILE, set_payload, 2u);
    CHECK(win2.service_once());

    uint16_t status = 0xFFFFu;
    CHECK(pop_response(win2, &status, nullptr, 0, nullptr));
    CHECK(status == API_OK);

    push_request(win2, 3u, SVC_IDENTITY, IDN_GET_ACTIVE_PROFILE, nullptr, 0u);
    CHECK(win2.service_once());

    uint8_t  get_payload[4] = {};
    uint16_t get_len = 0;
    CHECK(pop_response(win2, &status, get_payload, sizeof(get_payload), &get_len));
    CHECK(status == API_OK);
    CHECK(get_len == 3u);

    uint16_t active_id = static_cast<uint16_t>(get_payload[0] | (get_payload[1] << 8u));
    CHECK(active_id == id);
}

// ---------------------------------------------------------------------------
// test_identity_set_active_not_found — SET with unknown ID → API_E_NOT_FOUND
// ---------------------------------------------------------------------------

static void test_identity_set_active_not_found() {
    ensure_spine();

    FatTestEnv    env;
    UserDataStore uds;
    CHECK(uds.init().ok());

    static ApiWindow win3;
    win3.init(g_posture, g_policy, g_registry);
    win3.bind_user_data(uds);

    uint8_t payload[2] = { 0x42, 0x00 };
    push_request(win3, 4u, SVC_IDENTITY, IDN_SET_ACTIVE_PROFILE, payload, 2u);
    CHECK(win3.service_once());

    uint16_t status = 0u;
    CHECK(pop_response(win3, &status, nullptr, 0, nullptr));
    CHECK(status == API_E_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// test_identity_no_store — SVC_IDENTITY without bound store → API_E_UNSUPPORTED
// ---------------------------------------------------------------------------

static void test_identity_no_store() {
    ensure_spine();

    static ApiWindow win4;
    win4.init(g_posture, g_policy, g_registry);
    // Deliberately do NOT call bind_user_data.

    push_request(win4, 5u, SVC_IDENTITY, IDN_LIST_PROFILES, nullptr, 0u);
    CHECK(win4.service_once());

    uint16_t status = 0u;
    CHECK(pop_response(win4, &status, nullptr, 0, nullptr));
    CHECK(status == API_E_UNSUPPORTED);
}

// ---------------------------------------------------------------------------
// test_guest_session_begin_end — begin_guest sets PROF_ID_GUEST; end restores
// ---------------------------------------------------------------------------

static void test_guest_session_begin_end() {
    ProfileFixture f;

    uint16_t id = 0;
    CHECK(f.uds.profile_create("Leo", "en", &id).ok());
    CHECK(f.uds.profile_set_active(id).ok());
    CHECK(f.uds.profile_active() == id);

    f.uds.profile_begin_guest();
    CHECK(f.uds.profile_active() == PROF_ID_GUEST);
    CHECK(f.uds.profile_in_guest_session());

    f.uds.profile_end_guest();
    CHECK(f.uds.profile_active() == id);
    CHECK(!f.uds.profile_in_guest_session());
}

// ---------------------------------------------------------------------------
// test_guest_session_no_prior — begin with PROF_ID_NONE; end restores NONE
// ---------------------------------------------------------------------------

static void test_guest_session_no_prior() {
    ProfileFixture f;

    CHECK(f.uds.profile_active() == PROF_ID_NONE);

    f.uds.profile_begin_guest();
    CHECK(f.uds.profile_active() == PROF_ID_GUEST);
    CHECK(f.uds.profile_in_guest_session());

    f.uds.profile_end_guest();
    CHECK(f.uds.profile_active() == PROF_ID_NONE);
    CHECK(!f.uds.profile_in_guest_session());
}

// ---------------------------------------------------------------------------
// test_guest_api_roundtrip — GUEST_BEGIN/GUEST_END via ApiWindow rings
// ---------------------------------------------------------------------------

static void test_guest_api_roundtrip() {
    ensure_spine();

    FatTestEnv    env;
    UserDataStore uds;
    CHECK(uds.init().ok());

    uint16_t id = 0;
    CHECK(uds.profile_create("Mia", "en", &id).ok());
    CHECK(uds.profile_set_active(id).ok());

    static ApiWindow win5;
    win5.init(g_posture, g_policy, g_registry);
    win5.bind_user_data(uds);

    push_request(win5, 10u, SVC_IDENTITY, IDN_GUEST_BEGIN, nullptr, 0u);
    CHECK(win5.service_once());

    uint16_t status = 0xFFFFu;
    uint8_t  rsp[4] = {};
    uint16_t rsp_len = 0u;
    CHECK(pop_response(win5, &status, rsp, sizeof(rsp), &rsp_len));
    CHECK(status == API_OK);
    CHECK(rsp_len == 2u);
    uint16_t guest_id = static_cast<uint16_t>(rsp[0] | (rsp[1] << 8u));
    CHECK(guest_id == PROF_ID_GUEST);
    CHECK(uds.profile_in_guest_session());

    push_request(win5, 11u, SVC_IDENTITY, IDN_GUEST_END, nullptr, 0u);
    CHECK(win5.service_once());

    status  = 0xFFFFu;
    rsp_len = 0u;
    CHECK(pop_response(win5, &status, rsp, sizeof(rsp), &rsp_len));
    CHECK(status == API_OK);
    CHECK(rsp_len == 2u);
    uint16_t restored_id = static_cast<uint16_t>(rsp[0] | (rsp[1] << 8u));
    CHECK(restored_id == id);
    CHECK(!uds.profile_in_guest_session());
    CHECK(uds.profile_active() == id);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_profile_create_list();
    test_profile_get();
    test_profile_get_not_found();
    test_profile_active();
    test_profile_active_persists();
    test_profile_remove();
    test_profile_full();
    test_identity_list();
    test_identity_set_get_active();
    test_identity_set_active_not_found();
    test_identity_no_store();
    test_guest_session_begin_end();
    test_guest_session_no_prior();
    test_guest_api_roundtrip();

    return test_summary();
}
