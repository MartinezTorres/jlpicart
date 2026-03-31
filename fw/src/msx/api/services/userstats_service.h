#pragma once
// userstats_service.h — UserStats service (0x04) dispatch (Stage 21/22).

#include "msx/api/api_types.h"
#include <cstdint>

class ApiWindow;
class StatsStore;
class DeviceIdentity;

// Dispatch one UserStats request.
// payload/payload_len are the bytes after MsgHeader in the request frame.
// active_payload_id is taken from ApiWindow::active_payload_id().
// dik: if non-null, leaderboard submissions are signed with the device key.
void userstats_service_handle(const MsgHeader& req,
                               const uint8_t*   payload,
                               uint16_t         payload_len,
                               ApiWindow&       win,
                               StatsStore&      stats_store,
                               uint16_t         active_profile_id,
                               const char*      active_payload_id,
                               DeviceIdentity*  dik = nullptr);
