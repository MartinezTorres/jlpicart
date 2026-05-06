#pragma once
// services.h — API service handler declarations (services 0x00–0x04).

#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "store/user_data_store.h"
#include <cstdint>

class ApiWindow;
class DeviceIdentity;
class TransportEspAt;

// System service (0x00): GET_API_INFO, GET_DEVICE_ID, GET_CAPS, GET_RANDOM,
//                        RESET_TO_MENU, GET_SECURITY_INFO, GET_POLICY_FLAGS.
void core_service_handle(const MsgHeader&          req,
                         const uint8_t*            payload,
                         uint16_t                  payload_len,
                         ApiWindow&                win,
                         const SecurityPosture&    posture,
                         const PolicyStore&        policy_store,
                         const CapabilityRegistry& registry,
                         void                    (*reset_menu_fn)() = nullptr,
                         const DeviceIdentity*     device_identity  = nullptr);

uint32_t posture_to_props(const SecurityPosture& posture);

// Storage service (0x01): LIST_BLOBS, READ_BLOB, WRITE_BLOB_BEGIN/CHUNK/COMMIT,
//                         DELETE_BLOB.
void storage_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             UserDataStore&   uds);

// Network service (0x02): NET_STATUS, NET_HTTP_REQUEST.
void network_service_handle(const MsgHeader& req,
                             const uint8_t*   payload,
                             uint16_t         payload_len,
                             ApiWindow&       win,
                             TransportEspAt&  transport);

// Identity service (0x03): LIST_PROFILES, SET/GET_ACTIVE_PROFILE,
//                           GUEST_BEGIN, GUEST_END.
void identity_service_handle(const MsgHeader&  req,
                              const uint8_t*    payload,
                              uint16_t          payload_len,
                              ApiWindow&        win,
                              UserDataStore&    uds);

// UserStats service (0x04): achievements, stats, leaderboards.
void userstats_service_handle(const MsgHeader& req,
                               const uint8_t*   payload,
                               uint16_t         payload_len,
                               ApiWindow&       win,
                               UserDataStore&   uds,
                               const char*      active_payload_id,
                               DeviceIdentity*  dik = nullptr);
