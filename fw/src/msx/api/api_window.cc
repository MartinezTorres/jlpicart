// api_window.cc — JLPiCart API window: ring framing and service dispatch.
//
// Ring framing algorithm is specified in spec.md §5.1 "Ring layout and invariants".
// Frames: u16 frame_len (includes itself) followed by (frame_len-2) msg_bytes.
// frame_len == 0 is a wrap marker; consumer must wrap tail to 0 and re-read.

#include "msx/api/api_window.h"
#include "msx/api/services/core_service.h"
#include <cstring>

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void ApiWindow::init(const SecurityPosture& posture,
                     const PolicyStore&     policy_store,
                     const CapabilityRegistry& registry)
{
    posture_      = &posture;
    policy_store_ = &policy_store;
    registry_     = &registry;

    memset(buf_, 0, WIN_BYTES);

    // Write the ApiWindowHeader.
    ApiWindowHeader& h = header();
    h.sig[0]         = 'J'; h.sig[1] = 'L'; h.sig[2] = 'P'; h.sig[3] = '1';
    h.api_major      = API_MAJOR;
    h.api_minor      = API_MINOR;
    h.layout_ver     = API_LAYOUT_VER;
    h.flags          = API_HDR_FLAGS_V1;
    h.win_size       = API_WINDOW_SIZE;
    h.regs_ofs       = API_REGS_OFS;
    h.req_ring_ofs   = API_REQ_RING_OFS;
    h.req_ring_len   = API_REQ_RING_LEN;
    h.rsp_ring_ofs   = API_RSP_RING_OFS;
    h.rsp_ring_len   = API_RSP_RING_LEN;
    h.h2c_scratch_ofs = API_H2C_SCRATCH_OFS;
    h.h2c_scratch_len = API_H2C_SCRATCH_LEN;
    h.c2h_scratch_ofs = API_C2H_SCRATCH_OFS;
    h.c2h_scratch_len = API_C2H_SCRATCH_LEN;
    h.feature_bits   = API_FEATURES_STAGE4;
    h.max_frame      = API_MAX_FRAME;

    // Init request ring.
    RingHeader& rreq = ring_hdr(API_REQ_RING_OFS);
    rreq.head  = 0;
    rreq.tail  = 0;
    rreq.size  = API_RING_DATA_SIZE;
    rreq.flags = 0;

    // Init response ring.
    RingHeader& rrsp = ring_hdr(API_RSP_RING_OFS);
    rrsp.head  = 0;
    rrsp.tail  = 0;
    rrsp.size  = API_RING_DATA_SIZE;
    rrsp.flags = 0;

    initialized_ = true;
}

// ---------------------------------------------------------------------------
// ring_empty
// ---------------------------------------------------------------------------

bool ApiWindow::ring_empty(uint16_t ring_ofs) const
{
    const RingHeader& rh = *reinterpret_cast<const RingHeader*>(buf_ + ring_ofs);
    return rh.head == rh.tail;
}

// ---------------------------------------------------------------------------
// ring_push_msg — producer (firmware side for responses, Z80 side for requests)
// ---------------------------------------------------------------------------
//
// Algorithm per spec.md §5.1 "Writing a frame (producer algorithm)":
// 1. needed = msg_len + 2
// 2. compute free space from head and tail
// 3. if insufficient, return E_RING_FULL
// 4. if needed > bytes remaining to end-of-ring (need to wrap):
//    - if ≥ 2 bytes remain: write frame_len=0 marker, advance head to 0
//    - else: just advance head to 0
// 5. write frame_len then msg_bytes
// 6. update head

uint16_t ApiWindow::ring_push_msg(uint16_t ring_ofs,
                                  const uint8_t* msg, uint16_t msg_len)
{
    if (msg_len > API_MAX_MSG) {
        return API_E_TOO_BIG;
    }

    RingHeader& rh   = ring_hdr(ring_ofs);
    uint8_t*    data = ring_data(ring_ofs);
    const uint16_t size = rh.size;
    uint16_t head = rh.head;
    const uint16_t tail = rh.tail;

    const uint16_t frame_len = static_cast<uint16_t>(msg_len + 2u);

    // Compute free bytes (producer and consumer share the ring; leave 1 slot
    // so we can distinguish full from empty via head == tail for empty).
    // free = (tail - head - 1 + size) % size
    const uint16_t used = (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(size - (tail - head));
    const uint16_t free_bytes = (uint16_t)(size - used - 1u);

    // Count bytes available contiguously from head to end-of-ring.
    const uint16_t to_end = (uint16_t)(size - head);

    // Total free must accommodate frame_len bytes.
    if (free_bytes < frame_len) {
        return API_E_RING_FULL;
    }

    if (frame_len > to_end) {
        // Frame won't fit contiguously — write wrap marker if room, then wrap.
        if (to_end >= 2u) {
            // Write frame_len=0 (wrap marker) little-endian.
            data[head]     = 0x00;
            data[head + 1] = 0x00;
        }
        head = 0;
    }

    // Write frame_len (little-endian) then msg_bytes.
    data[head]     = static_cast<uint8_t>(frame_len & 0xFFu);
    data[head + 1] = static_cast<uint8_t>(frame_len >> 8u);
    if (msg_len > 0) {
        memcpy(data + head + 2, msg, msg_len);
    }

    // Advance head past the frame.
    head = (uint16_t)((head + frame_len) % size);
    rh.head = head;

    return API_OK;
}

// ---------------------------------------------------------------------------
// ring_pop_msg — consumer
// ---------------------------------------------------------------------------
//
// Reads one framed message from the ring, skipping any wrap markers.
// Returns true if a message was available (out_msg_len = msg_bytes count).
// Returns false if the ring was empty (out_msg_len = 0).

bool ApiWindow::ring_pop_msg(uint16_t ring_ofs, uint8_t* dst, uint16_t dst_max,
                             uint16_t* out_msg_len)
{
    RingHeader& rh   = ring_hdr(ring_ofs);
    uint8_t*    data = ring_data(ring_ofs);
    const uint16_t size = rh.size;

    *out_msg_len = 0;

retry:
    uint16_t head = rh.head;
    uint16_t tail = rh.tail;

    if (head == tail) {
        return false; // empty
    }

    // Read frame_len (little-endian u16) at tail.
    uint16_t frame_len = (uint16_t)(data[tail] | ((uint16_t)data[tail + 1] << 8u));

    if (frame_len == 0) {
        // Wrap marker: consumer wraps to 0.
        rh.tail = 0;
        goto retry;
    }

    if (frame_len < 2u) {
        // Malformed: consume 2 bytes and return.
        rh.tail = (uint16_t)((tail + 2u) % size);
        return true; // *out_msg_len stays 0
    }

    const uint16_t msg_len = (uint16_t)(frame_len - 2u);

    if (msg_len > dst_max) {
        // Too large for caller's buffer — consume the frame anyway.
        rh.tail = (uint16_t)((tail + frame_len) % size);
        return true; // *out_msg_len stays 0 (caller sees truncated)
    }

    // Copy msg_bytes into dst.
    if (msg_len > 0) {
        memcpy(dst, data + tail + 2, msg_len);
    }
    rh.tail = (uint16_t)((tail + frame_len) % size);

    *out_msg_len = msg_len;
    return true;
}

// ---------------------------------------------------------------------------
// write_response — helper used by service handlers via service_once
// ---------------------------------------------------------------------------

void ApiWindow::write_response(uint16_t seq, uint8_t service, uint8_t method,
                               uint16_t status,
                               const uint8_t* payload, uint16_t payload_len)
{
    // Build the response message: MsgHeader + payload.
    uint8_t rsp[API_MAX_MSG];
    MsgHeader hdr = {};
    hdr.seq         = seq;
    hdr.service     = service;
    hdr.method      = method;
    hdr.flags       = 0;
    hdr.status      = status;
    hdr.payload_len = payload_len;
    hdr.scratch_ofs = 0xFFFFu; // none
    hdr.scratch_len = 0;
    hdr.reserved    = 0;

    const uint16_t total_msg = static_cast<uint16_t>(sizeof(MsgHeader) + payload_len);
    if (total_msg > API_MAX_MSG) {
        // Payload too large — send error instead.
        hdr.status      = API_E_INTERNAL;
        hdr.payload_len = 0;
        memcpy(rsp, &hdr, sizeof(MsgHeader));
        ring_push_msg(API_RSP_RING_OFS, rsp, sizeof(MsgHeader));
        return;
    }

    memcpy(rsp, &hdr, sizeof(MsgHeader));
    if (payload_len > 0 && payload != nullptr) {
        memcpy(rsp + sizeof(MsgHeader), payload, payload_len);
    }
    ring_push_msg(API_RSP_RING_OFS, rsp, total_msg);

    // Increment cart_event to signal the Z80 that a response is ready.
    // Explicit read-modify-write avoids the deprecated volatile ++ form.
    regs().cart_event = static_cast<uint8_t>(regs().cart_event + 1u);
}

// ---------------------------------------------------------------------------
// service_once — read one request, dispatch, write response
// ---------------------------------------------------------------------------

bool ApiWindow::service_once()
{
    if (ring_empty(API_REQ_RING_OFS)) {
        return false;
    }

    uint8_t  frame[API_MAX_MSG];
    uint16_t msg_len = 0;
    bool     got     = ring_pop_msg(API_REQ_RING_OFS, frame, sizeof(frame), &msg_len);

    if (!got) {
        return false;
    }

    if (msg_len < sizeof(MsgHeader)) {
        // Frame too short to contain a header — cannot echo seq; drop silently.
        return true;
    }

    MsgHeader req;
    memcpy(&req, frame, sizeof(MsgHeader));

    // Validate request invariants.
    if (req.status != 0 || req.reserved != 0) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    // Check declared payload fits in the frame.
    const uint16_t expected_total = (uint16_t)(sizeof(MsgHeader) + req.payload_len);
    if (expected_total > msg_len) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    const uint8_t* payload     = frame + sizeof(MsgHeader);
    const uint16_t payload_len = req.payload_len;

    // Dispatch by service.
    switch (req.service) {
        case SVC_SYSTEM:
            core_service_handle(req, payload, payload_len, *this,
                                *posture_, *policy_store_, *registry_);
            break;

        default:
            write_response(req.seq, req.service, req.method, API_E_UNSUPPORTED, nullptr, 0);
            break;
    }

    return true;
}
