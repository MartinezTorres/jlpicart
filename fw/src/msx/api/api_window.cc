// api_window.cc — JLPiCart API window: ring framing and service dispatch.
//
// Ring framing algorithm is specified in spec.md §5.1 "Ring layout and invariants".
// Frames: u16 frame_len (includes itself) followed by (frame_len-2) msg_bytes.
// frame_len == 0 is a wrap marker; consumer must wrap tail to 0 and re-read.

#include "msx/api/api_window.h"
#include "msx/api/services/core_service.h"
#include "msx/api/services/identity_service.h"
#include "msx/api/services/storage_service.h"
#include "msx/api/services/userstats_service.h"
#include "net/network_service.h"
#include "net/transport_esp_at.h"
#include "identity/device_identity.h"
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
// bind_profile_store
// ---------------------------------------------------------------------------

void ApiWindow::bind_profile_store(ProfileStore& ps)
{
    profile_store_ = &ps;
    // Advertise the Identity service now that it has a backing store.
    header().feature_bits |= API_FEATURE_IDENTITY;
}

void ApiWindow::set_reset_menu_fn(void (*fn)())
{
    reset_menu_fn_ = fn;
}

void ApiWindow::bind_save_store(SaveStore& ss)
{
    save_store_ = &ss;
    // Update active_profile_id_ from profile store if available.
    if (profile_store_) {
        active_profile_id_ = profile_store_->active();
    }
    header().feature_bits |= API_FEATURE_STORAGE;
}

void ApiWindow::bind_stats_store(StatsStore& ss)
{
    stats_store_ = &ss;
    header().feature_bits |= API_FEATURE_USERSTATS;
}

void ApiWindow::bind_network_transport(TransportEspAt& t)
{
    net_transport_ = &t;
    header().feature_bits |= API_FEATURE_NETWORK;
}

void ApiWindow::bind_device_identity(DeviceIdentity& dik)
{
    device_identity_ = &dik;
}

void ApiWindow::set_active_payload(const char* payload_id)
{
    if (!payload_id) {
        active_payload_id_[0] = '\0';
        return;
    }
    strncpy(active_payload_id_, payload_id, sizeof(active_payload_id_) - 1u);
    active_payload_id_[sizeof(active_payload_id_) - 1u] = '\0';
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

    // Total free bytes in the ring (leave 1 slot so head==tail means empty).
    const uint16_t used = (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(size - (tail - head));
    const uint16_t free_bytes = (uint16_t)(size - used - 1u);

    // Bytes from head to end-of-ring (contiguous space at the current head).
    const uint16_t to_end = (uint16_t)(size - head);

    // When frame_len > to_end the frame does not fit contiguously; we must
    // wrap.  Wrapping wastes the [head, size) bytes (the consumer advances
    // past them via the wrap marker).  Space consumed = to_end + frame_len.
    const uint16_t needed = (frame_len > to_end)
        ? static_cast<uint16_t>(to_end + frame_len)
        : frame_len;

    if (needed > free_bytes) {
        return API_E_RING_FULL;
    }

    if (frame_len > to_end) {
        // Write frame_len=0 (wrap marker) if there is room, then wrap head.
        if (to_end >= 2u) {
            data[head]     = 0x00;
            data[head + 1] = 0x00;
        }
        head = 0;
    }

    // Write frame_len (little-endian) then msg_bytes.
    data[head]     = static_cast<uint8_t>(frame_len & 0xFFu);
    data[head + 1] = static_cast<uint8_t>(frame_len >> 8u);
    memcpy(data + head + 2, msg, msg_len);

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

    const uint16_t head = rh.head;
    uint16_t tail = rh.tail;

    if (head == tail) return false; // empty

    // If fewer than 2 bytes remain to end-of-ring, the producer wrapped head
    // without writing a marker (to_end < 2 in ring_push_msg). Mirror that wrap.
    if ((uint16_t)(size - tail) < 2u) {
        tail = 0;
        rh.tail = 0;
        if (head == tail) return false;
    }

    // Read frame_len (little-endian u16). frame_len == 0 is a wrap marker.
    // Bounds are guaranteed: size - tail >= 2 from the check above.
    uint16_t frame_len = (uint16_t)(data[tail] | ((uint16_t)data[tail + 1] << 8u));

    // Skip at most one wrap marker (frame_len == 0).
    // A well-formed ring has at most one wrap marker before a data frame.
    if (frame_len == 0) {
        tail = 0;
        rh.tail = 0;
        if (head == tail) return false;
        if ((uint16_t)(size - tail) < 2u) return false; // malformed
        frame_len = (uint16_t)(data[tail] | ((uint16_t)data[tail + 1] << 8u));
        if (frame_len == 0) return false; // two consecutive markers: malformed
    }

    if (frame_len < 2u) {
        // frame_len == 1 is malformed: consume 2 bytes and return.
        rh.tail = (uint16_t)((tail + 2u) % size);
        return true; // *out_msg_len stays 0
    }

    const uint16_t msg_len = (uint16_t)(frame_len - 2u);

    if (msg_len > dst_max) {
        // Too large for caller's buffer — consume the frame anyway.
        rh.tail = (uint16_t)((tail + frame_len) % size);
        return true; // *out_msg_len stays 0 (caller sees truncated)
    }

    memcpy(dst, data + tail + 2, msg_len);
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

    // Validate request invariants (spec.md §5.1 "Sequence number rules" and
    // "Message header (MsgHeader)").
    // seq MUST be nonzero.
    if (req.seq == 0 || req.status != 0 || req.reserved != 0) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    // Check declared payload fits in the frame.
    const uint16_t expected_total = (uint16_t)(sizeof(MsgHeader) + req.payload_len);
    if (expected_total > msg_len) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    // Scratch bounds check (spec.md §5.1):
    // "If scratch_ofs != 0xFFFF, then scratch_ofs + scratch_len MUST fit
    //  within the corresponding scratch buffer."
    // Requests use host→cart scratch (h2c), so bounds are API_H2C_SCRATCH_LEN.
    if (req.scratch_ofs != 0xFFFFu) {
        const uint32_t scratch_end = (uint32_t)req.scratch_ofs + req.scratch_len;
        if (scratch_end > API_H2C_SCRATCH_LEN) {
            write_response(req.seq, req.service, req.method, API_E_BAD_ARG, nullptr, 0);
            return true;
        }
    }

    const uint8_t* payload     = frame + sizeof(MsgHeader);
    const uint16_t payload_len = req.payload_len;

    // Dispatch by service.
    switch (req.service) {
        case SVC_SYSTEM:
            core_service_handle(req, payload, payload_len, *this,
                                *posture_, *policy_store_, *registry_,
                                reset_menu_fn_, device_identity_);
            break;

        case SVC_IDENTITY:
            if (profile_store_ != nullptr) {
                identity_service_handle(req, payload, payload_len,
                                        *this, *profile_store_);
            } else {
                write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            }
            break;

        case SVC_NETWORK:
            if (net_transport_ != nullptr) {
                network_service_handle(req, payload, payload_len,
                                       *this, *net_transport_);
            } else {
                write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            }
            break;

        case SVC_STORAGE:
            if (save_store_ != nullptr) {
                // Refresh active profile from ProfileStore on each request.
                uint16_t pid = (profile_store_ != nullptr)
                               ? profile_store_->active()
                               : active_profile_id_;
                storage_service_handle(req, payload, payload_len,
                                       *this, *save_store_, pid);
            } else {
                write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            }
            break;

        case SVC_USERSTATS:
            if (stats_store_ != nullptr) {
                uint16_t pid = (profile_store_ != nullptr)
                               ? profile_store_->active()
                               : active_profile_id_;
                userstats_service_handle(req, payload, payload_len,
                                          *this, *stats_store_,
                                          pid, active_payload_id_,
                                          device_identity_);
            } else {
                write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            }
            break;

        default:
            write_response(req.seq, req.service, req.method, API_E_UNSUPPORTED, nullptr, 0);
            break;
    }

    return true;
}
