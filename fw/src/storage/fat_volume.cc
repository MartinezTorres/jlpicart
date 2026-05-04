// fat_volume.cc — FatVolume implementation.

#include "storage/fat_volume.h"
#include "log/log.h"
#include <cstdio>

bool FatVolume::mount() {
    FRESULT res = f_mount(&fs_, DRIVE, 1 /* force mount */);
    if (res == FR_OK) {
        mounted_ = true;
        log_info("FAT volume mounted");
        return true;
    }
    if (res == FR_NO_FILESYSTEM) {
        log_info("FAT volume: no filesystem — formatting");
        return format_and_scaffold();
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "FAT volume mount failed: %d", static_cast<int>(res));
    log_warn(buf);
    return false;
}

void FatVolume::unmount() {
    if (mounted_) {
        f_mount(nullptr, DRIVE, 0);
        mounted_ = false;
        log_info("FAT volume unmounted");
    }
}

bool FatVolume::format_and_scaffold() {
    // mkfs work buffer: FatFs requires at least FF_MAX_SS bytes.
    static uint8_t work[512];
    MKFS_PARM opt = {};
    opt.fmt = FM_ANY | FM_SFD;  // auto-select FAT type, no partition table
    FRESULT res = f_mkfs(DRIVE, &opt, work, sizeof(work));
    if (res != FR_OK) {
        char buf[48];
        snprintf(buf, sizeof(buf), "FAT mkfs failed: %d", static_cast<int>(res));
        log_warn(buf);
        return false;
    }
    log_info("FAT volume formatted");

    res = f_mount(&fs_, DRIVE, 1);
    if (res != FR_OK) {
        log_warn("FAT volume: mount after format failed");
        return false;
    }
    mounted_ = true;

    // Create standard directory tree.
    bool ok = true;
    ok &= make_dir_if_missing(DIR_COLLECTIONS);
    ok &= make_dir_if_missing(DIR_PROFILES);
    ok &= make_dir_if_missing(DIR_SAVES);
    ok &= make_dir_if_missing(DIR_SYSTEM);
    if (ok) log_info("FAT volume scaffolded");
    return ok;
}

bool FatVolume::make_dir_if_missing(const char* path) {
    FRESULT res = f_mkdir(path);
    if (res == FR_OK || res == FR_EXIST) return true;
    char buf[64];
    snprintf(buf, sizeof(buf), "FAT mkdir failed: %s (%d)", path, static_cast<int>(res));
    log_warn(buf);
    return false;
}
