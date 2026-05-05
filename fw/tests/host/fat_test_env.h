#pragma once
// fat_test_env.h — Test fixture: RAM-backed FatFs volume for host tests.
//
// Usage:
//   #include "fat_test_env.h"
//
//   FatTestEnv env;           // mounts fresh 1 MB FAT volume
//   // ... create stores, call init(), do test ...
//   env.reset();              // re-format to blank state between tests

#include "filesystem/fat_volume.h"
#include <cassert>

// Declared in diskio_ram.cc.
void ram_disk_reset();

struct FatTestEnv {
    FatVolume vol;

    FatTestEnv() { ram_disk_reset(); mount_fresh(); }
    ~FatTestEnv() { vol.unmount(); }

    // Re-format to blank state.  Call between test cases that need isolation.
    void reset() {
        vol.unmount();
        ram_disk_reset();
        mount_fresh();
    }

private:
    void mount_fresh() {
        bool ok = vol.mount();
        assert(ok && "FatTestEnv: mount failed — check diskio_ram.cc is linked");
    }
};
