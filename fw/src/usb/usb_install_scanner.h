#pragma once
// usb_install_scanner.h — scan /JLPICART/INSTALL/ and install Collections.
//
// Testability: run_scan() accepts an abstract InstallDirSource.

#include "usb/usb_host.h"
#include "content/installer.h"
#include "spine/policy.h"
#include <cstddef>

static constexpr size_t INSTALL_SCAN_MAX_DIRS = 8;

// ---------------------------------------------------------------------------
// InstallDirSource — abstract source of install intent directories
// ---------------------------------------------------------------------------

class InstallDirSource {
public:
    virtual ~InstallDirSource() = default;

    virtual size_t count() const = 0;
    virtual const char* dir_name(size_t idx) const = 0;
    virtual InstallReader& open_reader(size_t idx) = 0;
};

// ---------------------------------------------------------------------------
// UsbInstallScanner
// ---------------------------------------------------------------------------

class UsbInstallScanner {
public:
    explicit UsbInstallScanner(UsbHost& host);

    // Production entry point — builds FatFsInstallDirSource, calls run_scan().
    void scan(const PolicyStore& policy);

    // Testable entry point.
    void run_scan(InstallDirSource& dirs, const PolicyStore& policy);

private:
    UsbHost& host_;

    // Check FAT for an active CollectionRecord with matching id+version.
    static bool already_installed(const char* collection_id, const char* version);
};
