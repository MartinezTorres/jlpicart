#pragma once
// fat_volume.h — Internal flash FAT volume lifecycle management.
//
// Owns the FATFS work area for logical drive "1:" (internal flash).
// Call mount() once at boot; it formats the volume automatically on first boot.
// Call unmount() before entering USB device mode so the host PC can take over.
//
// Directory layout created on first format:
//   1:/collections/    game ROM collections (each a sub-directory)
//   1:/profiles/       user profile JSON files
//   1:/saves/          per-collection per-profile save files
//   1:/system/         device key, policy, event log (not user-visible in intent)
//
// Thread safety: NOT thread-safe.  Use only from a single core/thread.

#include "ff.h"

class FatVolume {
public:
    // Mount the FAT volume.  On first boot (FR_NO_FILESYSTEM) formats it
    // and creates the standard directory tree.
    // Returns true on success.
    bool mount();

    // Unmount the volume.  Call before handing the physical drive to the
    // USB device MSC layer so the host PC sees a consistent state.
    void unmount();

    bool mounted() const { return mounted_; }

    // Logical drive path prefix used for all file operations ("1:/").
    static constexpr const char* DRIVE = "1:/";

    // Well-known top-level directories.
    static constexpr const char* DIR_COLLECTIONS = "1:/collections";
    static constexpr const char* DIR_PROFILES     = "1:/profiles";
    static constexpr const char* DIR_SAVES        = "1:/saves";
    static constexpr const char* DIR_SYSTEM       = "1:/system";

private:
    FATFS  fs_   = {};
    bool   mounted_ = false;

    bool format_and_scaffold();
    bool make_dir_if_missing(const char* path);
};
