// identity_service.cc — Identity service (0x03) implementation.

#include "msx/api/services/identity_service.h"
#include "msx/api/api_window.h"
#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// LIST_PROFILES (0x00)
//
// Response payload: { u16 count, entry... }
// Each entry: { u16 profile_id, u8 name_len, name_bytes[name_len] }
// Names are capped at PROF_API_NAME_MAX bytes to fit in API_MAX_MSG.
// ---------------------------------------------------------------------------

static void handle_list(const MsgHeader& req, ApiWindow& win, ProfileStore& ps)
{
    ProfileRecord recs[PROF_MAX_PROFILES];
    uint8_t n = ps.list(recs, PROF_MAX_PROFILES);

    // Build payload: u16 count + variable-length entries.
    uint8_t buf[API_MAX_MSG - sizeof(MsgHeader)];
    uint16_t pos = 0;

    // count (little-endian)
    buf[pos++] = static_cast<uint8_t>(n);
    buf[pos++] = 0;

    for (uint8_t i = 0; i < n; ++i) {
        // profile_id
        buf[pos++] = static_cast<uint8_t>(recs[i].profile_id & 0xFFu);
        buf[pos++] = static_cast<uint8_t>(recs[i].profile_id >> 8u);

        // name (capped to PROF_API_NAME_MAX, no null terminator in wire format)
        uint8_t name_len = 0;
        while (name_len < PROF_API_NAME_MAX && recs[i].name[name_len] != '\0')
            ++name_len;
        buf[pos++] = name_len;

        // Guard: ensure we don't overflow buf
        if (pos + name_len > sizeof(buf)) {
            // Truncate to what fits (shouldn't happen with PROF_MAX_PROFILES=8
            // and PROF_API_NAME_MAX=16).
            name_len = static_cast<uint8_t>(sizeof(buf) - pos);
            buf[pos - 1] = name_len;
        }
        memcpy(buf + pos, recs[i].name, name_len);
        pos = static_cast<uint16_t>(pos + name_len);
    }

    win.write_response(req.seq, req.service, req.method,
                       API_OK, buf, pos);
}

// ---------------------------------------------------------------------------
// SET_ACTIVE_PROFILE (0x01)
//
// Request payload: { u16 profile_id }
// ---------------------------------------------------------------------------

static void handle_set_active(const MsgHeader& req,
                               const uint8_t* payload, uint16_t payload_len,
                               ApiWindow& win, ProfileStore& ps)
{
    if (payload_len < 2u) {
        win.write_response(req.seq, req.service, req.method,
                           API_E_BAD_REQ, nullptr, 0);
        return;
    }

    uint16_t id = static_cast<uint16_t>(payload[0] | (payload[1] << 8u));
    DiagStatus s = ps.set_active(id);

    uint16_t status = API_OK;
    if (!s.ok()) {
        status = (s.code == DiagCode::STORAGE_NOT_FOUND)
                 ? API_E_NOT_FOUND
                 : API_E_INTERNAL;
    }

    win.write_response(req.seq, req.service, req.method, status, nullptr, 0);
}

// ---------------------------------------------------------------------------
// GET_ACTIVE_PROFILE (0x02)
//
// Response payload: { u16 profile_id, u8 flags }
// ---------------------------------------------------------------------------

static void handle_get_active(const MsgHeader& req, ApiWindow& win,
                               const ProfileStore& ps)
{
    uint8_t  buf[3];
    uint16_t id = ps.active();
    buf[0] = static_cast<uint8_t>(id & 0xFFu);
    buf[1] = static_cast<uint8_t>(id >> 8u);
    buf[2] = 0u; // flags reserved

    win.write_response(req.seq, req.service, req.method, API_OK, buf, 3u);
}

// ---------------------------------------------------------------------------
// identity_service_handle — dispatcher
// ---------------------------------------------------------------------------

void identity_service_handle(const MsgHeader& req,
                              const uint8_t*   payload,
                              uint16_t         payload_len,
                              ApiWindow&       win,
                              ProfileStore&    ps)
{
    switch (req.method) {
        case IDN_LIST_PROFILES:
            handle_list(req, win, ps);
            break;

        case IDN_SET_ACTIVE_PROFILE:
            handle_set_active(req, payload, payload_len, win, ps);
            break;

        case IDN_GET_ACTIVE_PROFILE:
            handle_get_active(req, win, ps);
            break;

        case IDN_GUEST_BEGIN:
        case IDN_GUEST_END:
            // Stub: guest sessions not implemented in Stage 15.
            win.write_response(req.seq, req.service, req.method,
                               API_OK, nullptr, 0);
            break;

        default:
            win.write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            break;
    }
}
