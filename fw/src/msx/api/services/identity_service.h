#pragma once
// identity_service.h — Identity service (0x03) handler for the JLPiCart API.
//
// Handles: LIST_PROFILES, SET_ACTIVE_PROFILE, GET_ACTIVE_PROFILE,
//          GUEST_BEGIN (stub), GUEST_END (stub).

#include "msx/api/api_types.h"
#include "store/profile_store.h"

class ApiWindow;

// Dispatch an Identity service request.
// Called from ApiWindow::service_once() when req.service == SVC_IDENTITY.
void identity_service_handle(const MsgHeader&  req,
                              const uint8_t*    payload,
                              uint16_t          payload_len,
                              ApiWindow&        win,
                              ProfileStore&     ps);
