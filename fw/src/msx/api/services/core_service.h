#pragma once
// core_service.h — System service (0x00) handler for the JLPiCart API.
//
// Handles: GET_API_INFO, GET_DEVICE_ID, GET_CAPS, GET_RANDOM, RESET_TO_MENU,
//          GET_SECURITY_INFO, GET_POLICY_FLAGS.

#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"

// Forward declarations.
class ApiWindow;
class DeviceIdentity;

// Dispatch a System service request.
// Called from ApiWindow::service_once() when req.service == SVC_SYSTEM.
// reset_menu_fn is called (if non-null) when SYS_RESET_TO_MENU is processed.
// device_identity may be nullptr (GET_DEVICE_ID returns E_UNSUPPORTED if absent).
void core_service_handle(const MsgHeader&          req,
                         const uint8_t*            payload,
                         uint16_t                  payload_len,
                         ApiWindow&                win,
                         const SecurityPosture&    posture,
                         const PolicyStore&        policy_store,
                         const CapabilityRegistry& registry,
                         void                    (*reset_menu_fn)() = nullptr,
                         const DeviceIdentity*     device_identity  = nullptr);

// Build the posture_props bitfield from a SecurityPosture.
// Exported for use by both GET_API_INFO and GET_SECURITY_INFO.
uint32_t posture_to_props(const SecurityPosture& posture);
