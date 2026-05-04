#pragma once
// network_service.h — Network service (0x02) handler.
//
// Implements NET_STATUS (0x00) and NET_HTTP_REQUEST (0x01).
// Called from ApiWindow::service_once() when req.service == SVC_NETWORK.

#include "msx/api/api_types.h"

class ApiWindow;
class TransportEspAt;

void network_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             TransportEspAt&  transport);
