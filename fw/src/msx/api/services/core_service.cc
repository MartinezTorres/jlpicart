// core_service.cc — System service (0x00) implementation.
// Methods: GET_API_INFO (0x00), GET_DEVICE_ID (0x01), GET_CAPS (0x02),
//   GET_RANDOM (0x03), RESET_TO_MENU (0x04),
//   GET_SECURITY_INFO (0x05), GET_POLICY_FLAGS (0x06).

#include "msx/api/services/services.h"
#include "msx/api/api_window.h"
#include "spine/device_identity.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
// RP2350 TRNG: rosc random bit register (available before dedicated TRNG peripheral).
#  include "hardware/structs/rosc.h"
#endif

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
    info.feature_bits       = API_FEATURES_CURRENT;
    info.max_frame          = API_MAX_FRAME;
    info.posture_props      = posture_to_props(posture);
    info.boot_key_valid_mask = posture.boot_key_valid_mask;

    send_ok(win, req, reinterpret_cast<const uint8_t*>(&info), sizeof(info));
}

// ---------------------------------------------------------------------------
// 0x02 GET_CAPS
// ---------------------------------------------------------------------------
//
// Returns the allowed capability set with stable numeric IDs per
// doc/api/capabilities.md.  Capabilities with unknown names (not in the
// kCapIdMappings table) are omitted from the response rather than sent as
// ID 0x0000, to preserve forward-compatibility.
//
// Response payload: u16 count, then count × CapEntry (8 bytes each).
// cap_id:    stable numeric ID (doc/api/capabilities.md)
// cap_flags: CAP_FLAG_ACTIVATED | CAP_FLAG_HARDWARE | CAP_FLAG_PROBE_OK
// cap_param: 0 for all current capabilities

static void handle_get_caps(const MsgHeader& req, ApiWindow& win,
                             const CapabilityRegistry& registry)
{
    // Collect all allowed capability names.
    const char* names[CAPABILITY_REGISTRY_MAX];
    const size_t allowed = registry.list_allowed(names, CAPABILITY_REGISTRY_MAX);

    // Build entries, skipping any name not in the stable ID table.
    CapEntry entries[CAPABILITY_REGISTRY_MAX];
    size_t count = 0;
    for (size_t i = 0; i < allowed; ++i) {
        const uint16_t cap_id = cap_id_from_name(names[i]);
        if (cap_id == 0x0000u) continue; // unknown name — omit

        CapEntry& e = entries[count++];
        e.cap_id    = cap_id;
        e.cap_flags = 0u;
        if (registry.is_activated(names[i])) e.cap_flags |= CAP_FLAG_ACTIVATED;
        if (cap_is_hw(names[i]))             e.cap_flags |= CAP_FLAG_HARDWARE;
        e.cap_param = 0u;
    }

    const uint16_t payload_len = static_cast<uint16_t>(2u + count * sizeof(CapEntry));
    if (payload_len > API_MAX_MSG - sizeof(MsgHeader)) {
        send_error(win, req, API_E_TOO_BIG);
        return;
    }

    uint8_t payload[2u + CAPABILITY_REGISTRY_MAX * sizeof(CapEntry)];
    payload[0] = static_cast<uint8_t>(count & 0xFFu);
    payload[1] = static_cast<uint8_t>(count >> 8u);
    memcpy(payload + 2u, entries, count * sizeof(CapEntry));

    send_ok(win, req, payload, payload_len);
}

// ---------------------------------------------------------------------------
// 0x03 GET_RANDOM
// ---------------------------------------------------------------------------
//
// Request payload: u16 nbytes (number of random bytes wanted).
// Response: scratch_len = nbytes, payload_len = 0.
// The random bytes are placed in the c2h scratch buffer.
//
// On hardware: reads nbytes bytes from the RP2350 ROSC random bit register.
// In host tests: fills with a deterministic counter (0x01, 0x02, …) so tests
// can verify that scratch_len matches and bytes are non-zero.

static void handle_get_random(const MsgHeader& req,
                               const uint8_t* payload, uint16_t payload_len,
                               ApiWindow& win)
{
    if (payload_len < 2u) {
        win.write_response(req.seq, req.service, req.method,
                           API_E_BAD_REQ, nullptr, 0);
        return;
    }

    uint16_t nbytes = static_cast<uint16_t>(payload[0] | (payload[1] << 8u));

    // Cap at c2h scratch length (API_C2H_SCRATCH_LEN = 1024).
    if (nbytes > API_C2H_SCRATCH_LEN) {
        nbytes = static_cast<uint16_t>(API_C2H_SCRATCH_LEN);
    }

    // Fill the c2h scratch buffer with random bytes.
    uint8_t* scratch = win.buf() + API_C2H_SCRATCH_OFS;

#ifndef JLPICART_HOST_TEST
    for (uint16_t i = 0; i < nbytes; ++i) {
        // Each read of randombit gives one random bit; accumulate 8 bits per byte.
        uint8_t byte = 0;
        for (int b = 0; b < 8; ++b) {
            byte = static_cast<uint8_t>((byte << 1u)
                   | (rosc_hw->randombit & 1u));
        }
        scratch[i] = byte;
    }
#else
    // Host stub: deterministic counter so tests can verify non-zero output.
    for (uint16_t i = 0; i < nbytes; ++i) {
        scratch[i] = static_cast<uint8_t>((i + 1u) & 0xFFu);
    }
#endif

    // Build response header with scratch_ofs pointing to c2h scratch.
    MsgHeader resp = {};
    resp.seq         = req.seq;
    resp.service     = req.service;
    resp.method      = req.method;
    resp.status      = API_OK;
    resp.payload_len = 0;
    resp.scratch_ofs = 0u; // offset within c2h scratch (start)
    resp.scratch_len = nbytes;
    resp.reserved    = 0;

    uint8_t rsp_buf[sizeof(MsgHeader)];
    memcpy(rsp_buf, &resp, sizeof(MsgHeader));
    win.ring_push_msg(API_RSP_RING_OFS, rsp_buf, sizeof(MsgHeader));

    // Notify Z80 via cart_event doorbell.
    ApiRegs* regs = reinterpret_cast<ApiRegs*>(win.buf() + API_REGS_OFS);
    regs->cart_event = static_cast<uint8_t>(regs->cart_event + 1u);
}

// ---------------------------------------------------------------------------
// 0x04 RESET_TO_MENU
// ---------------------------------------------------------------------------

static void handle_reset_to_menu(const MsgHeader& req, ApiWindow& win,
                                  void (*reset_menu_fn)())
{
    // Respond OK first, then invoke the callback.
    win.write_response(req.seq, req.service, req.method, API_OK, nullptr, 0);
    if (reset_menu_fn) {
        reset_menu_fn();
    }
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
                         const CapabilityRegistry& registry,
                         void                    (*reset_menu_fn)(),
                         const DeviceIdentity*     device_identity)
{
    switch (req.method) {
        case SYS_GET_API_INFO:
            (void)payload;
            (void)payload_len;
            handle_get_api_info(req, win, posture);
            break;

        case SYS_GET_DEVICE_ID:
        {
            if (!device_identity || !device_identity->initialized()) {
                (void)payload;
                (void)payload_len;
                send_error(win, req, API_E_UNSUPPORTED);
                break;
            }
            // Parse scope byte.  Absent → scope 0.
            uint8_t scope = 0u;
            if (payload_len >= 1u) scope = payload[0];

            // scope=0 (stable device ID): requires explicit policy permission.
            // Without the flag the global device ID is not exposed, preventing
            // game titles from correlating the same device across collections.
            if (scope == 0u &&
                !(policy_store.info().flags & POLICY_EXPOSE_STABLE_DEVICE_ID)) {
                send_error(win, req, API_E_POLICY);
                break;
            }

            uint8_t pub[DIK_PUB_KEY_LEN];
            device_identity->public_key(pub);

            uint8_t id_16[16];
            device_identity_scoped_id(pub, scope, id_16);

            send_ok(win, req, id_16, 16u);
            break;
        }

        case SYS_GET_CAPS:
            (void)payload;
            (void)payload_len;
            handle_get_caps(req, win, registry);
            break;

        case SYS_GET_RANDOM:
            handle_get_random(req, payload, payload_len, win);
            break;

        case SYS_RESET_TO_MENU:
            (void)payload;
            (void)payload_len;
            handle_reset_to_menu(req, win, reset_menu_fn);
            break;

        case SYS_GET_SECURITY_INFO:
            (void)payload;
            (void)payload_len;
            handle_get_security_info(req, win, posture);
            break;

        case SYS_GET_POLICY_FLAGS:
            (void)payload;
            (void)payload_len;
            handle_get_policy_flags(req, win, policy_store);
            break;

        default:
            (void)payload;
            (void)payload_len;
            send_error(win, req, API_E_UNSUPPORTED);
            break;
    }
}
