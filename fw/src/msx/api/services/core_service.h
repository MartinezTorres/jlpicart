#pragma once
// core_service.h — System service (0x00) handler for the JLPiCart API.
//
// Handles: GET_API_INFO, GET_DEVICE_ID, GET_CAPS, GET_RANDOM, RESET_TO_MENU,
//          GET_SECURITY_INFO, GET_POLICY_FLAGS.
// Spec reference: spec.md §5.1 "System service (0x00)".

#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"

// Forward declaration to avoid a circular include.
class ApiWindow;

// Dispatch a System service request.
// Called from ApiWindow::service_once() when req.service == SVC_SYSTEM.
void core_service_handle(const MsgHeader&         req,
                         const uint8_t*           payload,
                         uint16_t                 payload_len,
                         ApiWindow&               win,
                         const SecurityPosture&   posture,
                         const PolicyStore&       policy_store,
                         const CapabilityRegistry& registry);

// Build the posture_props bitfield from a SecurityPosture.
// Exported for use by both GET_API_INFO and GET_SECURITY_INFO.
uint32_t posture_to_props(const SecurityPosture& posture);
