#pragma once
// usb_install_scanner.h — scan /JLPICART/INSTALL/ and install Collections.
//
// On boot, scan() enumerates up to INSTALL_SCAN_MAX_DIRS subdirectories
// under /JLPICART/INSTALL/ and calls Installer::run() for each one whose
// collection_id + version is not already active in the KvStore.
//
// Testability: run_scan() accepts an abstract InstallDirSource so the skip
// and max-dirs logic can be exercised without a real USB device or FatFs.
// Tests call run_scan() directly with a MemInstallDirSource.

#include "usb/usb_host.h"
#include "content/installer.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"
#include "spine/policy_store.h"
#include <cstddef>

static constexpr size_t INSTALL_SCAN_MAX_DIRS = 8;

// ---------------------------------------------------------------------------
// InstallDirSource — abstract source of install intent directories
//
// Production: FatFsInstallDirSource (defined in usb_install_scanner.cc,
//             uses f_opendir / f_readdir; not compiled in host test builds).
// Tests:      MemInstallDirSource (defined in test_usb_scanner.cc).
// ---------------------------------------------------------------------------

class InstallDirSource {
public:
    virtual ~InstallDirSource() = default;

    // Total number of install directories available.
    virtual size_t count() const = 0;

    // Null-terminated name (not full path) of directory at index idx.
    virtual const char* dir_name(size_t idx) const = 0;

    // Returns an InstallReader for the directory at index idx.
    // Lifetime is owned by the source; callers must not store the reference
    // beyond the current scan_with() call.
    virtual InstallReader& open_reader(size_t idx) = 0;
};

// ---------------------------------------------------------------------------
// UsbInstallScanner
// ---------------------------------------------------------------------------

class UsbInstallScanner {
public:
    explicit UsbInstallScanner(UsbHost& host);

    // Production entry point.
    //   - Returns immediately if !host.is_msc_mounted().
    //   - Builds a FatFsInstallDirSource (firmware only) and calls run_scan().
    void scan(KvStore& kv, AppendLog& event_log, const PolicyStore& policy);

    // Testable entry point: inject any InstallDirSource.
    // Iterates over min(dirs.count(), INSTALL_SCAN_MAX_DIRS) directories.
    // For each: quick skip if already installed, else Installer::run().
    void run_scan(InstallDirSource& dirs,
                  KvStore& kv, AppendLog& event_log,
                  const PolicyStore& policy);

private:
    UsbHost& host_;

    // Check KvStore for an active CollectionRecord with matching id+version.
    static bool already_installed(KvStore& kv,
                                   const char* collection_id,
                                   const char* version);
};
