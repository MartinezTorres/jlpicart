// test_api_window.cc — host tests for ApiWindow ring framing and service dispatch.
#include "test_helpers.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "msx/api/services/services.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "platform/platform.h"
#include "spine/driver_descriptor.h"
#include "spine/otp_reader.h"
#include "spine/policy_store.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Minimal spine fixtures (mirrors test_capability_registry.cc)
// ---------------------------------------------------------------------------

static const BoardCapabilityDecl kTestCaps[] = {
    { "bus.msx",   false },
    { "net.wifi",  true  },
};
static const BoardDescriptor kTestBoard = { "test_board", kTestCaps, 2 };

// Build a minimal SecurityPosture from an all-zero OTP buffer.
static SecurityPosture make_dev_posture() {
    uint8_t buf[256] = {};
    FakeOtpReader otp(buf, sizeof(buf));
    return SecurityPosture::read(otp);
}

// Build an ApiWindow initialised with minimal spine objects.
struct TestFixture {
    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;

    TestFixture() : posture(make_dev_posture()) {
        // Use a zero-byte buffer to trigger "policy missing" → safe defaults.
        DiagStatus st = policy_store.load_from_buffer(nullptr, 0, posture);
        (void)st;

        registry.init(kTestBoard, kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
    }
};

// Push a minimal request frame (MsgHeader only, no payload).
static uint16_t push_request(ApiWindow& win, uint16_t seq,
                              uint8_t service, uint8_t method)
{
    uint8_t msg[sizeof(MsgHeader)] = {};
    MsgHeader hdr = {};
    hdr.seq         = seq;
    hdr.service     = service;
    hdr.method      = method;
    hdr.scratch_ofs = 0xFFFFu;
    memcpy(msg, &hdr, sizeof(MsgHeader));
    return win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(MsgHeader));
}

// Pop a response from the response ring and parse the MsgHeader.
static bool pop_response(ApiWindow& win, MsgHeader* hdr_out,
                         uint8_t* payload_out, uint16_t* payload_len_out)
{
    uint8_t  frame[API_MAX_MSG];
    uint16_t msg_len = 0;
    bool got = win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &msg_len);
    if (!got || msg_len < sizeof(MsgHeader)) return false;

    memcpy(hdr_out, frame, sizeof(MsgHeader));
    *payload_len_out = hdr_out->payload_len;
    if (payload_out && *payload_len_out > 0) {
        memcpy(payload_out, frame + sizeof(MsgHeader), *payload_len_out);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Tests: header initialisation
// ---------------------------------------------------------------------------

static void test_header_signature() {
    TestFixture f;
    const uint8_t* buf = f.win.buf();
    CHECK(buf[0] == 'J');
    CHECK(buf[1] == 'L');
    CHECK(buf[2] == 'P');
    CHECK(buf[3] == '1');
}

static void test_header_fields() {
    TestFixture f;
    const ApiWindowHeader& h = *reinterpret_cast<const ApiWindowHeader*>(f.win.buf());
    CHECK(h.api_major   == API_MAJOR);
    CHECK(h.api_minor   == API_MINOR);
    CHECK(h.layout_ver  == API_LAYOUT_VER);
    CHECK(h.flags       == API_HDR_FLAGS_V1);
    CHECK(h.win_size    == API_WINDOW_SIZE);
    CHECK(h.regs_ofs    == API_REGS_OFS);
    CHECK(h.req_ring_ofs == API_REQ_RING_OFS);
    CHECK(h.req_ring_len == API_REQ_RING_LEN);
    CHECK(h.rsp_ring_ofs == API_RSP_RING_OFS);
    CHECK(h.rsp_ring_len == API_RSP_RING_LEN);
    CHECK(h.max_frame   == API_MAX_FRAME);
    CHECK(h.feature_bits == API_FEATURES_CURRENT);
    CHECK(h.reserved0   == 0);
    CHECK(h.reserved1   == 0);
}

// ---------------------------------------------------------------------------
// Tests: ring push / pop roundtrip
// ---------------------------------------------------------------------------

static void test_ring_push_pop_roundtrip() {
    TestFixture f;
    const uint8_t msg[] = { 1, 2, 3, 4, 5 };
    uint16_t st = f.win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(msg));
    CHECK(st == API_OK);
    CHECK(!f.win.ring_empty(API_REQ_RING_OFS));

    uint8_t out[32] = {};
    uint16_t out_len = 0;
    bool got = f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &out_len);
    CHECK(got);
    CHECK(out_len == sizeof(msg));
    CHECK(memcmp(out, msg, sizeof(msg)) == 0);
    CHECK(f.win.ring_empty(API_REQ_RING_OFS));
}

static void test_ring_empty_after_init() {
    TestFixture f;
    CHECK(f.win.ring_empty(API_REQ_RING_OFS));
    CHECK(f.win.ring_empty(API_RSP_RING_OFS));
}

static void test_ring_pop_empty_returns_false() {
    TestFixture f;
    uint8_t out[32];
    uint16_t out_len = 99;
    bool got = f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &out_len);
    CHECK(!got);
    CHECK(out_len == 0);
}

static void test_ring_multiple_frames() {
    TestFixture f;
    const uint8_t a[] = { 0xAA };
    const uint8_t b[] = { 0xBB, 0xCC };
    f.win.ring_push_msg(API_REQ_RING_OFS, a, sizeof(a));
    f.win.ring_push_msg(API_REQ_RING_OFS, b, sizeof(b));

    uint8_t out[8];
    uint16_t len = 0;

    bool g1 = f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
    CHECK(g1 && len == 1 && out[0] == 0xAA);

    bool g2 = f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
    CHECK(g2 && len == 2 && out[0] == 0xBB && out[1] == 0xCC);

    CHECK(f.win.ring_empty(API_REQ_RING_OFS));
}

// ---------------------------------------------------------------------------
// Tests: ring wrap-around
// ---------------------------------------------------------------------------

// Fill the ring almost to the end with small frames, then push one more that
// straddles the end — it must wrap to 0 and still be readable.
static void test_ring_wrap_around() {
    TestFixture f;

    // Ring data = 512 bytes.  Each frame with 1 msg byte = 3 bytes total (frame_len=3).
    // Push until the tail of the ring is close to the end.
    // 512 / 3 = 170.6 → 170 frames consume 510 bytes; 2 remain at end.
    // The 171st frame (3 bytes) won't fit contiguously → wrap marker + wrap.

    const uint8_t msg1[] = { 0x01 };
    int pushes = 0;
    for (int i = 0; i < 170; i++) {
        uint16_t st = f.win.ring_push_msg(API_REQ_RING_OFS, msg1, sizeof(msg1));
        if (st == API_OK) pushes++;
    }

    // Drain all frames to reset tail.
    for (int i = 0; i < pushes; i++) {
        uint8_t out[8]; uint16_t len = 0;
        f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
    }
    CHECK(f.win.ring_empty(API_REQ_RING_OFS));

    // Now push frames until head is near the end, leave tail at 0.
    // Push 169 frames (507 bytes), head = 510, then push one more that wraps.
    for (int i = 0; i < 169; i++) {
        f.win.ring_push_msg(API_REQ_RING_OFS, msg1, sizeof(msg1));
    }
    // Drain half to free space for the wrap scenario.
    for (int i = 0; i < 80; i++) {
        uint8_t out[8]; uint16_t len = 0;
        f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
    }

    // Push a slightly larger message that should trigger a wrap.
    const uint8_t big[] = { 0xDE, 0xAD };
    uint16_t st = f.win.ring_push_msg(API_REQ_RING_OFS, big, sizeof(big));
    CHECK(st == API_OK || st == API_E_RING_FULL);

    // Drain remaining frames — none should cause an assertion or hang.
    int drained = 0;
    while (!f.win.ring_empty(API_REQ_RING_OFS) && drained < 512) {
        uint8_t out[8]; uint16_t len = 0;
        f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
        drained++;
    }
    CHECK(f.win.ring_empty(API_REQ_RING_OFS));
}

// Regression test for the wrap+full bug:
// When frame_len > to_end (wrapping needed), the bytes at [head, size) are
// permanently wasted by the wrap marker.  The effective space consumed is
// to_end + frame_len, not just frame_len.  A frame that fits in total free
// space but NOT in the post-wrap window MUST be rejected.
static void test_ring_wrap_then_full_is_rejected() {
    TestFixture f;

    // Fill request ring so head ends up close to the end and tail is small.
    // Each frame with 1 byte: frame_len=3.  Fill 169 frames (507 bytes used).
    const uint8_t tiny[] = { 0x01 };
    for (int i = 0; i < 169; i++) {
        f.win.ring_push_msg(API_REQ_RING_OFS, tiny, sizeof(tiny));
    }
    // head=507, tail=0.  Drain 2 frames so tail=6.
    for (int i = 0; i < 2; i++) {
        uint8_t out[8]; uint16_t len = 0;
        f.win.ring_pop_msg(API_REQ_RING_OFS, out, sizeof(out), &len);
    }
    // State: head=507, tail=6, size=512.
    // free_bytes = (512-507) + 6 - 1 = 5 + 5 = 10 bytes.
    // to_end = 5.  A frame_len=6 (4-byte msg) would need wrap:
    //   needed = to_end(5) + frame_len(6) = 11 > free_bytes(10) → FULL.
    // But old code checked only frame_len(6) <= free_bytes(10) → wrongly ALLOWED.
    const uint8_t msg4[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    uint16_t st = f.win.ring_push_msg(API_REQ_RING_OFS, msg4, sizeof(msg4));
    CHECK(st == API_E_RING_FULL);
}

static void test_ring_full_returns_error() {
    TestFixture f;
    // Fill the ring with max-size messages until it's full.
    // API_MAX_MSG = 254 bytes; ring data = 512 bytes.
    // Two frames of 254 + 2 = 256 bytes = 512 bytes total.
    // But ring must leave 1 free byte, so only one full frame fits.
    const uint8_t big[API_MAX_MSG] = {};
    uint16_t st1 = f.win.ring_push_msg(API_REQ_RING_OFS, big, API_MAX_MSG);
    CHECK(st1 == API_OK);
    uint16_t st2 = f.win.ring_push_msg(API_REQ_RING_OFS, big, API_MAX_MSG);
    CHECK(st2 == API_E_RING_FULL);
}

// ---------------------------------------------------------------------------
// Tests: service_once dispatch
// ---------------------------------------------------------------------------

static void test_service_once_returns_false_on_empty_ring() {
    TestFixture f;
    CHECK(!f.win.service_once());
}

static void test_service_once_get_api_info() {
    TestFixture f;

    uint16_t push_st = push_request(f.win, 42, SVC_SYSTEM, SYS_GET_API_INFO);
    CHECK(push_st == API_OK);

    bool processed = f.win.service_once();
    CHECK(processed);

    MsgHeader rsp_hdr = {};
    uint8_t   payload[sizeof(ApiInfo)] = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, payload, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.seq     == 42);
    CHECK(rsp_hdr.service == SVC_SYSTEM);
    CHECK(rsp_hdr.method  == SYS_GET_API_INFO);
    CHECK(rsp_hdr.status  == API_OK);
    CHECK(payload_len     == sizeof(ApiInfo));

    ApiInfo info;
    memcpy(&info, payload, sizeof(ApiInfo));
    CHECK(info.api_major == API_MAJOR);
    CHECK(info.api_minor == API_MINOR);
    CHECK(info.layout_ver == API_LAYOUT_VER);
    CHECK(info.max_frame  == API_MAX_FRAME);
    CHECK((info.feature_bits & API_FEATURE_SYSTEM) != 0);
}

static void test_service_once_get_security_info() {
    TestFixture f;

    push_request(f.win, 7, SVC_SYSTEM, SYS_GET_SECURITY_INFO);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint8_t   payload[sizeof(SecurityInfoResp)] = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, payload, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status  == API_OK);
    CHECK(payload_len     == sizeof(SecurityInfoResp));

    SecurityInfoResp resp;
    memcpy(&resp, payload, sizeof(resp));
    // Dev posture: all bits off.
    CHECK(resp.posture_props       == 0u);
    CHECK(resp.boot_key_valid_mask == 0u);
}

static void test_service_once_get_policy_flags() {
    TestFixture f;

    push_request(f.win, 8, SVC_SYSTEM, SYS_GET_POLICY_FLAGS);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint8_t   payload[sizeof(PolicyFlagsResp)] = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, payload, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_OK);
    CHECK(payload_len == sizeof(PolicyFlagsResp));

    PolicyFlagsResp resp;
    memcpy(&resp, payload, sizeof(resp));
    // Policy not loaded (buffer was null) → safe defaults (flags = 0).
    CHECK(resp.policy_flags == 0u);
}

static void test_service_once_get_caps() {
    TestFixture f;

    push_request(f.win, 3, SVC_SYSTEM, SYS_GET_CAPS);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint8_t   payload[128] = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, payload, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_OK);
    // Payload: u16 count + count * CapEntry(8).
    CHECK(payload_len >= 2u);
    uint16_t count = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
    // kTestBoard has 2 caps + driver caps (from kDriverDescriptors).
    CHECK(count >= 2u);
    CHECK(payload_len == (uint16_t)(2u + count * sizeof(CapEntry)));
}

static void test_service_once_unknown_service_returns_unsupported() {
    TestFixture f;

    push_request(f.win, 99, 0xFE, 0x00);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_E_UNSUPPORTED);
    CHECK(rsp_hdr.seq    == 99);
}

static void test_service_once_unknown_method_returns_unsupported() {
    TestFixture f;

    push_request(f.win, 55, SVC_SYSTEM, 0xFF);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(rsp_hdr.status == API_E_UNSUPPORTED);
}

static void test_service_once_bad_req_nonzero_status() {
    // Send a request with status != 0 — must be rejected.
    TestFixture f;

    uint8_t msg[sizeof(MsgHeader)] = {};
    MsgHeader hdr = {};
    hdr.seq     = 1;
    hdr.service = SVC_SYSTEM;
    hdr.method  = SYS_GET_API_INFO;
    hdr.status  = 0x0001; // invalid in a request
    hdr.scratch_ofs = 0xFFFF;
    memcpy(msg, &hdr, sizeof(MsgHeader));
    f.win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(MsgHeader));
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(rsp_hdr.status == API_E_BAD_REQ);
}

static void test_service_once_bad_req_seq_zero() {
    // seq MUST be nonzero.
    TestFixture f;

    uint8_t msg[sizeof(MsgHeader)] = {};
    MsgHeader hdr = {};
    hdr.seq         = 0;    // invalid
    hdr.service     = SVC_SYSTEM;
    hdr.method      = SYS_GET_API_INFO;
    hdr.scratch_ofs = 0xFFFF;
    memcpy(msg, &hdr, sizeof(MsgHeader));
    f.win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(MsgHeader));
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_E_BAD_REQ);
}

static void test_service_once_bad_arg_scratch_out_of_bounds() {
    // scratch_ofs + scratch_len must fit within h2c scratch (API_H2C_SCRATCH_LEN).
    TestFixture f;

    uint8_t msg[sizeof(MsgHeader)] = {};
    MsgHeader hdr = {};
    hdr.seq         = 10;
    hdr.service     = SVC_SYSTEM;
    hdr.method      = SYS_GET_API_INFO;
    hdr.scratch_ofs = static_cast<uint16_t>(API_H2C_SCRATCH_LEN - 1); // 1 byte from end
    hdr.scratch_len = 10;  // overflows by 9 bytes
    memcpy(msg, &hdr, sizeof(MsgHeader));
    f.win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(MsgHeader));
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_E_BAD_ARG);
    CHECK(rsp_hdr.seq    == 10);
}

static void test_service_once_scratch_at_exact_limit_is_ok() {
    // scratch_ofs + scratch_len == API_H2C_SCRATCH_LEN is exactly at the limit,
    // so it should be accepted (not OOB).
    TestFixture f;

    uint8_t msg[sizeof(MsgHeader)] = {};
    MsgHeader hdr = {};
    hdr.seq         = 11;
    hdr.service     = SVC_SYSTEM;
    hdr.method      = SYS_GET_API_INFO;
    hdr.scratch_ofs = 0;
    hdr.scratch_len = API_H2C_SCRATCH_LEN;  // exactly fills scratch
    memcpy(msg, &hdr, sizeof(MsgHeader));
    f.win.ring_push_msg(API_REQ_RING_OFS, msg, sizeof(MsgHeader));
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    bool got = pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(got);
    CHECK(rsp_hdr.status == API_OK);  // valid range: accepted
}

static void test_service_once_seq_echoed() {
    TestFixture f;
    push_request(f.win, 0xABCD, SVC_SYSTEM, SYS_GET_API_INFO);
    f.win.service_once();

    MsgHeader rsp_hdr = {};
    uint16_t  payload_len = 0;
    pop_response(f.win, &rsp_hdr, nullptr, &payload_len);
    CHECK(rsp_hdr.seq == 0xABCDu);
}

// ---------------------------------------------------------------------------
// Tests: posture_to_props
// ---------------------------------------------------------------------------

static void test_posture_to_props_all_off() {
    uint8_t buf[256] = {};
    FakeOtpReader otp(buf, sizeof(buf));
    SecurityPosture p = SecurityPosture::read(otp);
    CHECK(posture_to_props(p) == 0u);
}

static void test_posture_to_props_secure_boot() {
    uint8_t buf[256] = {};
    buf[otp_offsets::CRIT1] = otp_offsets::CRIT1_SECURE_BOOT_ENABLE_BIT;
    FakeOtpReader otp(buf, sizeof(buf));
    SecurityPosture p = SecurityPosture::read(otp);
    uint32_t props = posture_to_props(p);
    CHECK((props & API_POSTURE_SECURE_BOOT_ENABLED) != 0u);
    CHECK((props & API_POSTURE_DEBUG_DISABLED)       == 0u);
}

int main() {
    test_header_signature();
    test_header_fields();
    test_ring_empty_after_init();
    test_ring_push_pop_roundtrip();
    test_ring_pop_empty_returns_false();
    test_ring_multiple_frames();
    test_ring_wrap_around();
    test_ring_wrap_then_full_is_rejected();
    test_ring_full_returns_error();
    test_service_once_returns_false_on_empty_ring();
    test_service_once_get_api_info();
    test_service_once_get_security_info();
    test_service_once_get_policy_flags();
    test_service_once_get_caps();
    test_service_once_unknown_service_returns_unsupported();
    test_service_once_unknown_method_returns_unsupported();
    test_service_once_bad_req_nonzero_status();
    test_service_once_seq_echoed();
    test_posture_to_props_all_off();
    test_posture_to_props_secure_boot();
    test_service_once_bad_req_seq_zero();
    test_service_once_bad_arg_scratch_out_of_bounds();
    test_service_once_scratch_at_exact_limit_is_ok();
    return test_summary();
}
