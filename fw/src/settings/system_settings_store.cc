// system_settings_store.cc — SystemSettings load/save over FAT.

#include "settings/system_settings_store.h"
#include "storage/fat_util.h"
#include "profiles/profile_store.h"
#include <cstring>

SystemSettings SystemSettingsStore::defaults()
{
    SystemSettings s = {};
    memset(&s, 0, sizeof(s));
    s.language[0] = 'e';
    s.language[1] = 'n';
    s.video_mode       = SYS_VIDEO_AUTO;
    s.network_enabled  = 1u;
    s.guest_allowed    = 1u;
    s.source_priority[0] = SYS_SRC_FLASH;
    s.source_priority[1] = SYS_SRC_USB;
    s.source_priority[2] = SYS_SRC_OPTICAL;
    s.source_priority[3] = SYS_SRC_NETWORK;
    return s;
}

void SystemSettingsStore::init()
{
    initialized_ = false;
    settings_    = defaults();
    initialized_ = true;
    load(); // missing file → defaults; ignore error
}

DiagStatus SystemSettingsStore::load()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    size_t actual = 0;
    SystemSettings tmp = {};
    if (!fat_read_file(PATH, &tmp, sizeof(tmp), &actual)) {
        settings_ = defaults();
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    if (actual != sizeof(SystemSettings)) {
        settings_ = defaults();
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    memcpy(&settings_, &tmp, sizeof(settings_));
    return DiagStatus::success();
}

DiagStatus SystemSettingsStore::save()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    fat_ensure_dir("1:/system");
    if (!fat_write_file(PATH, &settings_, sizeof(settings_)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

DiagStatus SystemSettingsStore::set(const SystemSettings& s)
{
    settings_ = s;
    return save();
}

DiagStatus SystemSettingsStore::wipe_user_data(ProfileStore& profiles)
{
    // Delete all save files under 1:/saves/.
    // FatFs f_unlink can't recurse; delete all known per-profile dirs.
    // ProfileStore::wipe_all() handles its own FAT cleanup.
    return profiles.wipe_all();
}

DiagStatus SystemSettingsStore::full_wipe(ProfileStore& profiles)
{
    DiagStatus s1 = wipe_user_data(profiles);
    fat_delete_file(PATH);
    settings_ = defaults();
    return s1;
}
