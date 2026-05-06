// api_window.cc — JLPiCart API window: ring framing and service dispatch.
//
// Frames: u16 frame_len (includes itself) followed by (frame_len-2) msg_bytes.
// frame_len == 0 is a wrap marker; consumer must wrap tail to 0 and re-read.

#include "msx/api/api_window.h"
#include "msx/api/services/services.h"
#include "store/user_data_store.h"
#include "net/transport_esp_at.h"
#include "spine/device_identity.h"
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
    h.feature_bits   = API_FEATURES_CURRENT;
    h.max_frame      = API_MAX_FRAME;

    RingHeader& rreq = ring_hdr(API_REQ_RING_OFS);
    rreq.head  = 0;
    rreq.tail  = 0;
    rreq.size  = API_RING_DATA_SIZE;
    rreq.flags = 0;

    RingHeader& rrsp = ring_hdr(API_RSP_RING_OFS);
    rrsp.head  = 0;
    rrsp.tail  = 0;
    rrsp.size  = API_RING_DATA_SIZE;
    rrsp.flags = 0;

    initialized_ = true;
}

// ---------------------------------------------------------------------------
// bind_user_data
// ---------------------------------------------------------------------------

void ApiWindow::bind_user_data(UserDataStore& uds)
{
    user_data_ = &uds;
    header().feature_bits |= API_FEATURE_IDENTITY
                           |  API_FEATURE_STORAGE
                           |  API_FEATURE_USERSTATS;
}

void ApiWindow::set_reset_menu_fn(void (*fn)())
{
    reset_menu_fn_ = fn;
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

uint16_t ApiWindow::active_profile_id() const
{
    return user_data_ ? user_data_->profile_active() : 0u;
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
// ring_push_msg
// ---------------------------------------------------------------------------

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

    const uint16_t used = (head >= tail)
        ? (uint16_t)(head - tail)
        : (uint16_t)(size - (tail - head));
    const uint16_t free_bytes = (uint16_t)(size - used - 1u);

    const uint16_t to_end = (uint16_t)(size - head);

    const uint16_t needed = (frame_len > to_end)
        ? static_cast<uint16_t>(to_end + frame_len)
        : frame_len;

    if (needed > free_bytes) {
        return API_E_RING_FULL;
    }

    if (frame_len > to_end) {
        if (to_end >= 2u) {
            data[head]     = 0x00;
            data[head + 1] = 0x00;
        }
        head = 0;
    }

    data[head]     = static_cast<uint8_t>(frame_len & 0xFFu);
    data[head + 1] = static_cast<uint8_t>(frame_len >> 8u);
    memcpy(data + head + 2, msg, msg_len);

    head = (uint16_t)((head + frame_len) % size);
    rh.head = head;

    return API_OK;
}

// ---------------------------------------------------------------------------
// ring_pop_msg
// ---------------------------------------------------------------------------

bool ApiWindow::ring_pop_msg(uint16_t ring_ofs, uint8_t* dst, uint16_t dst_max,
                             uint16_t* out_msg_len)
{
    RingHeader& rh   = ring_hdr(ring_ofs);
    uint8_t*    data = ring_data(ring_ofs);
    const uint16_t size = rh.size;

    *out_msg_len = 0;

    const uint16_t head = rh.head;
    uint16_t tail = rh.tail;

    if (head == tail) return false;

    if ((uint16_t)(size - tail) < 2u) {
        tail = 0;
        rh.tail = 0;
        if (head == tail) return false;
    }

    uint16_t frame_len = (uint16_t)(data[tail] | ((uint16_t)data[tail + 1] << 8u));

    if (frame_len == 0) {
        tail = 0;
        rh.tail = 0;
        if (head == tail) return false;
        if ((uint16_t)(size - tail) < 2u) return false;
        frame_len = (uint16_t)(data[tail] | ((uint16_t)data[tail + 1] << 8u));
        if (frame_len == 0) return false;
    }

    if (frame_len < 2u) {
        rh.tail = (uint16_t)((tail + 2u) % size);
        return true;
    }

    const uint16_t msg_len = (uint16_t)(frame_len - 2u);

    if (msg_len > dst_max) {
        rh.tail = (uint16_t)((tail + frame_len) % size);
        return true;
    }

    memcpy(dst, data + tail + 2, msg_len);
    rh.tail = (uint16_t)((tail + frame_len) % size);

    *out_msg_len = msg_len;
    return true;
}

// ---------------------------------------------------------------------------
// write_response
// ---------------------------------------------------------------------------

void ApiWindow::write_response(uint16_t seq, uint8_t service, uint8_t method,
                               uint16_t status,
                               const uint8_t* payload, uint16_t payload_len)
{
    uint8_t rsp[API_MAX_MSG];
    MsgHeader hdr = {};
    hdr.seq         = seq;
    hdr.service     = service;
    hdr.method      = method;
    hdr.flags       = 0;
    hdr.status      = status;
    hdr.payload_len = payload_len;
    hdr.scratch_ofs = 0xFFFFu;
    hdr.scratch_len = 0;
    hdr.reserved    = 0;

    const uint16_t total_msg = static_cast<uint16_t>(sizeof(MsgHeader) + payload_len);
    if (total_msg > API_MAX_MSG) {
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

    regs().cart_event = static_cast<uint8_t>(regs().cart_event + 1u);
}

// ---------------------------------------------------------------------------
// service_once
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
        return true;
    }

    MsgHeader req;
    memcpy(&req, frame, sizeof(MsgHeader));

    if (req.seq == 0 || req.status != 0 || req.reserved != 0) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    const uint16_t expected_total = (uint16_t)(sizeof(MsgHeader) + req.payload_len);
    if (expected_total > msg_len) {
        write_response(req.seq, req.service, req.method, API_E_BAD_REQ, nullptr, 0);
        return true;
    }

    if (req.scratch_ofs != 0xFFFFu) {
        const uint32_t scratch_end = (uint32_t)req.scratch_ofs + req.scratch_len;
        if (scratch_end > API_H2C_SCRATCH_LEN) {
            write_response(req.seq, req.service, req.method, API_E_BAD_ARG, nullptr, 0);
            return true;
        }
    }

    const uint8_t* payload     = frame + sizeof(MsgHeader);
    const uint16_t payload_len = req.payload_len;

    switch (req.service) {
        case SVC_SYSTEM:
            core_service_handle(req, payload, payload_len, *this,
                                *posture_, *policy_store_, *registry_,
                                reset_menu_fn_, device_identity_);
            break;

        case SVC_IDENTITY:
            if (user_data_ != nullptr) {
                identity_service_handle(req, payload, payload_len,
                                        *this, *user_data_);
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
            if (user_data_ != nullptr) {
                storage_service_handle(req, payload, payload_len,
                                       *this, *user_data_);
            } else {
                write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            }
            break;

        case SVC_USERSTATS:
            if (user_data_ != nullptr) {
                userstats_service_handle(req, payload, payload_len,
                                          *this, *user_data_,
                                          active_payload_id_,
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
