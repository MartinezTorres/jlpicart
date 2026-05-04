#include "security_posture.h"
#include "security/otp_reader.h"
#include <cstdio>

SecurityPosture SecurityPosture::read(const OtpReader& otp) {
    SecurityPosture p = {};

    // --- CRIT1 row (0x040) — secure boot + debug disable ---
    uint32_t crit1 = otp.read_u24(otp_offsets::CRIT1);
    p.secure_boot_enabled   = (crit1 & otp_offsets::CRIT1_SECURE_BOOT_ENABLE_BIT)   != 0;
    p.secure_debug_disabled = (crit1 & otp_offsets::CRIT1_SECURE_DEBUG_DISABLE_BIT) != 0;
    p.debug_disabled        = (crit1 & otp_offsets::CRIT1_DEBUG_DISABLE_BIT)        != 0;

    // --- BOOT_FLAGS0 row (0x048) — rollback + boot path disables ---
    uint32_t bf0 = otp.read_u24(otp_offsets::BOOT_FLAGS0);
    p.anti_rollback_enabled = (bf0 & otp_offsets::BOOT_FLAGS0_ROLLBACK_REQUIRED)    != 0;
    p.uart_boot_disabled    = (bf0 & otp_offsets::BOOT_FLAGS0_DISABLE_UART_BOOT)    != 0;
    p.usb_boot_disabled     = (bf0 & otp_offsets::BOOT_FLAGS0_DISABLE_USB_PICOBOOT) != 0;

    // --- BOOT_FLAGS1 row (0x04B) — key valid mask ---
    uint32_t bf1 = otp.read_u24(otp_offsets::BOOT_FLAGS1);
    p.boot_key_valid_mask = static_cast<uint8_t>(bf1 & otp_offsets::BOOT_FLAGS1_KEY_VALID_MASK);

    // The RP2350 does not expose encrypted-boot state as a single OTP bit.
    // Requires partition table imagedef inspection; deferred until storage is ready.
    p.encrypted_boot_enabled = false;

    // Proxy: any enrolled key slot implies a provisioned device secret.
    // Full determination requires reading secret OTP pages via TrustZone Secure world.
    p.otp_device_secret_present = (p.boot_key_valid_mask != 0);

    return p;
}

void SecurityPosture::describe(char* buf, size_t len) const {
    snprintf(buf, len,
        "secure_boot=%d otp_secret=%d keys=0x%02x debug_off=%d "
        "anti_rollback=%d usb_boot_off=%d uart_boot_off=%d enc_boot=%d",
        secure_boot_enabled,
        otp_device_secret_present,
        boot_key_valid_mask,
        debug_disabled,
        anti_rollback_enabled,
        usb_boot_disabled,
        uart_boot_disabled,
        encrypted_boot_enabled);
}
