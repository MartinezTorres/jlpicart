#pragma once
// api_window.h — JLPiCart API window: ring framing and service dispatch.
//
// ApiWindow owns a 16KB buffer that the bus layer maps into MSX Z80 page 2
// (0x8000–0xBFFF in subslot 2).  The Z80 reads/writes frames in the request
// and response rings; the firmware reads requests and posts responses.
//

#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy.h"
#include "spine/capability_registry.h"
#include <cstddef>

class UserDataStore;
class DeviceIdentity;
class TransportEspAt;

class ApiWindow {
public:
    static constexpr size_t WIN_BYTES = API_WINDOW_SIZE; // 16 384 bytes

    // Initialize the window: write the header, init rings, store spine refs.
    // Must be called before any other method.
    void init(const SecurityPosture& posture,
              const PolicyStore&     policy_store,
              const CapabilityRegistry& registry);

    // Raw 16KB buffer.  The bus layer maps this into MSX page 2.
    uint8_t*       buf()       { return buf_; }
    const uint8_t* buf() const { return buf_; }

    bool initialized() const { return initialized_; }

    // Bind a UserDataStore and enable API_FEATURE_STORAGE in the window header.
    // Must be called after init().  Safe to call more than once (rebinds).
    void bind_user_data(UserDataStore& uds);

    // Register a callback invoked when the cartridge requests RESET_TO_MENU.
    // fn may be nullptr (disables the callback).
    void set_reset_menu_fn(void (*fn)());

    // Bind the ESP32 AT transport and enable API_FEATURE_NETWORK (Stage 32).
    void bind_network_transport(TransportEspAt& t);

    // Bind the Device Identity Key (Stage 22).
    void bind_device_identity(DeviceIdentity& dik);

    // Active payload ID — set by MenuApp during LAUNCH, cleared on cancel.
    void set_active_payload(const char* payload_id);
    const char* active_payload_id() const { return active_payload_id_; }

    // Read one request frame from the request ring, dispatch to the appropriate
    // service handler, and write one response frame to the response ring.
    // Returns true if a frame was processed, false if the request ring was empty.
    bool service_once();

    // ---------------------------------------------------------------------------
    // Low-level ring operations (also used by host tests)
    // ---------------------------------------------------------------------------

    uint16_t ring_push_msg(uint16_t ring_ofs, const uint8_t* msg, uint16_t msg_len);

    bool ring_pop_msg(uint16_t ring_ofs, uint8_t* dst, uint16_t dst_max,
                      uint16_t* out_msg_len);

    bool ring_empty(uint16_t ring_ofs) const;

    void write_response(uint16_t seq, uint8_t service, uint8_t method,
                        uint16_t status,
                        const uint8_t* payload, uint16_t payload_len);

private:
    alignas(4) uint8_t buf_[WIN_BYTES] = {};

    const SecurityPosture*    posture_          = nullptr;
    const PolicyStore*        policy_store_     = nullptr;
    const CapabilityRegistry* registry_         = nullptr;
    UserDataStore*            user_data_        = nullptr;
    DeviceIdentity*           device_identity_  = nullptr;
    TransportEspAt*           net_transport_    = nullptr;
    char                      active_payload_id_[64] = {};
    void                    (*reset_menu_fn_)() = nullptr;

    bool initialized_ = false;

    ApiWindowHeader& header()    { return *reinterpret_cast<ApiWindowHeader*>(buf_); }
    ApiRegs&         regs()      { return *reinterpret_cast<ApiRegs*>(buf_ + API_REGS_OFS); }
    RingHeader& ring_hdr(uint16_t ring_ofs) {
        return *reinterpret_cast<RingHeader*>(buf_ + ring_ofs);
    }
    uint8_t* ring_data(uint16_t ring_ofs) {
        return buf_ + ring_ofs + API_RING_HDR_SIZE;
    }
};
