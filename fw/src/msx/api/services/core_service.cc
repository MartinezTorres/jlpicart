// core_service.cc — System service (0x00) implementation.
//
// Spec reference: spec.md §5.1 "System service (0x00)".
// Methods implemented in this stage: GET_API_INFO (0x00), GET_CAPS (0x02),
// GET_SECURITY_INFO (0x05), GET_POLICY_FLAGS (0x06).
// Stubbed with E_UNSUPPORTED: GET_DEVICE_ID (0x01), GET_RANDOM (0x03),
//   RESET_TO_MENU (0x04) — full implementations in later stages.

#include "msx/api/services/core_service.h"
#include "msx/api/api_window.h"
#include <cstring>

// ---------------------------------------------------------------------------
// posture_to_props
// ---------------------------------------------------------------------------

uint32_t posture_to_props(const SecurityPosture& posture)
{
    uint32_t props = 0;
    if (posture.secure_boot_enabled)   props |= API_POSTURE_SECURE_BOOT_ENABLED;
    if (posture.otp_device_secret_present) props |= API_POSTURE_OTP_SECRET_PRESENT;
    if (posture.debug_disabled)        props |= API_POSTURE_DEBUG_DISABLED;
    if (posture.usb_boot_disabled)     props |= API_POSTURE_USB_BOOT_DISABLED;
    if (posture.uart_boot_disabled)    props |= API_POSTURE_UART_BOOT_DISABLED;
    if (posture.anti_rollback_enabled) props |= API_POSTURE_ANTI_ROLLBACK_ENABLED;
    if (posture.encrypted_boot_enabled) props |= API_POSTURE_ENCRYPTED_BOOT_ENABLED;
    return props;
}

// ---------------------------------------------------------------------------
// Handler helpers (write response via ApiWindow)
// ---------------------------------------------------------------------------

static void send_ok(ApiWindow& win, const MsgHeader& req,
                    const uint8_t* payload, uint16_t payload_len)
{
    win.write_response(req.seq, req.service, req.method, API_OK, payload, payload_len);
}

static void send_error(ApiWindow& win, const MsgHeader& req, uint16_t status)
{
    win.write_response(req.seq, req.service, req.method, status, nullptr, 0);
}

// ---------------------------------------------------------------------------
// 0x00 GET_API_INFO
// ---------------------------------------------------------------------------

static void handle_get_api_info(const MsgHeader& req, ApiWindow& win,
                                const SecurityPosture& posture)
{
    ApiInfo info = {};
    info.api_major          = API_MAJOR;
    info.api_minor          = API_MINOR;
    info.layout_ver         = API_LAYOUT_VER;
    info.flags              = API_HDR_FLAGS_V1;
    info.feature_bits       = API_FEATURES_STAGE4;
    info.max_frame          = API_MAX_FRAME;
    info.posture_props      = posture_to_props(posture);
    info.boot_key_valid_mask = posture.boot_key_valid_mask;

    send_ok(win, req, reinterpret_cast<const uint8_t*>(&info), sizeof(info));
}

// ---------------------------------------------------------------------------
// 0x02 GET_CAPS
// ---------------------------------------------------------------------------
//
// Returns allowed capabilities from the registry.
// Response payload: u16 count, then count × CapEntry (8 bytes each).
// cap_id is a placeholder index (0..N-1) until stable numeric IDs are defined.
// cap_flags = 0, cap_param = 0 in Stage 4.

static void handle_get_caps(const MsgHeader& req, ApiWindow& win,
                             const CapabilityRegistry& registry)
{
    const size_t count = registry.allowed_count();

    // Build payload: u16 count + count × CapEntry.
    const uint16_t payload_len = static_cast<uint16_t>(2u + count * sizeof(CapEntry));
    if (payload_len > API_MAX_MSG - sizeof(MsgHeader)) {
        send_error(win, req, API_E_TOO_BIG);
        return;
    }

    uint8_t payload[2 + CAPABILITY_REGISTRY_MAX * sizeof(CapEntry)];
    payload[0] = static_cast<uint8_t>(count & 0xFFu);
    payload[1] = static_cast<uint8_t>(count >> 8u);

    for (size_t i = 0; i < count; i++) {
        CapEntry entry = {};
        entry.cap_id    = static_cast<uint16_t>(i); // placeholder index (IDs not yet finalized)
        entry.cap_flags = 0;
        entry.cap_param = 0;
        memcpy(payload + 2 + i * sizeof(CapEntry), &entry, sizeof(CapEntry));
    }

    send_ok(win, req, payload, payload_len);
}

// ---------------------------------------------------------------------------
// 0x05 GET_SECURITY_INFO
// ---------------------------------------------------------------------------

static void handle_get_security_info(const MsgHeader& req, ApiWindow& win,
                                     const SecurityPosture& posture)
{
    SecurityInfoResp resp = {};
    resp.posture_props       = posture_to_props(posture);
    resp.boot_key_valid_mask = posture.boot_key_valid_mask;

    send_ok(win, req, reinterpret_cast<const uint8_t*>(&resp), sizeof(resp));
}

// ---------------------------------------------------------------------------
// 0x06 GET_POLICY_FLAGS
// ---------------------------------------------------------------------------

static void handle_get_policy_flags(const MsgHeader& req, ApiWindow& win,
                                    const PolicyStore& policy_store)
{
    PolicyFlagsResp resp = {};
    const PolicyInfo& pi = policy_store.info();

    // policy_flags: the lower 32 bits of PolicyFlags (all v1 bits fit in 32 bits)
    resp.policy_flags = static_cast<uint32_t>(pi.flags);

    // policy_gen: monotonic generation counter.
    // Set to 1 if policy loaded successfully, 0 otherwise (no KV store yet).
    resp.policy_gen = policy_store.loaded_ok() ? 1u : 0u;

    // policy_hash16: first 16 bytes of SHA-256(canonical policy bytes).
    memcpy(resp.policy_hash16, pi.digest16, 16);

    send_ok(win, req, reinterpret_cast<const uint8_t*>(&resp), sizeof(resp));
}

// ---------------------------------------------------------------------------
// core_service_handle — top-level dispatcher
// ---------------------------------------------------------------------------

void core_service_handle(const MsgHeader&          req,
                         const uint8_t*            payload,
                         uint16_t                  payload_len,
                         ApiWindow&                win,
                         const SecurityPosture&    posture,
                         const PolicyStore&        policy_store,
                         const CapabilityRegistry& registry)
{
    // Suppress unused-parameter warning for methods not yet reading payload.
    (void)payload;
    (void)payload_len;

    switch (req.method) {
        case SYS_GET_API_INFO:
            handle_get_api_info(req, win, posture);
            break;

        case SYS_GET_DEVICE_ID:
            // Stage 6: requires OTP device secret for stable IDs.
            send_error(win, req, API_E_UNSUPPORTED);
            break;

        case SYS_GET_CAPS:
            handle_get_caps(req, win, registry);
            break;

        case SYS_GET_RANDOM:
            // Stage 5: requires TRNG access.
            send_error(win, req, API_E_UNSUPPORTED);
            break;

        case SYS_RESET_TO_MENU:
            // Stage 5: requires Menu ABI.
            send_error(win, req, API_E_UNSUPPORTED);
            break;

        case SYS_GET_SECURITY_INFO:
            handle_get_security_info(req, win, posture);
            break;

        case SYS_GET_POLICY_FLAGS:
            handle_get_policy_flags(req, win, policy_store);
            break;

        default:
            send_error(win, req, API_E_UNSUPPORTED);
            break;
    }
}
