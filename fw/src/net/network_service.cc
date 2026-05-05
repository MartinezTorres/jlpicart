// network_service.cc — Network service (0x02) implementation.

#include "net/transport_esp_at.h"
#include "net/transport_esp_at.h"
#include "msx/api/api_window.h"
#include <cstring>

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

static void send_ok(ApiWindow& win, const MsgHeader& req,
                    const uint8_t* payload = nullptr, uint16_t len = 0)
{
    win.write_response(req.seq, req.service, req.method, API_OK, payload, len);
}

static void send_err(ApiWindow& win, const MsgHeader& req, uint16_t status)
{
    win.write_response(req.seq, req.service, req.method, status, nullptr, 0);
}

// ---------------------------------------------------------------------------
// 0x00 NET_STATUS
//
// Request payload: none.
// Response payload: { u8 connected, u8 rssi, u32 ipv4, u32 reserved }  (8 bytes)
// ---------------------------------------------------------------------------

static void handle_net_status(const MsgHeader& req,
                               ApiWindow& win,
                               TransportEspAt& transport)
{
    TransportEspAt::NetStatus st = {};
    transport.get_status(st);

    uint8_t resp[8] = {};
    resp[0] = st.connected ? 1u : 0u;
    resp[1] = static_cast<uint8_t>(st.rssi);  // signed → unsigned reinterpret
    // ipv4 at bytes 2–5, little-endian in wire format
    resp[2] = static_cast<uint8_t>(st.ipv4 >> 24u);
    resp[3] = static_cast<uint8_t>(st.ipv4 >> 16u);
    resp[4] = static_cast<uint8_t>(st.ipv4 >>  8u);
    resp[5] = static_cast<uint8_t>(st.ipv4);
    // bytes 6–7: reserved, already zero

    send_ok(win, req, resp, sizeof(resp));
}

// ---------------------------------------------------------------------------
// 0x01 NET_HTTP_REQUEST
//
// Request payload: HttpReq (8 bytes)
// Request scratch (H2C): [url_len bytes URL][body_len bytes body]
// Response payload: HttpResp (4 bytes)
// Response scratch (C2H): response body bytes
// ---------------------------------------------------------------------------

static void handle_http_request(const MsgHeader& req,
                                  const uint8_t* payload, uint16_t payload_len,
                                  ApiWindow& win,
                                  TransportEspAt& transport)
{
    if (payload_len < sizeof(HttpReq)) {
        send_err(win, req, API_E_BAD_REQ);
        return;
    }

    HttpReq hreq;
    memcpy(&hreq, payload, sizeof(hreq));

    // Scratch must be present and large enough to hold URL + body.
    if (req.scratch_ofs == 0xFFFFu) {
        send_err(win, req, API_E_BAD_ARG);
        return;
    }
    uint32_t scratch_needed = (uint32_t)hreq.url_len + hreq.body_len;
    if (scratch_needed > API_H2C_SCRATCH_LEN ||
        (uint32_t)req.scratch_ofs + scratch_needed > API_H2C_SCRATCH_LEN) {
        send_err(win, req, API_E_BAD_ARG);
        return;
    }
    if (hreq.url_len == 0u) {
        send_err(win, req, API_E_BAD_ARG);
        return;
    }

    // Extract URL (null-terminate it in a local buffer).
    if (hreq.url_len >= 512u) {
        send_err(win, req, API_E_TOO_BIG);
        return;
    }
    char url[512];
    const uint8_t* scratch_base = win.buf() + API_H2C_SCRATCH_OFS + req.scratch_ofs;
    memcpy(url, scratch_base, hreq.url_len);
    url[hreq.url_len] = '\0';

    const uint8_t* body     = scratch_base + hreq.url_len;
    uint16_t       body_len = hreq.body_len;

    bool https = (hreq.flags & 0x01u) != 0u;

    // Write response body directly into C2H scratch.
    uint8_t* c2h = win.buf() + API_C2H_SCRATCH_OFS;
    uint16_t http_status = 0u;
    uint16_t resp_len    = 0u;

    DiagStatus ds = transport.http_request(
        hreq.verb, https, url,
        body, body_len,
        c2h, static_cast<uint16_t>(API_C2H_SCRATCH_LEN),
        hreq.timeout_ms,
        &http_status, &resp_len);

    if (!ds.ok()) {
        uint16_t api_err = (ds.code == DiagCode::NET_TIMEOUT)
                           ? API_E_TIMEOUT
                           : API_E_INTERNAL;
        send_err(win, req, api_err);
        return;
    }

    // Build response: HttpResp payload + scratch pointing to C2H body.
    HttpResp hresp = {};
    hresp.http_status = http_status;
    hresp.body_len    = resp_len;

    // Use ring_push_msg directly so we can set scratch_ofs/scratch_len.
    MsgHeader rsp = {};
    rsp.seq         = req.seq;
    rsp.service     = req.service;
    rsp.method      = req.method;
    rsp.status      = API_OK;
    rsp.payload_len = static_cast<uint16_t>(sizeof(HttpResp));
    rsp.scratch_ofs = 0u;          // body starts at C2H scratch offset 0
    rsp.scratch_len = resp_len;
    rsp.reserved    = 0u;

    uint8_t frame[sizeof(MsgHeader) + sizeof(HttpResp)];
    memcpy(frame,                  &rsp,   sizeof(MsgHeader));
    memcpy(frame + sizeof(MsgHeader), &hresp, sizeof(HttpResp));
    win.ring_push_msg(API_RSP_RING_OFS, frame, sizeof(frame));

    // Doorbell
    ApiRegs* regs = reinterpret_cast<ApiRegs*>(win.buf() + API_REGS_OFS);
    regs->cart_event = static_cast<uint8_t>(regs->cart_event + 1u);
}

// ---------------------------------------------------------------------------
// dispatch
// ---------------------------------------------------------------------------

void network_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             TransportEspAt&  transport)
{
    switch (req.method) {
        case NET_STATUS:
            handle_net_status(req, win, transport);
            break;
        case NET_HTTP_REQUEST:
            handle_http_request(req, payload, payload_len, win, transport);
            break;
        default:
            win.write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            break;
    }
}
