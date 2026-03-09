// otp_reader_hw.cc — hardware OtpReader backend for RP2350 firmware.
// NOT compiled in host test builds (use FakeOtpReader from otp_reader.h).

#include "otp_reader.h"
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"

// HardwareOtpReader reads from the RP2350 OTP data guarded window.
// The OTP data window at OTP_DATA_GUARDED_BASE maps each 24-bit row to a
// 32-bit word (lower 24 bits = ECC-corrected data, upper 8 = ECC redundancy).
// Reads that fail ECC return 0xFFFFFFFF; guarded reads raise a bus fault.

class HardwareOtpReader : public OtpReader {
public:
    bool read_bytes(uint32_t offset, uint8_t* dst, size_t len) const override;
};

bool HardwareOtpReader::read_bytes(uint32_t offset, uint8_t* dst, size_t len) const {
    if (offset + len > OTP_BYTE_SIZE) return false;

    // OTP rows are 3 data bytes each. Read row by row.
    // byte_offset = row * 3 + byte_within_row
    const volatile uint32_t* otp_data =
        reinterpret_cast<const volatile uint32_t*>(OTP_DATA_GUARDED_BASE);

    size_t written = 0;
    while (written < len) {
        uint32_t byte_addr   = offset + written;
        uint32_t row         = byte_addr / 3;
        uint32_t byte_in_row = byte_addr % 3;

        // Read the 24-bit ECC-corrected row via the guarded data window.
        uint32_t row_val = otp_data[row] & 0x00FFFFFFu;
        uint8_t row_bytes[3] = {
            static_cast<uint8_t>(row_val & 0xFF),
            static_cast<uint8_t>((row_val >> 8) & 0xFF),
            static_cast<uint8_t>((row_val >> 16) & 0xFF),
        };

        size_t remaining_in_row = 3 - byte_in_row;
        size_t to_copy = (len - written < remaining_in_row)
                       ? (len - written)
                       : remaining_in_row;
        for (size_t i = 0; i < to_copy; i++) {
            dst[written++] = row_bytes[byte_in_row + i];
        }
    }
    return true;
}

// Global singleton — initialized once at boot by SecurityPosture::read().
static HardwareOtpReader s_hw_otp_reader;

// Factory function called by security_posture.cc
const OtpReader& get_hardware_otp_reader() {
    return s_hw_otp_reader;
}
