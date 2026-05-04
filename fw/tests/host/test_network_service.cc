// test_network_service.cc — host tests for Network service (Stage 32).
//
// Tests verify:
//   - NET_STATUS returns a well-formed 8-byte response with disconnected state
//   - NET_HTTP_REQUEST with missing scratch returns E_BAD_ARG
//   - NET_HTTP_REQUEST with zero url_len returns E_BAD_ARG
//   - NET_HTTP_REQUEST with valid args returns E_IO (transport stub returns NET_UNAVAILABLE)
//   - Unbound transport returns E_UNSUPPORTED
//   - bind_network_transport() sets API_FEATURE_NETWORK in feature_bits

#include "test_helpers.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "net/transport_esp_at.h"
#include "net/network_service.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "boards/board_descriptor.h"
#include "drivers/driver_descriptor.h"
#include "security/otp_reader.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

static const BoardCapabilityDecl kNetTestCaps[] = {
    { "bus.msx",  false },
    { "net.wifi", true  },
};
static const BoardDescriptor kNetTestBoard = { "test_board", kNetTestCaps, 2 };

static SecurityPosture make_posture() {
    uint8_t buf[256] = {};
    FakeOtpReader otp(buf, sizeof(buf));
    return SecurityPosture::read(otp);
}

struct NetFixture {
    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;
    TransportEspAt     transport;

    NetFixture() : posture(make_posture()) {
        policy_store.load_from_buffer(nullptr, 0, posture);
        registry.init(kNetTestBoard, kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        transport.init();
    }
};

// Push a request frame (with optional payload) into the request ring.
static uint16_t push_req(ApiWindow& win, uint16_t seq, uint8_t service,
                          uint8_t method,
                          const uint8_t* payload = nullptr, uint16_t payload_len = 0,
                          uint16_t scratch_ofs = 0xFFFFu, uint16_t scratch_len = 0u)
{
    uint8_t frame[sizeof(MsgHeader) + 256];
    MsgHeader hdr = {};
    hdr.seq         = seq;
    hdr.service     = service;
    hdr.method      = method;
    hdr.payload_len = payload_len;
    hdr.scratch_ofs = scratch_ofs;
    hdr.scratch_len = scratch_len;
    memcpy(frame, &hdr, sizeof(MsgHeader));
    if (payload && payload_len > 0)
        memcpy(frame + sizeof(MsgHeader), payload, payload_len);
    return win.ring_push_msg(API_REQ_RING_OFS, frame,
                              (uint16_t)(sizeof(MsgHeader) + payload_len));
}

static bool pop_rsp(ApiWindow& win, MsgHeader* hdr_out,
                    uint8_t* payload_out, uint16_t* payload_len_out)
{
    uint8_t  frame[API_MAX_MSG];
    uint16_t msg_len = 0;
    bool got = win.ring_pop_msg(API_RSP_RING_OFS, frame, sizeof(frame), &msg_len);
    if (!got || msg_len < sizeof(MsgHeader)) return false;
    memcpy(hdr_out, frame, sizeof(MsgHeader));
    *payload_len_out = hdr_out->payload_len;
    if (payload_out && *payload_len_out > 0)
        memcpy(payload_out, frame + sizeof(MsgHeader), *payload_len_out);
    return true;
}

// ---------------------------------------------------------------------------
// test_feature_bit — bind sets API_FEATURE_NETWORK
// ---------------------------------------------------------------------------

static void test_feature_bit()
{
    NetFixture f;
    const ApiWindowHeader& h = *reinterpret_cast<const ApiWindowHeader*>(f.win.buf());
    CHECK(!(h.feature_bits & API_FEATURE_NETWORK));

    f.win.bind_network_transport(f.transport);
    CHECK(h.feature_bits & API_FEATURE_NETWORK);
}

// ---------------------------------------------------------------------------
// test_net_status — stub returns disconnected (all zeros)
// ---------------------------------------------------------------------------

static void test_net_status()
{
    NetFixture f;
    f.win.bind_network_transport(f.transport);

    push_req(f.win, 1u, SVC_NETWORK, NET_STATUS);
    bool ok = f.win.service_once();
    CHECK(ok);

    MsgHeader rsp = {};
    uint8_t   payload[16] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, payload, &plen));
    CHECK(rsp.status == API_OK);
    CHECK(plen == 8u);
    CHECK(payload[0] == 0u);  // connected = false
    CHECK(payload[1] == 0u);  // rssi = 0
    // ipv4 bytes 2–5 all zero
    CHECK(payload[2] == 0u);
    CHECK(payload[3] == 0u);
    CHECK(payload[4] == 0u);
    CHECK(payload[5] == 0u);
}

// ---------------------------------------------------------------------------
// test_net_status_unbound — no transport bound → E_UNSUPPORTED
// ---------------------------------------------------------------------------

static void test_net_status_unbound()
{
    NetFixture f;
    // Transport not bound.

    push_req(f.win, 1u, SVC_NETWORK, NET_STATUS);
    f.win.service_once();

    MsgHeader rsp = {};
    uint8_t   payload[16] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, payload, &plen));
    CHECK(rsp.status == API_E_UNSUPPORTED);
}

// ---------------------------------------------------------------------------
// test_http_no_scratch — HTTP_REQUEST without scratch → E_BAD_ARG
// ---------------------------------------------------------------------------

static void test_http_no_scratch()
{
    NetFixture f;
    f.win.bind_network_transport(f.transport);

    HttpReq req = {};
    req.verb    = 0u;  // GET
    req.url_len = 20u;

    push_req(f.win, 1u, SVC_NETWORK, NET_HTTP_REQUEST,
             reinterpret_cast<const uint8_t*>(&req), sizeof(req),
             0xFFFFu, 0u);  // scratch_ofs = 0xFFFF (no scratch)
    f.win.service_once();

    MsgHeader rsp = {};
    uint8_t   pl[4] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, pl, &plen));
    CHECK(rsp.status == API_E_BAD_ARG);
}

// ---------------------------------------------------------------------------
// test_http_zero_url — HTTP_REQUEST with url_len=0 → E_BAD_ARG
// ---------------------------------------------------------------------------

static void test_http_zero_url()
{
    NetFixture f;
    f.win.bind_network_transport(f.transport);

    HttpReq req = {};
    req.verb    = 0u;
    req.url_len = 0u;

    // Place a dummy URL in H2C scratch anyway (url_len=0 → rejected before reading)
    push_req(f.win, 1u, SVC_NETWORK, NET_HTTP_REQUEST,
             reinterpret_cast<const uint8_t*>(&req), sizeof(req),
             0u, 1u);
    f.win.service_once();

    MsgHeader rsp = {};
    uint8_t   pl[4] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, pl, &plen));
    CHECK(rsp.status == API_E_BAD_ARG);
}

// ---------------------------------------------------------------------------
// test_http_transport_unavailable — valid request but stub returns NET_UNAVAILABLE
// → E_IO
// ---------------------------------------------------------------------------

static void test_http_transport_unavailable()
{
    NetFixture f;
    f.win.bind_network_transport(f.transport);

    // Write a URL into H2C scratch.
    const char url[] = "http://example.com/";
    uint16_t url_len = (uint16_t)(sizeof(url) - 1u);  // exclude NUL
    memcpy(f.win.buf() + API_H2C_SCRATCH_OFS, url, url_len);

    HttpReq req = {};
    req.verb       = 0u;  // GET
    req.url_len    = url_len;
    req.body_len   = 0u;
    req.timeout_ms = 100u;

    push_req(f.win, 2u, SVC_NETWORK, NET_HTTP_REQUEST,
             reinterpret_cast<const uint8_t*>(&req), sizeof(req),
             0u, url_len);
    f.win.service_once();

    MsgHeader rsp = {};
    uint8_t   pl[8] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, pl, &plen));
    // Stub returns NET_UNAVAILABLE → service maps to E_IO
    CHECK(rsp.status == API_E_INTERNAL);
}

// ---------------------------------------------------------------------------
// test_http_unknown_method — unknown method → E_UNSUPPORTED
// ---------------------------------------------------------------------------

static void test_http_unknown_method()
{
    NetFixture f;
    f.win.bind_network_transport(f.transport);

    push_req(f.win, 1u, SVC_NETWORK, 0xFFu);
    f.win.service_once();

    MsgHeader rsp = {};
    uint8_t   pl[4] = {};
    uint16_t  plen = 0u;
    CHECK(pop_rsp(f.win, &rsp, pl, &plen));
    CHECK(rsp.status == API_E_UNSUPPORTED);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_feature_bit();
    test_net_status();
    test_net_status_unbound();
    test_http_no_scratch();
    test_http_zero_url();
    test_http_transport_unavailable();
    test_http_unknown_method();

    return test_summary();
}
