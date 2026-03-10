#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// otp_reader.h — platform-abstracted RP2350 OTP access.
//
// OTP rows on the RP2350 are 24-bit ECC-protected words. Each row is
// addressed by a row number (0x000 – 0xFFF). The SDK exposes rows as
// 32-bit values (upper byte = ECC redundancy, lower 24 bits = data).
//
// Only SecurityPosture may use OtpReader — per spec.md §10 and the
// agent rules in bootstrapping.md (Appendix).
//
// Two backends exist:
//   - HardwareOtpReader (fw/src/security/otp_reader_hw.cc) — firmware only
//   - FakeOtpReader     (defined below in this header)      — host tests only

class OtpReader {
public:
    virtual ~OtpReader() = default;

    // Read `len` bytes starting at byte offset `offset` within OTP data space.
    // Returns true on success; false if offset+len exceeds OTP bounds or HW error.
    virtual bool read_bytes(uint32_t offset, uint8_t* dst, size_t len) const = 0;

    // Convenience helpers built on read_bytes.
    uint8_t  read_u8 (uint32_t byte_offset) const;
    uint32_t read_u24(uint32_t byte_offset) const;  // 3-byte little-endian OTP row

    // RP2350 OTP is 4096 rows × 3 bytes = 12288 bytes of data.
    static constexpr uint32_t OTP_BYTE_SIZE = 4096u * 3u;
};

// ---------------------------------------------------------------------------
// OTP byte offsets for fields used by SecurityPosture.
// Row N starts at byte offset N*3 (each row is 3 data bytes).
// References: RP2350 Datasheet §8.9 and spec.md §10.
// ---------------------------------------------------------------------------
namespace otp_offsets {
    // CRIT1 row 0x040 — secure boot enable, debug disable
    // spec.md §10: "secure_boot_enabled", "debug_disabled"
    static constexpr uint32_t CRIT1            = 0x040u * 3u;
    static constexpr uint8_t  CRIT1_SECURE_BOOT_ENABLE_BIT   = (1u << 0);
    static constexpr uint8_t  CRIT1_SECURE_DEBUG_DISABLE_BIT = (1u << 1);
    static constexpr uint8_t  CRIT1_DEBUG_DISABLE_BIT        = (1u << 2);

    // BOOT_FLAGS0 row 0x048 — rollback required, UART/USB boot disable
    // spec.md §10: "uart_boot_disabled", "usb_boot_disabled", "anti_rollback_enabled"
    static constexpr uint32_t BOOT_FLAGS0                    = 0x048u * 3u;
    static constexpr uint32_t BOOT_FLAGS0_ROLLBACK_REQUIRED  = (1u << 11);
    static constexpr uint32_t BOOT_FLAGS0_DISABLE_UART_BOOT  = (1u << 19);
    static constexpr uint32_t BOOT_FLAGS0_DISABLE_USB_PICOBOOT = (1u << 18);

    // BOOT_FLAGS1 row 0x04B — key_valid mask (bits 0-3)
    // spec.md §10: "boot_key_valid_mask"
    static constexpr uint32_t BOOT_FLAGS1                    = 0x04bu * 3u;
    static constexpr uint8_t  BOOT_FLAGS1_KEY_VALID_MASK     = 0x0Fu;

    // CRIT0 row 0x038 (not directly used by SecurityPosture, reserved)
    static constexpr uint32_t CRIT0 = 0x038u * 3u;
}

// ---------------------------------------------------------------------------
// FakeOtpReader — for host unit tests.
// Constructed with a byte buffer representing OTP data space.
// ---------------------------------------------------------------------------
class FakeOtpReader : public OtpReader {
public:
    FakeOtpReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}

    bool read_bytes(uint32_t offset, uint8_t* dst, size_t len) const override {
        if (offset + len > len_) return false;
        memcpy(dst, buf_ + offset, len);
        return true;
    }

private:
    const uint8_t* buf_;
    size_t         len_;
};

// Inline helpers.
// read_bytes() failures return 0 — callers in SecurityPosture treat 0 bits
// as "feature not set", which is the safe/dev (non-enforced) behaviour.
inline uint8_t OtpReader::read_u8(uint32_t byte_offset) const {
    uint8_t v = 0;
    (void)read_bytes(byte_offset, &v, 1);
    return v;
}

inline uint32_t OtpReader::read_u24(uint32_t byte_offset) const {
    uint8_t buf[3] = {};
    (void)read_bytes(byte_offset, buf, 3);
    return static_cast<uint32_t>(buf[0])
         | (static_cast<uint32_t>(buf[1]) << 8)
         | (static_cast<uint32_t>(buf[2]) << 16);
}
