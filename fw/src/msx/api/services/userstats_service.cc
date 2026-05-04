// userstats_service.cc — UserStats service (0x04) implementation.

#include "msx/api/services/userstats_service.h"
#include "msx/api/api_window.h"
#include "stats/stats_store.h"
#include "identity/device_identity.h"
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
        case DiagCode::STORAGE_FULL:      return API_E_RING_FULL;
        default:                           return API_E_INTERNAL;
    }
}

// ---------------------------------------------------------------------------
// UST_STAT_GET (0x00)
//
// Request payload: { u16 stat_id }
// Response payload: { s32 value }
// ---------------------------------------------------------------------------

static void handle_stat_get(const MsgHeader& req,
                              const uint8_t* payload, uint16_t payload_len,
                              ApiWindow& win, StatsStore& ss,
                              uint16_t profile_id, const char* payload_id)
{
    if (payload_len < 2u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t stat_id;
    memcpy(&stat_id, payload, 2u);

    int32_t value = 0;
    DiagStatus s = ss.stat_get(profile_id, payload_id, stat_id, &value);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }

    uint8_t rsp[4];
    memcpy(rsp, &value, 4u);
    send_ok(win, req, rsp, 4u);
}

// ---------------------------------------------------------------------------
// UST_STAT_SET (0x01)
//
// Request payload: { u16 stat_id, s32 value, u8 op }
// Response: OK
// ---------------------------------------------------------------------------

static void handle_stat_set(const MsgHeader& req,
                              const uint8_t* payload, uint16_t payload_len,
                              ApiWindow& win, StatsStore& ss,
                              uint16_t profile_id, const char* payload_id)
{
    if (payload_len < 7u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t stat_id;
    int32_t  value;
    uint8_t  op = payload[6];
    memcpy(&stat_id, payload + 0u, 2u);
    memcpy(&value,   payload + 2u, 4u);

    DiagStatus s = ss.stat_set(profile_id, payload_id, stat_id, value, op);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }
    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// UST_ACH_UNLOCK (0x02)
//
// Request payload: { u16 ach_id }
// Response: OK
// ---------------------------------------------------------------------------

static void handle_ach_unlock(const MsgHeader& req,
                                const uint8_t* payload, uint16_t payload_len,
                                ApiWindow& win, StatsStore& ss,
                                uint16_t profile_id, const char* payload_id)
{
    if (payload_len < 2u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t ach_id;
    memcpy(&ach_id, payload, 2u);

    DiagStatus s = ss.ach_unlock(profile_id, payload_id, ach_id);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }
    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// UST_LEADER_RUN_BEGIN (0x03)
//
// Request payload: { u16 lb_id }
// Response payload: { u8 handle }; token placed in c2h scratch (scratch_len=16)
// ---------------------------------------------------------------------------

static void handle_leader_begin(const MsgHeader& req,
                                  const uint8_t* payload, uint16_t payload_len,
                                  ApiWindow& win, StatsStore& ss,
                                  uint16_t profile_id, const char* payload_id)
{
    if (payload_len < 2u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint16_t lb_id;
    memcpy(&lb_id, payload, 2u);

    uint8_t* scratch = win.buf() + API_C2H_SCRATCH_OFS;
    uint8_t  handle  = 0u;
    DiagStatus s = ss.leader_begin(profile_id, payload_id, lb_id,
                                    scratch, &handle);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }

    // Respond: payload = {u8 handle}, scratch_len = STATS_TOKEN_LEN.
    MsgHeader rsp = {};
    rsp.seq         = req.seq;
    rsp.service     = req.service;
    rsp.method      = req.method;
    rsp.status      = API_OK;
    rsp.payload_len = 1u;
    rsp.scratch_ofs = 0u;
    rsp.scratch_len = STATS_TOKEN_LEN;
    rsp.reserved    = 0u;

    uint8_t rsp_buf[sizeof(MsgHeader) + 1u];
    memcpy(rsp_buf, &rsp, sizeof(MsgHeader));
    rsp_buf[sizeof(MsgHeader)] = handle;
    win.ring_push_msg(API_RSP_RING_OFS, rsp_buf,
                      static_cast<uint16_t>(sizeof(MsgHeader) + 1u));

    ApiRegs* regs = reinterpret_cast<ApiRegs*>(win.buf() + API_REGS_OFS);
    regs->cart_event = static_cast<uint8_t>(regs->cart_event + 1u);
}

// ---------------------------------------------------------------------------
// UST_LEADER_SUBMIT (0x04)
//
// Request payload: { u8 handle, u32 score, u8 proof_kind, u16 proof_len }
// proof data in h2c scratch.
// Response: OK
// ---------------------------------------------------------------------------

static void handle_leader_submit(const MsgHeader& req,
                                   const uint8_t* payload, uint16_t payload_len,
                                   ApiWindow& win, StatsStore& ss, DeviceIdentity* dik)
{
    if (payload_len < 8u) { send_err(win, req, API_E_BAD_REQ); return; }

    uint8_t  handle     = payload[0];
    uint32_t score;
    uint8_t  proof_kind = payload[5];
    uint16_t proof_len;
    memcpy(&score,     payload + 1u, 4u);
    memcpy(&proof_len, payload + 6u, 2u);

    const uint8_t* proof_buf = win.buf() + API_H2C_SCRATCH_OFS;
    if (req.scratch_ofs != 0xFFFFu) {
        proof_buf = win.buf() + API_H2C_SCRATCH_OFS + req.scratch_ofs;
    }
    if (proof_len > req.scratch_len) proof_len = req.scratch_len;

    DiagStatus s = ss.leader_submit(handle, score, proof_kind, proof_buf, proof_len, dik);
    if (!s.ok()) { send_err(win, req, diag_to_api(s.code)); return; }
    send_ok(win, req);
}

// ---------------------------------------------------------------------------
// userstats_service_handle — dispatcher
// ---------------------------------------------------------------------------

void userstats_service_handle(const MsgHeader& req,
                                const uint8_t*   payload,
                                uint16_t         payload_len,
                                ApiWindow&       win,
                                StatsStore&      stats_store,
                                uint16_t         active_profile_id,
                                const char*      active_payload_id,
                                DeviceIdentity*  dik)
{
    switch (req.method) {
        case UST_STAT_GET:
            handle_stat_get(req, payload, payload_len, win,
                            stats_store, active_profile_id, active_payload_id);
            break;
        case UST_STAT_SET:
            handle_stat_set(req, payload, payload_len, win,
                            stats_store, active_profile_id, active_payload_id);
            break;
        case UST_ACH_UNLOCK:
            handle_ach_unlock(req, payload, payload_len, win,
                              stats_store, active_profile_id, active_payload_id);
            break;
        case UST_LEADER_RUN_BEGIN:
            handle_leader_begin(req, payload, payload_len, win,
                                stats_store, active_profile_id, active_payload_id);
            break;
        case UST_LEADER_SUBMIT:
            handle_leader_submit(req, payload, payload_len, win, stats_store, dik);
            break;
        default:
            win.write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0u);
            break;
    }
}
