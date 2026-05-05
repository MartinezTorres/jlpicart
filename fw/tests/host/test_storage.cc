// test_storage.cc — Host tests for Stage 6 storage substrate.
//
// Covers:
//   FlashDevice: bounds, read/write/erase, power-loss injection.
//   KvStore: roundtrip/overwrite/delete/power-loss/reinit.

#include "filesystem/flash_device.h"
#include "filesystem/flash_device.h"
#include "store/kv_store.h"
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

// Small partition size used by KvStore tests.
static constexpr uint32_t TEST_KV_SIZE = FLASH_SECTOR_SIZE * 4;  // 16 KB

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
// KvStore (still exercised in standalone tests)
// ---------------------------------------------------------------------------

static void test_kv_init_empty()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    CHECK_OK(kv.init(dev, 0, TEST_KV_SIZE));
    CHECK(kv.initialized());
    CHECK_EQ(kv.live_count(), 0u);
}

static void test_kv_put_get_roundtrip()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    kv.init(dev, 0, TEST_KV_SIZE);

    const uint8_t val[] = { 1, 2, 3, 4, 5 };
    CHECK_OK(kv.put("mykey", val, sizeof(val)));
    CHECK(kv.contains("mykey"));
    CHECK_EQ(kv.live_count(), 1u);

    uint8_t out[16] = {};
    uint16_t out_len = 0;
    CHECK_OK(kv.get("mykey", out, &out_len, sizeof(out)));
    CHECK_EQ(out_len, sizeof(val));
    CHECK(memcmp(out, val, sizeof(val)) == 0);
}

static void test_kv_put_overwrite()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    kv.init(dev, 0, TEST_KV_SIZE);

    const uint8_t v1[] = { 0x11 };
    const uint8_t v2[] = { 0x22, 0x33 };
    kv.put("k", v1, sizeof(v1));
    kv.put("k", v2, sizeof(v2));

    CHECK(kv.contains("k"));
    CHECK_EQ(kv.live_count(), 1u);

    uint8_t out[8] = {}; uint16_t out_len = 0;
    CHECK_OK(kv.get("k", out, &out_len, sizeof(out)));
    CHECK_EQ(out_len, sizeof(v2));
    CHECK(memcmp(out, v2, sizeof(v2)) == 0);
}

static void test_kv_del()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    kv.init(dev, 0, TEST_KV_SIZE);

    const uint8_t val[] = { 42 };
    kv.put("gone", val, sizeof(val));
    CHECK(kv.contains("gone"));

    CHECK_OK(kv.del("gone"));
    CHECK(!kv.contains("gone"));
    CHECK_EQ(kv.live_count(), 0u);

    CHECK_OK(kv.del("gone"));
}

static void test_kv_missing_key_returns_error()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    kv.init(dev, 0, TEST_KV_SIZE);
    uint8_t out[8]; uint16_t len = 0;
    DiagStatus s = kv.get("nosuchkey", out, &len, sizeof(out));
    CHECK_FAIL(s);
    CHECK(s.code == DiagCode::STORAGE_NOT_FOUND);
}

static void test_kv_multiple_keys()
{
    FlashDevice dev(TEST_KV_SIZE);
    KvStore kv;
    kv.init(dev, 0, TEST_KV_SIZE);

    const uint8_t a[] = { 1 }, b[] = { 2, 3 }, c[] = { 4, 5, 6 };
    kv.put("alpha", a, sizeof(a));
    kv.put("beta",  b, sizeof(b));
    kv.put("gamma", c, sizeof(c));

    CHECK_EQ(kv.live_count(), 3u);

    uint8_t out[8]; uint16_t len = 0;
    kv.get("alpha", out, &len, sizeof(out));
    CHECK_EQ(len, 1u); CHECK_EQ(out[0], 1u);

    kv.get("beta", out, &len, sizeof(out));
    CHECK_EQ(len, 2u); CHECK_EQ(out[1], 3u);

    kv.get("gamma", out, &len, sizeof(out));
    CHECK_EQ(len, 3u); CHECK_EQ(out[2], 6u);
}

static void test_kv_survives_reinit()
{
    FlashDevice dev(TEST_KV_SIZE);

    {
        KvStore kv;
        kv.init(dev, 0, TEST_KV_SIZE);
        const uint8_t val[] = { 0xCA, 0xFE };
        kv.put("persist", val, sizeof(val));
    }

    KvStore kv2;
    CHECK_OK(kv2.init(dev, 0, TEST_KV_SIZE));
    CHECK(kv2.contains("persist"));
    uint8_t out[8]; uint16_t len = 0;
    CHECK_OK(kv2.get("persist", out, &len, sizeof(out)));
    CHECK_EQ(len, 2u);
    CHECK_EQ(out[0], 0xCAu);
    CHECK_EQ(out[1], 0xFEu);
}

static void test_kv_power_loss_old_value_survives()
{
    FlashDevice dev(TEST_KV_SIZE);

    {
        KvStore kv;
        kv.init(dev, 0, TEST_KV_SIZE);
        const uint8_t v1[] = "version1";
        CHECK_OK(kv.put("cfg", v1, sizeof(v1)));
    }

    dev.inject_power_loss_after(sizeof(KvRecordHdr));
    {
        KvStore kv;
        kv.init(dev, 0, TEST_KV_SIZE);
        const uint8_t v2[] = "version2";
        kv.put("cfg", v2, sizeof(v2));
    }

    KvStore kv3;
    CHECK_OK(kv3.init(dev, 0, TEST_KV_SIZE));
    CHECK(kv3.contains("cfg"));

    uint8_t out[16] = {}; uint16_t len = 0;
    CHECK_OK(kv3.get("cfg", out, &len, sizeof(out)));
    CHECK_EQ(len, sizeof("version1"));
    CHECK(memcmp(out, "version1", sizeof("version1")) == 0);
}

static void test_kv_tombstone_survives_reinit()
{
    FlashDevice dev(TEST_KV_SIZE);
    {
        KvStore kv;
        kv.init(dev, 0, TEST_KV_SIZE);
        const uint8_t v[] = { 1 };
        kv.put("x", v, 1);
        kv.del("x");
    }
    KvStore kv2;
    kv2.init(dev, 0, TEST_KV_SIZE);
    CHECK(!kv2.contains("x"));
    CHECK_EQ(kv2.live_count(), 0u);
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

    test_kv_init_empty();
    test_kv_put_get_roundtrip();
    test_kv_put_overwrite();
    test_kv_del();
    test_kv_missing_key_returns_error();
    test_kv_multiple_keys();
    test_kv_survives_reinit();
    test_kv_power_loss_old_value_survives();
    test_kv_tombstone_survives_reinit();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
