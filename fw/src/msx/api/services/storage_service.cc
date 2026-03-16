// storage_service.cc — Storage service (0x01) implementation (Stage 19).
//
// Spec reference: spec.md §7.2 "Storage service (0x01)".

#include "msx/api/services/storage_service.h"
#include "msx/api/api_window.h"
#include "storage/save_store.h"
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

static uint16_t diag_to_api(DiagCode code)
{
    switch (code) {
        case DiagCode::STORAGE_NOT_FOUND: return API_E_NOT_FOUND;
        case DiagCode::STORAGE_FULL:      return API_E_RING_FULL; // nearest "full" code
        default:                           return API_E_INTERNAL;
    }
}

// ---------------------------------------------------------------------------
// STG_LIST_BLOBS (0x00)
//
// Request payload: { u8 blob_kind }
// Response payload: { u16 count, entry... }
//   entry: { u16 blob_id, u16 flags, u16 size, u16 max_bytes }
// ---------------------------------------------------------------------------

static void handle_list_blobs(const MsgHeader& req,
                               const uint8_t* payload, uint16_t payload_len,
                               ApiWindow& win, SaveStore& ss,
                               uint16_t profile_id)
{
    uint8_t kind = 0u;
    if (payload_len >= 1u) kind = payload[0];

    BlobInfo infos[16];
    uint8_t n = ss.list(profile_id, kind, infos,
                        static_cast<uint8_t>(sizeof(infos)/sizeof(infos[0])));

    // Build payload: u16 count + n × 8-byte entries.
    uint8_t buf[2 + 16 * 8];
    buf[0] = static_cast<uint8_t>(n);
    buf[1] = 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t* e = buf + 2 + i * 8;
        memcpy(e + 0, &infos[i].blob_id,   2);
        memcpy(e + 2, &infos[i].flags,     2);
        memcpy(e + 4, &infos[i].size,      2);
        memcpy(e + 6, &infos[i].max_bytes, 2);
    }
    send_ok(win, req, buf, static_cast<uint16_t>(2u + n * 8u));
}

// ---------------------------------------------------------------------------
// STG_READ_BLOB (0x01)
//
// Request payload: { u16 blob_id, u32 offset, u16 len }
// Response: data placed in c2h scratch; response scratch_len = bytes_read.
// ---------------------------------------------------------------------------

static void handle_read_blob(const MsgHeader& req,
                              const uint8_t* payload, uint16_t payload_len,
                              ApiWindow& win, SaveStore& ss,
                              uint16_t profile_id)
{
    if (payload_len < 8u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t blob_id;
    uint32_t offset;
    uint16_t len;
    memcpy(&blob_id, payload + 0, 2);
    memcpy(&offset,  payload + 2, 4);
    memcpy(&len,     payload + 6, 2);

    if (len > API_C2H_SCRATCH_LEN) len = static_cast<uint16_t>(API_C2H_SCRATCH_LEN);

    uint8_t* scratch = win.buf() + API_C2H_SCRATCH_OFS;
    DiagStatus s = ss.read(profile_id, blob_id, offset, scratch, len);
    if (!s.ok()) {
        send_err(win, req, diag_to_api(s.code));
        return;
    }

    // Respond with scratch_len = bytes placed in scratch.
    MsgHeader rsp = {};
    rsp.seq         = req.seq;
    rsp.service     = req.service;
    rsp.method      = req.method;
    rsp.status      = API_OK;
    rsp.payload_len = 0;
    rsp.scratch_ofs = 0u;
    rsp.scratch_len = len;
    rsp.reserved    = 0;
    uint8_t rsp_buf[sizeof(MsgHeader)];
    memcpy(rsp_buf, &rsp, sizeof(MsgHeader));
    win.ring_push_msg(API_RSP_RING_OFS, rsp_buf, sizeof(MsgHeader));
    ApiRegs* regs = reinterpret_cast<ApiRegs*>(win.buf() + API_REGS_OFS);
    regs->cart_event = static_cast<uint8_t>(regs->cart_event + 1u);
}

// ---------------------------------------------------------------------------
// STG_WRITE_BLOB_BEGIN (0x02)
//
// Request payload: { u16 blob_id, u16 total_len, u16 flags }
// Response payload: { u16 handle, u16 chunk_hint }
// ---------------------------------------------------------------------------

static void handle_write_begin(const MsgHeader& req,
                                const uint8_t* payload, uint16_t payload_len,
                                ApiWindow& win, SaveStore& ss,
                                uint16_t profile_id)
{
    if (payload_len < 6u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t blob_id, total_len, flags;
    memcpy(&blob_id,   payload + 0, 2);
    memcpy(&total_len, payload + 2, 2);
    memcpy(&flags,     payload + 4, 2);

    uint8_t handle = 0;
    DiagStatus s = ss.write_begin(profile_id, blob_id, total_len, flags, &handle);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }

    uint8_t rsp_buf[4];
    uint16_t handle16   = static_cast<uint16_t>(handle);
    uint16_t chunk_hint = API_H2C_SCRATCH_LEN;
    memcpy(rsp_buf + 0, &handle16,   2);
    memcpy(rsp_buf + 2, &chunk_hint, 2);
    send_ok(win, req, rsp_buf, 4u);
}

// ---------------------------------------------------------------------------
// STG_WRITE_BLOB_CHUNK (0x03)
//
// Request payload: { u16 handle, u32 offset, u16 len }
// Data comes from h2c scratch (req.scratch_ofs, req.scratch_len).
// ---------------------------------------------------------------------------

static void handle_write_chunk(const MsgHeader& req,
                                const uint8_t* payload, uint16_t payload_len,
                                ApiWindow& win, SaveStore& ss)
{
    if (payload_len < 8u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t handle16;
    uint32_t offset;
    uint16_t len;
    memcpy(&handle16, payload + 0, 2);
    memcpy(&offset,   payload + 2, 4);
    memcpy(&len,      payload + 6, 2);

    if (handle16 >= SAVE_WRITE_HANDLES) {
        send_err(win, req, API_E_NOT_FOUND);
        return;
    }
    uint8_t handle = static_cast<uint8_t>(handle16);

    // Read data from h2c scratch.
    const uint8_t* data = win.buf() + API_H2C_SCRATCH_OFS;
    if (req.scratch_ofs != 0xFFFFu) {
        data = win.buf() + API_H2C_SCRATCH_OFS + req.scratch_ofs;
    }
    if (len > req.scratch_len) len = req.scratch_len;

    DiagStatus s = ss.write_chunk(handle, offset, data, len);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }

    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// STG_WRITE_BLOB_COMMIT (0x04)
//
// Request payload: { u16 handle }
// ---------------------------------------------------------------------------

static void handle_write_commit(const MsgHeader& req,
                                 const uint8_t* payload, uint16_t payload_len,
                                 ApiWindow& win, SaveStore& ss)
{
    if (payload_len < 2u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t handle16;
    memcpy(&handle16, payload, 2);
    if (handle16 >= SAVE_WRITE_HANDLES) {
        send_err(win, req, API_E_NOT_FOUND);
        return;
    }

    DiagStatus s = ss.write_commit(static_cast<uint8_t>(handle16));
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }
    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// STG_DELETE_BLOB (0x05)
//
// Request payload: { u16 blob_id }
// ---------------------------------------------------------------------------

static void handle_delete_blob(const MsgHeader& req,
                                const uint8_t* payload, uint16_t payload_len,
                                ApiWindow& win, SaveStore& ss,
                                uint16_t profile_id)
{
    if (payload_len < 2u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t blob_id;
    memcpy(&blob_id, payload, 2);

    DiagStatus s = ss.delete_blob(profile_id, blob_id);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }
    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// storage_service_handle — dispatcher
// ---------------------------------------------------------------------------

void storage_service_handle(const MsgHeader& req,
                              const uint8_t*   payload,
                              uint16_t         payload_len,
                              ApiWindow&       win,
                              SaveStore&       save_store,
                              uint16_t         active_profile_id)
{
    switch (req.method) {
        case STG_LIST_BLOBS:
            handle_list_blobs(req, payload, payload_len, win,
                              save_store, active_profile_id);
            break;
        case STG_READ_BLOB:
            handle_read_blob(req, payload, payload_len, win,
                             save_store, active_profile_id);
            break;
        case STG_WRITE_BLOB_BEGIN:
            handle_write_begin(req, payload, payload_len, win,
                               save_store, active_profile_id);
            break;
        case STG_WRITE_BLOB_CHUNK:
            handle_write_chunk(req, payload, payload_len, win, save_store);
            break;
        case STG_WRITE_BLOB_COMMIT:
            handle_write_commit(req, payload, payload_len, win, save_store);
            break;
        case STG_DELETE_BLOB:
            handle_delete_blob(req, payload, payload_len, win,
                               save_store, active_profile_id);
            break;
        default:
            win.write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            break;
    }
}
