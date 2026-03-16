#pragma once
// system_settings_store.h — Load/save interface for SystemSettings (Stage 17).
//
// Wraps KvStore to provide typed access to the system settings blob.
// Factory reset operations are also centralised here.
//
// Spec references: §5.3 "Persistence contract (v1)", §6.2.

#include "settings/system_settings.h"
#include "diag/diag.h"

class KvStore;
class ProfileStore;

class SystemSettingsStore {
public:
    // Attach to a KvStore and immediately call load().
    // Must be called before any other method.
    void init(KvStore& sys_kv);

    bool initialized() const { return initialized_; }

    // (Re)load settings from KV into the cache.
    // Returns STORAGE_NOT_FOUND if the key is absent (defaults applied, not an error).
    // Returns STORAGE_IO_ERROR if the blob is corrupt (defaults applied).
    DiagStatus load();

    // Write the current cached settings to KV.
    DiagStatus save();

    // Return const reference to the cached (loaded or default) settings.
    const SystemSettings& get() const { return settings_; }

    // Update the cache and persist to KV.
    DiagStatus set(const SystemSettings& s);

    // Return a SystemSettings struct with all canonical defaults applied.
    static SystemSettings defaults();

    // ---------------------------------------------------------------------------
    // Factory reset operations
    // ---------------------------------------------------------------------------

    // Wipe user data: deletes all "sav.*" keys from kv_saves and all "prof.*"
    // keys from profiles.  Does NOT touch installed collections or system settings.
    DiagStatus wipe_user_data(KvStore& kv_saves, ProfileStore& profiles);

    // Full wipe: same as wipe_user_data() plus deletes KV_SYS_SETTINGS itself,
    // restoring factory defaults on next boot.
    DiagStatus full_wipe(KvStore& kv_saves, ProfileStore& profiles);

private:
    KvStore*       kv_          = nullptr;
    SystemSettings settings_    = {};
    bool           initialized_ = false;
};
