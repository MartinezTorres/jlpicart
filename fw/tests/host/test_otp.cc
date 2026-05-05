// test_otp.cc — unit tests for FakeOtpReader and OtpReader helpers.
#include "test_helpers.h"
#include "spine/otp_reader.h"
#include <cstring>

// Minimal OTP buffer for testing (covers the farthest offset we read).
// BOOT_FLAGS1 is at 0x04B * 3 = 225, so 228 bytes covers all fields.
static constexpr size_t BUF_SIZE = 256;

static void test_fake_otp_read_bytes_basic() {
    uint8_t buf[BUF_SIZE] = {};
    buf[0] = 0xAB;
    buf[1] = 0xCD;
    buf[2] = 0xEF;
    FakeOtpReader otp(buf, BUF_SIZE);

    uint8_t out[3] = {};
    CHECK(otp.read_bytes(0, out, 3));
    CHECK(out[0] == 0xAB);
    CHECK(out[1] == 0xCD);
    CHECK(out[2] == 0xEF);
}

static void test_fake_otp_read_bytes_offset() {
    uint8_t buf[BUF_SIZE] = {};
    buf[10] = 0x42;
    FakeOtpReader otp(buf, BUF_SIZE);

    uint8_t out = 0;
    CHECK(otp.read_bytes(10, &out, 1));
    CHECK(out == 0x42);
}

static void test_fake_otp_read_bytes_out_of_bounds() {
    uint8_t buf[BUF_SIZE] = {};
    FakeOtpReader otp(buf, BUF_SIZE);

    uint8_t out[4] = {};
    // Exact end of buffer — should succeed.
    CHECK(otp.read_bytes(BUF_SIZE - 1, out, 1));
    // One byte past end — should fail.
    CHECK(!otp.read_bytes(BUF_SIZE, out, 1));
    // Overflow: offset + len wraps around — should fail.
    CHECK(!otp.read_bytes(BUF_SIZE - 1, out, 2));
}

static void test_fake_otp_read_u8() {
    uint8_t buf[BUF_SIZE] = {};
    buf[5] = 0x77;
    FakeOtpReader otp(buf, BUF_SIZE);
    CHECK(otp.read_u8(5) == 0x77);
    CHECK(otp.read_u8(0) == 0x00);
}

static void test_fake_otp_read_u24_little_endian() {
    uint8_t buf[BUF_SIZE] = {};
    // Row 0: bytes at offset 0,1,2 = 0x11,0x22,0x33
    buf[0] = 0x11;
    buf[1] = 0x22;
    buf[2] = 0x33;
    FakeOtpReader otp(buf, BUF_SIZE);

    uint32_t val = otp.read_u24(0);
    // read_u24 returns buf[0] | (buf[1] << 8) | (buf[2] << 16)
    CHECK(val == (0x11u | (0x22u << 8) | (0x33u << 16)));
}

static void test_fake_otp_read_u24_zero() {
    uint8_t buf[BUF_SIZE] = {};
    FakeOtpReader otp(buf, BUF_SIZE);
    CHECK(otp.read_u24(0) == 0);
    CHECK(otp.read_u24(otp_offsets::CRIT1) == 0);
    CHECK(otp.read_u24(otp_offsets::BOOT_FLAGS0) == 0);
    CHECK(otp.read_u24(otp_offsets::BOOT_FLAGS1) == 0);
}

static void test_otp_byte_size_constant() {
    // 4096 rows × 3 bytes = 12288
    CHECK(OtpReader::OTP_BYTE_SIZE == 4096u * 3u);
}

static void test_otp_offsets_values() {
    CHECK(otp_offsets::CRIT1       == 0x040u * 3u);
    CHECK(otp_offsets::BOOT_FLAGS0 == 0x048u * 3u);
    CHECK(otp_offsets::BOOT_FLAGS1 == 0x04Bu * 3u);
}

int main() {
    test_fake_otp_read_bytes_basic();
    test_fake_otp_read_bytes_offset();
    test_fake_otp_read_bytes_out_of_bounds();
    test_fake_otp_read_u8();
    test_fake_otp_read_u24_little_endian();
    test_fake_otp_read_u24_zero();
    test_otp_byte_size_constant();
    test_otp_offsets_values();
    return test_summary();
}
