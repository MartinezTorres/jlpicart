// system_settings_store.cc — SystemSettings load/save over KvStore (Stage 17).

#include "settings/system_settings_store.h"
#include "storage/kv_store.h"
#include "profiles/profile_store.h"
#include <cstring>

// ---------------------------------------------------------------------------
// defaults
// ---------------------------------------------------------------------------

SystemSettings SystemSettingsStore::defaults()
{
    SystemSettings s = {};
    memset(&s, 0, sizeof(s));
    // language default: "en"
    s.language[0] = 'e';
    s.language[1] = 'n';
    // video_mode default: auto
    s.video_mode       = SYS_VIDEO_AUTO;
    // network/guest on by default
    s.network_enabled  = 1u;
    s.guest_allowed    = 1u;
    // source priority: flash → usb → optical → network
    s.source_priority[0] = SYS_SRC_FLASH;
    s.source_priority[1] = SYS_SRC_USB;
    s.source_priority[2] = SYS_SRC_OPTICAL;
    s.source_priority[3] = SYS_SRC_NETWORK;
    return s;
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void SystemSettingsStore::init(KvStore& sys_kv)
{
    kv_          = &sys_kv;
    initialized_ = false;
    settings_    = defaults();
    initialized_ = true;
    load(); // ignore return value; missing key → defaults already applied
}

// ---------------------------------------------------------------------------
// load
// ---------------------------------------------------------------------------

DiagStatus SystemSettingsStore::load()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    uint8_t  buf[sizeof(SystemSettings)];
    uint16_t len = 0;
    DiagStatus s = kv_->get(KV_SYS_SETTINGS, buf, &len,
                             static_cast<uint16_t>(sizeof(buf)));
    if (!s.ok()) {
        // Missing or unreadable key — apply defaults (not a fatal error).
        settings_ = defaults();
        return s; // propagate STORAGE_NOT_FOUND / IO_ERROR to caller
    }

    if (len != sizeof(SystemSettings)) {
        // Wrong size — treat as corrupt.
        settings_ = defaults();
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    memcpy(&settings_, buf, sizeof(SystemSettings));
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// save
// ---------------------------------------------------------------------------

DiagStatus SystemSettingsStore::save()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    return kv_->put(KV_SYS_SETTINGS,
                    reinterpret_cast<const uint8_t*>(&settings_),
                    static_cast<uint16_t>(sizeof(SystemSettings)));
}

// ---------------------------------------------------------------------------
// set
// ---------------------------------------------------------------------------

DiagStatus SystemSettingsStore::set(const SystemSettings& s)
{
    settings_ = s;
    return save();
}

// ---------------------------------------------------------------------------
// wipe_user_data
// ---------------------------------------------------------------------------

DiagStatus SystemSettingsStore::wipe_user_data(KvStore& kv_saves, ProfileStore& profiles)
{
    // Delete all save blobs ("sav.*" prefix in saves KV).
    DiagStatus s1 = kv_saves.del_prefix("sav.");

    // Delete all stats/achievements/leaderboards ("st.*" prefix in saves KV).
    DiagStatus s2 = kv_saves.del_prefix("st.");

    // Delete all profile data ("prof.*" prefix in profiles KV) and reset state.
    DiagStatus s3 = profiles.wipe_all();

    // Return the first error encountered; all operations always attempted.
    if (!s1.ok()) return s1;
    if (!s2.ok()) return s2;
    return s3;
}

// ---------------------------------------------------------------------------
// full_wipe
// ---------------------------------------------------------------------------

DiagStatus SystemSettingsStore::full_wipe(KvStore& kv_saves, ProfileStore& profiles)
{
    // Wipe all user data first.
    DiagStatus s1 = wipe_user_data(kv_saves, profiles);

    // Also erase the system settings blob (restores defaults on next boot).
    DiagStatus s2 = kv_->del(KV_SYS_SETTINGS);
    settings_ = defaults(); // update in-memory cache immediately

    return s1.ok() ? s2 : s1;
}
