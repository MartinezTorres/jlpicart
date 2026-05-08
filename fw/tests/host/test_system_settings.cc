// test_system_settings.cc — host tests for system settings via UserDataStore.

#include "store/user_data_store.h"

#include "fat_test_env.h"
#include "filesystem/fat_util.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// test_settings_defaults — fresh store yields canonical defaults
// ---------------------------------------------------------------------------

static void test_settings_defaults()
{
    FatTestEnv env;

    UserDataStore uds;
    uds.init();

    const SystemSettings& cfg = uds.settings();
    CHECK(cfg.wifi_ssid[0] == '\0');
    CHECK(cfg.wifi_pass[0] == '\0');
    CHECK(strcmp(cfg.language, "en") == 0);
    CHECK(cfg.video_mode      == SYS_VIDEO_AUTO);
    CHECK(cfg.network_enabled == 1u);
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

    SystemSettings s = UserDataStore::settings_defaults();
    strncpy(s.wifi_ssid, "MyNetwork", sizeof(s.wifi_ssid) - 1u);
    strncpy(s.wifi_pass, "hunter2",   sizeof(s.wifi_pass) - 1u);
    strncpy(s.language,  "fr",        sizeof(s.language)  - 1u);
    s.video_mode      = SYS_VIDEO_CRT;
    s.network_enabled = 0u;

    {
        UserDataStore uds;
        uds.init();
        DiagStatus ds = uds.settings_set(s);
        CHECK(ds.ok());
    }

    {
        UserDataStore uds2;
        uds2.init();
        const SystemSettings& cfg = uds2.settings();
        CHECK(strcmp(cfg.wifi_ssid, "MyNetwork") == 0);
        CHECK(strcmp(cfg.wifi_pass, "hunter2")   == 0);
        CHECK(strcmp(cfg.language,  "fr")         == 0);
        CHECK(cfg.video_mode      == SYS_VIDEO_CRT);
        CHECK(cfg.network_enabled == 0u);
    }
}

// ---------------------------------------------------------------------------
// test_settings_missing_key — absent file → defaults applied; no crash
// ---------------------------------------------------------------------------

static void test_settings_missing_key()
{
    FatTestEnv env;

    UserDataStore uds;
    uds.init();
    CHECK(uds.initialized());

    // Explicit load() should return STORAGE_NOT_FOUND.
    DiagStatus s = uds.settings_load();
    CHECK(s.code == DiagCode::STORAGE_NOT_FOUND);

    // settings() must still return defaults (not garbage).
    const SystemSettings& cfg = uds.settings();
    CHECK(strcmp(cfg.language, "en") == 0);
    CHECK(cfg.network_enabled == 1u);
}

// ---------------------------------------------------------------------------
// test_settings_corrupt — short blob → defaults applied
// ---------------------------------------------------------------------------

static void test_settings_corrupt()
{
    FatTestEnv env;

    uint8_t garbage[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    fat_ensure_dir("1:/system");
    fat_write_file("1:/system/settings.bin", garbage, sizeof(garbage));

    UserDataStore uds;
    uds.init();

    // load() should detect wrong size and apply defaults.
    DiagStatus s = uds.settings_load();
    CHECK(s.code == DiagCode::STORAGE_IO_ERROR);

    const SystemSettings& cfg = uds.settings();
    CHECK(strcmp(cfg.language, "en") == 0);
}

// ---------------------------------------------------------------------------
// test_wipe_user_data — saves are wiped; system settings intact
// ---------------------------------------------------------------------------

static void test_wipe_user_data()
{
    FatTestEnv env;

    UserDataStore uds;
    uds.init();

    SystemSettings cfg = UserDataStore::settings_defaults();
    strncpy(cfg.language, "de", sizeof(cfg.language) - 1u);
    uds.settings_set(cfg);

    // Write a save blob.
    uint8_t data[4] = {1, 2, 3, 4};
    uint8_t h = 0xFF;
    uds.save_write_begin(1u, sizeof(data), 0u, &h);
    uds.save_write_chunk(h, 0, data, sizeof(data));
    CHECK(uds.save_write_commit(h).ok());

    BlobInfo infos[4];
    CHECK(uds.save_list(0u, infos, 4u) == 1u);

    DiagStatus ws = uds.wipe_user_data();
    CHECK(ws.ok());

    CHECK(uds.save_list(0u, infos, 4u) == 0u);

    // System settings should still be present.
    uds.settings_load();
    CHECK(strcmp(uds.settings().language, "de") == 0);
}

// ---------------------------------------------------------------------------
// test_full_wipe — system settings also revert to defaults on next load
// ---------------------------------------------------------------------------

static void test_full_wipe()
{
    FatTestEnv env;

    UserDataStore uds;
    uds.init();

    SystemSettings cfg = UserDataStore::settings_defaults();
    strncpy(cfg.language, "ja", sizeof(cfg.language) - 1u);
    uds.settings_set(cfg);
    CHECK(strcmp(uds.settings().language, "ja") == 0);

    DiagStatus ws = uds.full_wipe();
    CHECK(ws.ok());

    // After reload, settings should be defaults again.
    DiagStatus ls = uds.settings_load();
    CHECK(ls.code == DiagCode::STORAGE_NOT_FOUND);
    CHECK(strcmp(uds.settings().language, "en") == 0);
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
