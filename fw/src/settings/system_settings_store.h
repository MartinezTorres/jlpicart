#pragma once
// system_settings_store.h — Load/save interface for SystemSettings.
//
// Backed by 1:/system/settings.bin (raw binary blob).
// FatVolume must be mounted before calling init().

#include "settings/system_settings.h"
#include "diag/diag.h"

class ProfileStore;

class SystemSettingsStore {
public:
    // Load settings from FAT.  Must be called before any other method.
    // Missing file → defaults applied (not an error).
    void init();

    bool initialized() const { return initialized_; }

    DiagStatus load();
    DiagStatus save();

    const SystemSettings& get() const { return settings_; }
    DiagStatus set(const SystemSettings& s);

    static SystemSettings defaults();

    // ---------------------------------------------------------------------------
    // Factory reset
    // ---------------------------------------------------------------------------

    // Wipe user data: deletes 1:/saves/ and 1:/profiles/ contents.
    DiagStatus wipe_user_data(ProfileStore& profiles);

    // Full wipe: wipe_user_data() + delete 1:/system/settings.bin.
    DiagStatus full_wipe(ProfileStore& profiles);

private:
    static constexpr const char* PATH = "1:/system/settings.bin";

    SystemSettings settings_    = {};
    bool           initialized_ = false;
};
