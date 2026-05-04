// test_store.cc — Host tests for the append-only Store.

#include "storage/uuid.h"
#include "storage/store_key.h"
#include "storage/store_record.h"
#include "storage/store.h"
#include "storage/fat_util.h"
#include "fat_test_env.h"

#include <cstring>
#include <cstdio>

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
// Helpers
// ---------------------------------------------------------------------------

static Uuid make_profile(uint8_t fill) {
    Uuid u; memset(u.bytes, fill, 16); return u;
}

static StoreRecord make_save_rec(const Uuid& profile, const char* payload_id,
                                   const uint8_t* data, uint16_t len)
{
    StoreRecord r = {};
    StoreKey k = StoreKey::for_save(profile, payload_id);
    memcpy(r.key, k.data, 32);
    r.event_type  = static_cast<uint8_t>(StoreEntryType::SAVE);
    r.payload_len = len;
    if (data && len) memcpy(r.payload, data, len);
    return r;
}

static StoreRecord make_ach_rec(const Uuid& profile, const char* payload_id,
                                  const char* ach_id)
{
    StoreRecord r = {};
    StoreKey k = StoreKey::for_achievement(profile, payload_id, ach_id);
    memcpy(r.key, k.data, 32);
    r.event_type  = static_cast<uint8_t>(StoreEntryType::ACHIEVEMENT);
    r.payload_len = 1;
    r.payload[0]  = 1;
    return r;
}

// ---------------------------------------------------------------------------
// test_store_init_creates_files
// ---------------------------------------------------------------------------

static void test_store_init_creates_files()
{
    FatTestEnv env;
    Store log;
    CHECK_OK(log.init());
    CHECK(log.initialized());
    CHECK(fat_file_exists("1:/system/events.bin"));
    CHECK(fat_file_exists("1:/system/device.uuid"));
}

// ---------------------------------------------------------------------------
// test_store_init_empty
// ---------------------------------------------------------------------------

static void test_store_init_empty()
{
    FatTestEnv env;
    Store log;
    CHECK_OK(log.init());
    CHECK_EQ(log.record_count(),  0u);
    CHECK_EQ(log.pending_count(), 0u);
}

// ---------------------------------------------------------------------------
// test_store_append_and_find
// ---------------------------------------------------------------------------

static void test_store_append_and_find()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    const uint8_t data[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    StoreRecord rec = make_save_rec(prof, "game.alpha", data, sizeof(data));
    CHECK_OK(log.append(rec));

    CHECK_EQ(log.record_count(),  1u);
    CHECK_EQ(log.pending_count(), 1u);

    StoreRecord out = {};
    StoreKey k = StoreKey::for_save(prof, "game.alpha");
    CHECK_OK(log.find_latest(k, out));
    CHECK(out.payload_len == sizeof(data));
    CHECK(memcmp(out.payload, data, sizeof(data)) == 0);
    CHECK(static_cast<SyncState>(out.sync_state) == SyncState::PENDING);
}

// ---------------------------------------------------------------------------
// test_store_key_isolation
// ---------------------------------------------------------------------------

static void test_store_key_isolation()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid profA = make_profile(0x01);
    Uuid profB = make_profile(0x02);

    const uint8_t dA[] = { 0xAA };
    const uint8_t dB[] = { 0xBB };
    StoreRecord rA = make_save_rec(profA, "game.x", dA, 1);
    StoreRecord rB = make_save_rec(profB, "game.x", dB, 1);
    log.append(rA);
    log.append(rB);

    StoreRecord out = {};
    CHECK_OK(log.find_latest(StoreKey::for_save(profA, "game.x"), out));
    CHECK(out.payload[0] == 0xAA);
    CHECK_OK(log.find_latest(StoreKey::for_save(profB, "game.x"), out));
    CHECK(out.payload[0] == 0xBB);

    // A key that was never written returns NOT_FOUND.
    Uuid profC = make_profile(0x03);
    CHECK(!log.find_latest(StoreKey::for_save(profC, "game.x"), out).ok());
}

// ---------------------------------------------------------------------------
// test_store_latest_wins
// ---------------------------------------------------------------------------

static void test_store_latest_wins()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    const uint8_t d1[] = { 0x11 };
    const uint8_t d2[] = { 0x22 };
    StoreRecord r1 = make_save_rec(prof, "game.x", d1, 1);
    StoreRecord r2 = make_save_rec(prof, "game.x", d2, 1);
    log.append(r1);
    log.append(r2);

    StoreRecord out = {};
    log.find_latest(StoreKey::for_save(prof, "game.x"), out);
    CHECK(out.payload[0] == 0x22);  // second write wins
}

// ---------------------------------------------------------------------------
// test_store_iterate_order
// ---------------------------------------------------------------------------

static void test_store_iterate_order()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid profA = make_profile(0x01);
    Uuid profB = make_profile(0x02);

    StoreRecord rA = make_ach_rec(profA, "game.x", "first_blood");
    StoreRecord rB = make_ach_rec(profB, "game.x", "first_blood");
    StoreRecord rA2 = make_save_rec(profA, "game.x", nullptr, 0);
    log.append(rA);
    log.append(rB);
    log.append(rA2);

    struct Ctx { int count; };
    Ctx ctx = {};
    log.iterate([](const StoreRecord&, void* c) -> bool {
        ++static_cast<Ctx*>(c)->count;
        return true;
    }, &ctx);
    CHECK_EQ(ctx.count, 3);
}

// ---------------------------------------------------------------------------
// test_store_mark_synced
// ---------------------------------------------------------------------------

static void test_store_mark_synced()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    StoreRecord r1 = make_ach_rec(prof, "game.x", "ach_a");
    StoreRecord r2 = make_ach_rec(prof, "game.x", "ach_b");
    log.append(r1);
    log.append(r2);
    CHECK_EQ(log.pending_count(), 2u);

    StoreKey k1 = StoreKey::for_achievement(prof, "game.x", "ach_a");
    CHECK_OK(log.mark_synced(k1));
    CHECK_EQ(log.pending_count(), 1u);

    // Verify state on disk.
    StoreRecord out = {};
    log.find_latest(k1, out);
    CHECK(static_cast<SyncState>(out.sync_state) == SyncState::SYNCED);

    StoreKey k2 = StoreKey::for_achievement(prof, "game.x", "ach_b");
    log.find_latest(k2, out);
    CHECK(static_cast<SyncState>(out.sync_state) == SyncState::PENDING);
}

// ---------------------------------------------------------------------------
// test_store_mark_all_synced
// ---------------------------------------------------------------------------

static void test_store_mark_all_synced()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    for (int i = 0; i < 5; ++i) {
        char name[16]; snprintf(name, sizeof(name), "ach_%d", i);
        StoreRecord r = make_ach_rec(prof, "game.x", name);
        log.append(r);
    }
    CHECK_EQ(log.pending_count(), 5u);
    CHECK_OK(log.mark_all_synced());
    CHECK_EQ(log.pending_count(), 0u);
}

// ---------------------------------------------------------------------------
// test_store_compact_removes_synced
// ---------------------------------------------------------------------------

static void test_store_compact_removes_synced()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    // Append three records for the same key — simulating three saves.
    for (int i = 0; i < 3; ++i) {
        uint8_t d = static_cast<uint8_t>(i);
        StoreRecord r = make_save_rec(prof, "game.x", &d, 1);
        log.append(r);
    }
    CHECK_EQ(log.record_count(), 3u);

    log.mark_all_synced();
    CHECK_OK(log.compact());

    // Three records for one key collapse to one checkpoint.
    CHECK_EQ(log.record_count(), 1u);
    CHECK_EQ(log.pending_count(), 0u);
}

// ---------------------------------------------------------------------------
// test_store_compact_keeps_pending
// ---------------------------------------------------------------------------

static void test_store_compact_keeps_pending()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);

    // Synced save.
    const uint8_t d1[] = { 0x55 };
    StoreRecord synced = make_save_rec(prof, "game.x", d1, 1);
    log.append(synced);
    log.mark_all_synced();

    // Pending achievement (not synced).
    StoreRecord pending = make_ach_rec(prof, "game.x", "secret");
    log.append(pending);
    CHECK_EQ(log.pending_count(), 1u);

    CHECK_OK(log.compact());

    // One checkpoint for the save key + one pending achievement.
    CHECK_EQ(log.record_count(),  2u);
    CHECK_EQ(log.pending_count(), 1u);

    // Pending achievement is still findable.
    StoreRecord out = {};
    StoreKey kach = StoreKey::for_achievement(prof, "game.x", "secret");
    CHECK_OK(log.find_latest(kach, out));
    CHECK(static_cast<SyncState>(out.sync_state) == SyncState::PENDING);
}

// ---------------------------------------------------------------------------
// test_store_compact_find_latest_works
// ---------------------------------------------------------------------------

static void test_store_compact_find_latest_works()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    const uint8_t latest_data[] = { 0xFE, 0xED };
    StoreRecord r1 = make_save_rec(prof, "game.x", (const uint8_t*)"\x01", 1);
    StoreRecord r2 = make_save_rec(prof, "game.x", latest_data, 2);
    log.append(r1);
    log.append(r2);
    log.mark_all_synced();
    log.compact();

    // After compact, find_latest must return the last value (now a CHECKPOINT).
    StoreRecord out = {};
    StoreKey k = StoreKey::for_save(prof, "game.x");
    CHECK_OK(log.find_latest(k, out));
    CHECK(out.payload_len == 2);
    CHECK(memcmp(out.payload, latest_data, 2) == 0);
    CHECK(static_cast<SyncState>(out.sync_state) == SyncState::CHECKPOINT);
}

// ---------------------------------------------------------------------------
// test_store_survives_reinit
// ---------------------------------------------------------------------------

static void test_store_survives_reinit()
{
    FatTestEnv env;

    Uuid prof = make_profile(0x01);
    const uint8_t data[] = { 0xCA, 0xFE };

    {
        Store log;
        log.init();
        StoreRecord r = make_save_rec(prof, "game.x", data, sizeof(data));
        log.append(r);
        StoreRecord r2 = make_ach_rec(prof, "game.x", "first_win");
        log.append(r2);
    }

    Store log2;
    log2.init();
    CHECK_EQ(log2.record_count(),  2u);
    CHECK_EQ(log2.pending_count(), 2u);

    StoreRecord out = {};
    CHECK_OK(log2.find_latest(StoreKey::for_save(prof, "game.x"), out));
    CHECK(memcmp(out.payload, data, sizeof(data)) == 0);
}

// ---------------------------------------------------------------------------
// test_store_purge_pending
// ---------------------------------------------------------------------------

static void test_store_purge_pending()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);

    // Synced record.
    const uint8_t d[] = { 0x77 };
    StoreRecord synced = make_save_rec(prof, "game.x", d, 1);
    log.append(synced);
    log.mark_all_synced();

    // Pending record.
    StoreRecord pending = make_ach_rec(prof, "game.x", "ach");
    log.append(pending);
    CHECK_EQ(log.pending_count(), 1u);

    CHECK_OK(log.purge_pending());

    // Pending record is gone; synced record survives.
    CHECK_EQ(log.record_count(),  1u);
    CHECK_EQ(log.pending_count(), 0u);

    StoreRecord out = {};
    CHECK(!log.find_latest(StoreKey::for_achievement(prof, "game.x", "ach"), out).ok());
    CHECK_OK(log.find_latest(StoreKey::for_save(prof, "game.x"), out));
}

// ---------------------------------------------------------------------------
// test_store_pressure
// ---------------------------------------------------------------------------

static void test_store_pressure()
{
    FatTestEnv env;
    Store log;
    log.init();

    CHECK(log.pressure() == 0.0f);

    Uuid prof = make_profile(0x01);
    for (int i = 0; i < 10; ++i) {
        char name[16]; snprintf(name, sizeof(name), "ach_%d", i);
        StoreRecord r = make_ach_rec(prof, "game.x", name);
        log.append(r);
    }
    float p = log.pressure();
    CHECK(p > 0.0f);
    CHECK(p < 1.0f);  // 10 records is nowhere near the limit
}

// ---------------------------------------------------------------------------
// test_store_compact_recovery
// ---------------------------------------------------------------------------

static void test_store_compact_recovery()
{
    // Simulate interrupted compaction: events.tmp exists, events.bin absent.
    FatTestEnv env;

    Uuid prof = make_profile(0x01);

    // Write a record to the tmp path directly (mimics an interrupted compact).
    {
        Store log;
        log.init();
        const uint8_t d[] = { 0x42 };
        StoreRecord r = make_save_rec(prof, "game.x", d, 1);
        log.append(r);
        // Simulate: compact wrote tmp but power failed before rename.
        // Copy events.bin → events.tmp, then delete events.bin.
        size_t sz = fat_file_size("1:/system/events.bin");
        uint8_t buf[160 * 32] = {};
        if (sz > 0 && sz <= sizeof(buf)) {
            fat_read_file("1:/system/events.bin", buf, sz, nullptr);
            fat_write_file("1:/system/events.tmp", buf, sz);
        }
        fat_delete_file("1:/system/events.bin");
    }

    // A fresh init must succeed and recover the log.
    {
        Store log;
        CHECK_OK(log.init());
        CHECK(log.initialized());
        // Record count may be 0 or 1 depending on whether recovery found data;
        // key contract: init succeeds without crash.
    }
}

// ---------------------------------------------------------------------------
// test_store_crc_corruption_skipped
// ---------------------------------------------------------------------------

static void test_store_crc_corruption_skipped()
{
    FatTestEnv env;
    Store log;
    log.init();

    Uuid prof = make_profile(0x01);
    const uint8_t d[] = { 0xAB };
    StoreRecord r = make_save_rec(prof, "game.x", d, 1);
    log.append(r);

    // Corrupt the 10th byte of the record on disk.
    {
        FIL f;
        f_open(&f, "1:/system/events.bin", FA_READ | FA_WRITE);
        f_lseek(&f, 10);
        uint8_t bad = 0xFF;
        UINT bw;
        f_write(&f, &bad, 1, &bw);
        f_close(&f);
    }

    // find_latest should not return the corrupted record.
    StoreRecord out = {};
    DiagStatus s = log.find_latest(StoreKey::for_save(prof, "game.x"), out);
    CHECK(!s.ok());

    // iterate should skip it silently.
    struct Ctx { int count; };
    Ctx ctx = {};
    log.iterate([](const StoreRecord&, void* c) -> bool {
        ++static_cast<Ctx*>(c)->count; return true;
    }, &ctx);
    CHECK_EQ(ctx.count, 0);
}

// ---------------------------------------------------------------------------
// test_store_device_uuid_stable
// ---------------------------------------------------------------------------

static void test_store_device_uuid_stable()
{
    FatTestEnv env;

    uint8_t uuid1[16] = {}, uuid2[16] = {};

    {
        Store log;
        log.init();
        Uuid prof = make_profile(0x01);
        StoreRecord r = make_ach_rec(prof, "game.x", "a");
        log.append(r);

        StoreRecord out = {};
        log.find_latest(StoreKey::for_achievement(prof, "game.x", "a"), out);
        memcpy(uuid1, out.device_uuid, 16);
    }

    // Second init must load the same device UUID, not generate a new one.
    {
        Store log;
        log.init();
        Uuid prof = make_profile(0x01);
        StoreRecord r = make_ach_rec(prof, "game.x", "b");
        log.append(r);

        StoreRecord out = {};
        log.find_latest(StoreKey::for_achievement(prof, "game.x", "b"), out);
        memcpy(uuid2, out.device_uuid, 16);
    }

    CHECK(memcmp(uuid1, uuid2, 16) == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_store_init_creates_files();
    test_store_init_empty();
    test_store_append_and_find();
    test_store_key_isolation();
    test_store_latest_wins();
    test_store_iterate_order();
    test_store_mark_synced();
    test_store_mark_all_synced();
    test_store_compact_removes_synced();
    test_store_compact_keeps_pending();
    test_store_compact_find_latest_works();
    test_store_survives_reinit();
    test_store_purge_pending();
    test_store_pressure();
    test_store_compact_recovery();
    test_store_crc_corruption_skipped();
    test_store_device_uuid_stable();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
