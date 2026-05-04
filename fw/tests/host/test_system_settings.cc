// test_system_settings.cc — host tests for SystemSettingsStore (Stage 17).
//
// Tests verify: defaults, round-trip persistence, missing-key fallback,
// corrupt-blob fallback, and factory reset operations.

#include "settings/system_settings_store.h"
#include "settings/system_settings.h"
#include "profiles/profile_store.h"

#include "fat_test_env.h"
#include "storage/fat_util.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// test_settings_defaults — fresh store yields canonical defaults
// ---------------------------------------------------------------------------

static void test_settings_defaults()
{
    FatTestEnv env;

    SystemSettingsStore store;
    store.init();

    const SystemSettings& cfg = store.get();
    CHECK(cfg.wifi_ssid[0] == '\0');
    CHECK(cfg.wifi_pass[0] == '\0');
    CHECK(strcmp(cfg.language, "en") == 0);
    CHECK(cfg.video_mode      == SYS_VIDEO_AUTO);
    CHECK(cfg.network_enabled == 1u);
    CHECK(cfg.guest_allowed   == 1u);
    CHECK(cfg.source_priority[0] == SYS_SRC_FLASH);
    CHECK(cfg.source_priority[1] == SYS_SRC_USB);
    CHECK(cfg.source_priority[2] == SYS_SRC_OPTICAL);
    CHECK(cfg.source_priority[3] == SYS_SRC_NETWORK);
}

// ---------------------------------------------------------------------------
// test_settings_roundtrip — save custom values, reload → same values
// ---------------------------------------------------------------------------

static void test_settings_roundtrip()
{
    FatTestEnv env;

    SystemSettings s = SystemSettingsStore::defaults();
    strncpy(s.wifi_ssid, "MyNetwork", sizeof(s.wifi_ssid) - 1u);
    strncpy(s.wifi_pass, "hunter2",   sizeof(s.wifi_pass) - 1u);
    strncpy(s.language,  "fr",        sizeof(s.language)  - 1u);
    s.video_mode      = SYS_VIDEO_CRT;
    s.network_enabled = 0u;
    s.guest_allowed   = 0u;

    {
        SystemSettingsStore store;
        store.init();
        DiagStatus ds = store.set(s);
        CHECK(ds.ok());
    }

    // Reload from same FAT volume (simulates reboot using same flash device).
    {
        SystemSettingsStore store2;
        store2.init();
        const SystemSettings& cfg = store2.get();
        CHECK(strcmp(cfg.wifi_ssid, "MyNetwork") == 0);
        CHECK(strcmp(cfg.wifi_pass, "hunter2")   == 0);
        CHECK(strcmp(cfg.language,  "fr")         == 0);
        CHECK(cfg.video_mode      == SYS_VIDEO_CRT);
        CHECK(cfg.network_enabled == 0u);
        CHECK(cfg.guest_allowed   == 0u);
    }
}

// ---------------------------------------------------------------------------
// test_settings_missing_key — absent file → defaults applied; no crash
// ---------------------------------------------------------------------------

static void test_settings_missing_key()
{
    FatTestEnv env;

    // Don't write anything; just init the store.
    SystemSettingsStore store;
    DiagStatus init_status = DiagStatus::success(); // init() calls load() internally
    store.init();
    // init() succeeds even with a missing file (load() returns NOT_FOUND internally).
    CHECK(store.initialized());

    // Explicit load() should return STORAGE_NOT_FOUND.
    DiagStatus s = store.load();
    CHECK(s.code == DiagCode::STORAGE_NOT_FOUND);

    // get() must still return defaults (not garbage).
    const SystemSettings& cfg = store.get();
    CHECK(strcmp(cfg.language, "en") == 0);
    CHECK(cfg.network_enabled == 1u);

    (void)init_status;
}

// ---------------------------------------------------------------------------
// test_settings_corrupt — short blob → defaults applied
// ---------------------------------------------------------------------------

static void test_settings_corrupt()
{
    FatTestEnv env;

    // Write a blob that is too short to be a valid SystemSettings.
    uint8_t garbage[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    fat_ensure_dir("1:/system");
    fat_write_file("1:/system/settings.bin", garbage, sizeof(garbage));

    SystemSettingsStore store;
    store.init();

    // load() should detect wrong size and apply defaults.
    DiagStatus s = store.load();
    CHECK(s.code == DiagCode::STORAGE_IO_ERROR);

    const SystemSettings& cfg = store.get();
    CHECK(strcmp(cfg.language, "en") == 0);
}

// ---------------------------------------------------------------------------
// test_wipe_user_data — profiles are wiped; system settings intact
// ---------------------------------------------------------------------------

static void test_wipe_user_data()
{
    FatTestEnv env;

    ProfileStore profiles;
    profiles.init();

    // Write a system settings value.
    SystemSettingsStore store;
    store.init();
    SystemSettings cfg = SystemSettingsStore::defaults();
    strncpy(cfg.language, "de", sizeof(cfg.language) - 1u);
    store.set(cfg);

    // Write a profile.
    uint16_t pid = 0u;
    CHECK(profiles.create("Alice", "en", &pid).ok());
    CHECK(profiles.count() == 1u);

    // Wipe user data.
    DiagStatus ws = store.wipe_user_data(profiles);
    CHECK(ws.ok());

    // Profiles should be gone.
    CHECK(profiles.count() == 0u);
    CHECK(profiles.active() == PROF_ID_NONE);

    // System settings should still be present.
    store.load();
    CHECK(strcmp(store.get().language, "de") == 0);
}

// ---------------------------------------------------------------------------
// test_full_wipe — system settings also revert to defaults on next load
// ---------------------------------------------------------------------------

static void test_full_wipe()
{
    FatTestEnv env;

    ProfileStore profiles;
    profiles.init();

    SystemSettingsStore store;
    store.init();

    // Write non-default settings.
    SystemSettings cfg = SystemSettingsStore::defaults();
    strncpy(cfg.language, "ja", sizeof(cfg.language) - 1u);
    store.set(cfg);
    CHECK(strcmp(store.get().language, "ja") == 0);

    // Full wipe.
    DiagStatus ws = store.full_wipe(profiles);
    CHECK(ws.ok());

    // After reload, settings should be defaults again.
    DiagStatus ls = store.load();
    // File is gone — STORAGE_NOT_FOUND expected.
    CHECK(ls.code == DiagCode::STORAGE_NOT_FOUND);
    CHECK(strcmp(store.get().language, "en") == 0);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_settings_defaults();
    test_settings_roundtrip();
    test_settings_missing_key();
    test_settings_corrupt();
    test_wipe_user_data();
    test_full_wipe();

    return test_summary();
}
