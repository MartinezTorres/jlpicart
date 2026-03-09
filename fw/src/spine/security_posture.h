#pragma once
#include <cstdint>
#include "security/otp_reader.h"

// security_posture.h — OTP-derived security facts for JLPiCart.
//
// SecurityPosture is read ONCE at boot from the OtpReader and then treated
// as immutable for the lifetime of the firmware. Only this module reads OTP.
// See spec.md §10 (Security, licensing, and privacy).

struct SecurityPosture {
    // boot ROM enforces firmware signature checks against enrolled key slots.
    bool secure_boot_enabled;

    // A device-secret seed exists in OTP (for storage encryption and device ID).
    bool otp_device_secret_present;

    // Bitmask of enrolled boot-key slots (bits 0–3 map to slots 0–3).
    // spec.md §10: "boot_key_valid_mask"
    uint8_t boot_key_valid_mask;

    // SWD/debug interface is fully disabled by OTP.
    bool debug_disabled;

    // SWD/debug in secure mode is disabled (Secure world debug off).
    bool secure_debug_disabled;

    // USB BOOTSEL / picoboot boot path is disabled.
    bool usb_boot_disabled;

    // UART boot path is disabled.
    bool uart_boot_disabled;

    // Monotonic firmware rollback protection is enabled.
    bool anti_rollback_enabled;

    // Read all posture fields from the given OtpReader.
    // This is the only function that calls otp.read_bytes().
    static SecurityPosture read(const OtpReader& otp);

    // Write a human-readable summary into buf (null-terminated, max len bytes).
    void describe(char* buf, size_t len) const;
};

// Expose the hardware singleton for use in main.cc.
// Host tests use FakeOtpReader directly and never call this.
#ifndef JLPICART_HOST_TEST
const OtpReader& get_hardware_otp_reader();
#endif
