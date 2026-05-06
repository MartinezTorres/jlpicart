// test_storage.cc — Host tests for Stage 6 storage substrate.
//
// Covers:
//   FlashDevice: bounds, read/write/erase, power-loss injection.

#include "filesystem/flash_device.h"
#include "fat_test_env.h"

#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_OK(s)    CHECK((s).ok())
#define CHECK_FAIL(s)  CHECK(!(s).ok())

// ---------------------------------------------------------------------------
// flash_layout: compile-time assertions are in the header; verify key values.
// ---------------------------------------------------------------------------

static void test_flash_layout_constants()
{
    CHECK_EQ(FLASH_SIZE_BYTES, 16u * 1024u * 1024u);
    CHECK_EQ(FLASH_SECTOR_SIZE, 4096u);
    CHECK_EQ(FLASH_PAGE_SIZE,   256u);
    CHECK_EQ(FLASH_FIRMWARE_SIZE, 0x200000u);
    CHECK_EQ(FLASH_FAT_SIZE,      0xE00000u);
    CHECK(FLASH_FIRMWARE_OFS + FLASH_FIRMWARE_SIZE == FLASH_FAT_OFS);
}

// ---------------------------------------------------------------------------
// FlashDevice (host sim)
// ---------------------------------------------------------------------------

static void test_flash_device_erased_state()
{
    FlashDevice dev(1024);
    uint8_t buf[4] = {};
    CHECK_OK(dev.read(0, buf, 4));
    for (int i = 0; i < 4; ++i) CHECK_EQ(buf[i], 0xFFu);
}

static void test_flash_device_write_read_roundtrip()
{
    FlashDevice dev(1024);
    const uint8_t src[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    CHECK_OK(dev.write(16, src, sizeof(src)));
    uint8_t dst[4] = {};
    CHECK_OK(dev.read(16, dst, sizeof(dst)));
    CHECK(memcmp(src, dst, sizeof(src)) == 0);
}

static void test_flash_device_erase()
{
    FlashDevice dev(FLASH_SECTOR_SIZE * 2);
    uint8_t ones[8];
    memset(ones, 0x01, sizeof(ones));
    CHECK_OK(dev.write(0, ones, sizeof(ones)));
    CHECK_OK(dev.erase(0, 1));
    uint8_t buf[8] = {};
    CHECK_OK(dev.read(0, buf, sizeof(buf)));
    for (size_t i = 0; i < sizeof(buf); ++i) CHECK_EQ(buf[i], 0xFFu);
}

static void test_flash_device_bounds_check()
{
    FlashDevice dev(512);
    uint8_t buf[8] = {};
    CHECK_FAIL(dev.read(505, buf, 8));   // 505+8 > 512
    CHECK_FAIL(dev.write(505, buf, 8));
    CHECK_OK(dev.read(504, buf, 8));     // 504+8 == 512 — exactly at end
    CHECK_OK(dev.write(504, buf, 8));
}

static void test_flash_device_power_loss_injection()
{
    FlashDevice dev(64);
    dev.inject_power_loss_after(4);

    uint8_t data[8];
    memset(data, 0xAB, sizeof(data));
    DiagStatus s = dev.write(0, data, sizeof(data));
    CHECK_FAIL(s);

    uint8_t buf[8];
    dev.read(0, buf, sizeof(buf));
    for (int i = 0; i < 4; ++i) CHECK_EQ(buf[i], 0xABu);
    for (int i = 4; i < 8; ++i) CHECK_EQ(buf[i], 0xFFu);

    memset(data, 0xCD, sizeof(data));
    dev.write(8, data, sizeof(data));
    dev.read(8, buf, sizeof(buf));
    for (int i = 0; i < 8; ++i) CHECK_EQ(buf[i], 0xFFu);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_flash_layout_constants();
    test_flash_device_erased_state();
    test_flash_device_write_read_roundtrip();
    test_flash_device_erase();
    test_flash_device_bounds_check();
    test_flash_device_power_loss_injection();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
