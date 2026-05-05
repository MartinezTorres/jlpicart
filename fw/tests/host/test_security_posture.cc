// test_security_posture.cc — unit tests for SecurityPosture::read.
#include "test_helpers.h"
#include "spine/security_posture.h"
#include "spine/otp_reader.h"
#include <cstring>

static constexpr size_t BUF_SIZE = 256;

// Helper: build a FakeOtpReader from a zero-filled buffer with one customized row.
// OTP row N starts at byte offset N*3.

static void test_all_zeros_otp() {
    uint8_t buf[BUF_SIZE] = {};
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(!p.secure_boot_enabled);
    CHECK(!p.otp_device_secret_present);
    CHECK(p.boot_key_valid_mask == 0);
    CHECK(!p.debug_disabled);
    CHECK(!p.secure_debug_disabled);
    CHECK(!p.anti_rollback_enabled);
    CHECK(!p.usb_boot_disabled);
    CHECK(!p.uart_boot_disabled);
}

static void test_secure_boot_enable_bit() {
    uint8_t buf[BUF_SIZE] = {};
    // CRIT1 row 0x040 → byte offset 0x040*3 = 192.
    // Bit 0 = SECURE_BOOT_ENABLE.
    buf[otp_offsets::CRIT1] = otp_offsets::CRIT1_SECURE_BOOT_ENABLE_BIT;
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.secure_boot_enabled);
    CHECK(!p.debug_disabled);
    CHECK(!p.secure_debug_disabled);
}

static void test_debug_disable_bits() {
    uint8_t buf[BUF_SIZE] = {};
    buf[otp_offsets::CRIT1] =
        otp_offsets::CRIT1_DEBUG_DISABLE_BIT |
        otp_offsets::CRIT1_SECURE_DEBUG_DISABLE_BIT;
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(!p.secure_boot_enabled);
    CHECK(p.debug_disabled);
    CHECK(p.secure_debug_disabled);
}

static void test_all_crit1_bits() {
    uint8_t buf[BUF_SIZE] = {};
    buf[otp_offsets::CRIT1] =
        otp_offsets::CRIT1_SECURE_BOOT_ENABLE_BIT   |
        otp_offsets::CRIT1_DEBUG_DISABLE_BIT         |
        otp_offsets::CRIT1_SECURE_DEBUG_DISABLE_BIT;
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.secure_boot_enabled);
    CHECK(p.debug_disabled);
    CHECK(p.secure_debug_disabled);
}

static void test_anti_rollback_bit() {
    uint8_t buf[BUF_SIZE] = {};
    // BOOT_FLAGS0 row 0x048 → offset 216.
    // ROLLBACK_REQUIRED = bit 11 → byte 1 bit 3 = 0x08 at buf[217].
    uint32_t off = otp_offsets::BOOT_FLAGS0;
    buf[off + 1] = 0x08;  // bit 11
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.anti_rollback_enabled);
    CHECK(!p.usb_boot_disabled);
    CHECK(!p.uart_boot_disabled);
}

static void test_boot_disable_bits() {
    uint8_t buf[BUF_SIZE] = {};
    // DISABLE_USB_PICOBOOT = bit 18, DISABLE_UART_BOOT = bit 19 → byte 2 of row.
    uint32_t off = otp_offsets::BOOT_FLAGS0;
    buf[off + 2] = 0x04 | 0x08;  // bits 18 + 19
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.usb_boot_disabled);
    CHECK(p.uart_boot_disabled);
    CHECK(!p.anti_rollback_enabled);
}

static void test_boot_key_valid_mask() {
    uint8_t buf[BUF_SIZE] = {};
    // BOOT_FLAGS1 row 0x04B → offset 225.
    // KEY_VALID_MASK = bits 0-3 of the row.
    buf[otp_offsets::BOOT_FLAGS1] = 0x05;
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.boot_key_valid_mask == 0x05);
    CHECK(p.otp_device_secret_present);  // non-zero key mask implies provisioned
}

static void test_no_key_means_no_secret() {
    uint8_t buf[BUF_SIZE] = {};
    buf[otp_offsets::BOOT_FLAGS1] = 0x00;
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    CHECK(p.boot_key_valid_mask == 0);
    CHECK(!p.otp_device_secret_present);
}

static void test_encrypted_boot_enabled_defaults_false() {
    // RP2350 has no single OTP bit for encrypted boot; field is always false
    // until Stage 6 adds partition table inspection. See security_posture.h.
    uint8_t buf[BUF_SIZE] = {};
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);
    CHECK(!p.encrypted_boot_enabled);
}

static void test_describe_does_not_overflow() {
    uint8_t buf[BUF_SIZE] = {};
    FakeOtpReader otp(buf, BUF_SIZE);
    SecurityPosture p = SecurityPosture::read(otp);

    char desc[128];
    p.describe(desc, sizeof(desc));
    CHECK(strlen(desc) > 0);
    CHECK(strlen(desc) < sizeof(desc));
}

int main() {
    test_all_zeros_otp();
    test_secure_boot_enable_bit();
    test_debug_disable_bits();
    test_all_crit1_bits();
    test_anti_rollback_bit();
    test_boot_disable_bits();
    test_boot_key_valid_mask();
    test_no_key_means_no_secret();
    test_encrypted_boot_enabled_defaults_false();
    test_describe_does_not_overflow();
    return test_summary();
}
