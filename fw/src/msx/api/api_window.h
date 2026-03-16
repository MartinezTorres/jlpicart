#pragma once
// api_window.h — JLPiCart API window: ring framing and service dispatch.
//
// ApiWindow owns a 16KB buffer that the bus layer maps into MSX Z80 page 2
// (0x8000–0xBFFF in subslot 2).  The Z80 reads/writes frames in the request
// and response rings; the firmware reads requests and posts responses.
//
// Spec reference: spec.md §5.1.  Bootstrapping: bootstrapping.md §4.1.

#include "msx/api/api_types.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "profiles/profile_store.h"
#include <cstddef>

class SaveStore;

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

    // Bind a ProfileStore and enable API_FEATURE_IDENTITY in the window header.
    // Must be called after init().  Safe to call more than once (rebinds).
    void bind_profile_store(ProfileStore& ps);

    // Register a callback invoked when the cartridge requests RESET_TO_MENU
    // (Stage 18).  The callback is called from service_once() on Core 1.
    // fn may be nullptr (disables the callback).
    void set_reset_menu_fn(void (*fn)());

    // Bind a SaveStore and enable API_FEATURE_STORAGE (Stage 19).
    void bind_save_store(SaveStore& ss);

    // Active profile ID for storage and stats services (Stage 19+).
    // Updated by bind_profile_store or set directly.
    uint16_t active_profile_id() const { return active_profile_id_; }

    // Read one request frame from the request ring, dispatch to the appropriate
    // service handler, and write one response frame to the response ring.
    // Returns true if a frame was processed, false if the request ring was empty.
    bool service_once();

    // ---------------------------------------------------------------------------
    // Low-level ring operations (also used by host tests)
    // ---------------------------------------------------------------------------

    // Push msg_len bytes of msg_bytes into the ring at ring_ofs as a framed message.
    // Adds the 2-byte frame_len prefix internally.
    // Returns API_OK or API_E_RING_FULL.
    uint16_t ring_push_msg(uint16_t ring_ofs, const uint8_t* msg, uint16_t msg_len);

    // Pop one framed message from the ring at ring_ofs into dst (dst_max bytes).
    // Sets *out_msg_len to the number of message bytes (frame_len - 2).
    // Returns true if a frame was available; false if the ring was empty.
    // On a malformed frame (frame_len < 2), consumes and sets *out_msg_len = 0.
    bool ring_pop_msg(uint16_t ring_ofs, uint8_t* dst, uint16_t dst_max,
                      uint16_t* out_msg_len);

    // True when head == tail (ring is empty).
    bool ring_empty(uint16_t ring_ofs) const;

    // Write a response (including MsgHeader) to the response ring.
    // Called by service handlers (e.g. core_service) to post results.
    // seq, service, method are echoed from the request header.
    void write_response(uint16_t seq, uint8_t service, uint8_t method,
                        uint16_t status,
                        const uint8_t* payload, uint16_t payload_len);

private:
    alignas(4) uint8_t buf_[WIN_BYTES] = {};

    const SecurityPosture*    posture_          = nullptr;
    const PolicyStore*        policy_store_     = nullptr;
    const CapabilityRegistry* registry_         = nullptr;
    ProfileStore*             profile_store_    = nullptr;
    SaveStore*                save_store_       = nullptr;
    uint16_t                  active_profile_id_ = 0u;
    void                    (*reset_menu_fn_)() = nullptr;

    bool initialized_ = false;

    // Typed accessors into buf_.
    ApiWindowHeader& header()    { return *reinterpret_cast<ApiWindowHeader*>(buf_); }
    ApiRegs&         regs()      { return *reinterpret_cast<ApiRegs*>(buf_ + API_REGS_OFS); }
    RingHeader& ring_hdr(uint16_t ring_ofs) {
        return *reinterpret_cast<RingHeader*>(buf_ + ring_ofs);
    }
    uint8_t* ring_data(uint16_t ring_ofs) {
        return buf_ + ring_ofs + API_RING_HDR_SIZE;
    }

};
