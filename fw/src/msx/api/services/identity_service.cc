// identity_service.cc — Identity service (0x03) implementation.

#include "msx/api/services/services.h"
#include "msx/api/api_window.h"
#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// LIST_PROFILES (0x00)
// ---------------------------------------------------------------------------

static void handle_list(const MsgHeader& req, ApiWindow& win, UserDataStore& uds)
{
    ProfileRecord recs[PROF_MAX_PROFILES];
    uint8_t n = uds.profile_list(recs, PROF_MAX_PROFILES);

    uint8_t buf[API_MAX_MSG - sizeof(MsgHeader)];
    uint16_t pos = 0;

    buf[pos++] = static_cast<uint8_t>(n);
    buf[pos++] = 0;

    for (uint8_t i = 0; i < n; ++i) {
        buf[pos++] = static_cast<uint8_t>(recs[i].profile_id & 0xFFu);
        buf[pos++] = static_cast<uint8_t>(recs[i].profile_id >> 8u);

        uint8_t name_len = 0;
        while (name_len < PROF_API_NAME_MAX && recs[i].name[name_len] != '\0')
            ++name_len;
        buf[pos++] = name_len;

        if (pos + name_len > sizeof(buf)) {
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
// ---------------------------------------------------------------------------

static void handle_set_active(const MsgHeader& req,
                               const uint8_t* payload, uint16_t payload_len,
                               ApiWindow& win, UserDataStore& uds)
{
    if (payload_len < 2u) {
        win.write_response(req.seq, req.service, req.method,
                           API_E_BAD_REQ, nullptr, 0);
        return;
    }

    uint16_t id = static_cast<uint16_t>(payload[0] | (payload[1] << 8u));
    DiagStatus s = uds.profile_set_active(id);

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
// ---------------------------------------------------------------------------

static void handle_get_active(const MsgHeader& req, ApiWindow& win,
                               const UserDataStore& uds)
{
    uint8_t  buf[3];
    uint16_t id = uds.profile_active();
    buf[0] = static_cast<uint8_t>(id & 0xFFu);
    buf[1] = static_cast<uint8_t>(id >> 8u);
    buf[2] = 0u;

    win.write_response(req.seq, req.service, req.method, API_OK, buf, 3u);
}

// ---------------------------------------------------------------------------
// identity_service_handle — dispatcher
// ---------------------------------------------------------------------------

void identity_service_handle(const MsgHeader& req,
                              const uint8_t*   payload,
                              uint16_t         payload_len,
                              ApiWindow&       win,
                              UserDataStore&   uds)
{
    switch (req.method) {
        case IDN_LIST_PROFILES:
            handle_list(req, win, uds);
            break;

        case IDN_SET_ACTIVE_PROFILE:
            handle_set_active(req, payload, payload_len, win, uds);
            break;

        case IDN_GET_ACTIVE_PROFILE:
            handle_get_active(req, win, uds);
            break;

        case IDN_GUEST_BEGIN: {
            uds.profile_begin_guest();
            uint8_t resp[2];
            resp[0] = static_cast<uint8_t>(PROF_ID_GUEST & 0xFFu);
            resp[1] = static_cast<uint8_t>(PROF_ID_GUEST >> 8u);
            win.write_response(req.seq, req.service, req.method,
                               API_OK, resp, sizeof(resp));
            break;
        }
        case IDN_GUEST_END: {
            uds.profile_end_guest();
            uint16_t restored = uds.profile_active();
            uint8_t resp[2];
            resp[0] = static_cast<uint8_t>(restored & 0xFFu);
            resp[1] = static_cast<uint8_t>(restored >> 8u);
            win.write_response(req.seq, req.service, req.method,
                               API_OK, resp, sizeof(resp));
            break;
        }

        default:
            win.write_response(req.seq, req.service, req.method,
                               API_E_UNSUPPORTED, nullptr, 0);
            break;
    }
}
