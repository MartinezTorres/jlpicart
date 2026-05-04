// usb_install_scanner.cc — UsbInstallScanner implementation.

#include "usb/usb_install_scanner.h"
#include "content/content_store.h"
#include "content/collection_format.h"
#include "content/manifest.h"
#include "content/manifest_parser.h"
#include "log/log.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// FatFsInstallDirSource — firmware-only
// ---------------------------------------------------------------------------

#ifndef JLPICART_HOST_TEST
#include "ff.h"
#include "usb/usb_install_reader.h"

class FatFsInstallDirSource : public InstallDirSource {
public:
    FatFsInstallDirSource() {
        f_mount(&fs_, "0:", 1);
        enumerate();
    }

    ~FatFsInstallDirSource() {
        for (size_t i = 0; i < count_; ++i) {
            delete readers_[i];
            readers_[i] = nullptr;
        }
        f_unmount("0:");
    }

    size_t count() const override { return count_; }

    const char* dir_name(size_t idx) const override {
        return idx < count_ ? names_[idx] : "";
    }

    InstallReader& open_reader(size_t idx) override {
        return *readers_[idx];
    }

private:
    static constexpr size_t NAME_MAX_  = 64;
    static constexpr const char* SCAN_ROOT = "0:/JLPICART/INSTALL";

    FATFS fs_;
    char  names_[INSTALL_SCAN_MAX_DIRS][NAME_MAX_];
    UsbInstallReader* readers_[INSTALL_SCAN_MAX_DIRS] = {};
    size_t count_ = 0;

    void enumerate() {
        DIR dir;
        if (f_opendir(&dir, SCAN_ROOT) != FR_OK) {
            log_info("USB install: /JLPICART/INSTALL not found");
            return;
        }
        FILINFO info;
        while (count_ < INSTALL_SCAN_MAX_DIRS) {
            if (f_readdir(&dir, &info) != FR_OK || info.fname[0] == '\0') break;
            if (!(info.fattrib & AM_DIR)) continue;
            size_t nlen = strlen(info.fname);
            if (nlen >= NAME_MAX_) nlen = NAME_MAX_ - 1;
            memcpy(names_[count_], info.fname, nlen);
            names_[count_][nlen] = '\0';
            char root[128];
            snprintf(root, sizeof(root), "0:/JLPICART/INSTALL/%s/", info.fname);
            readers_[count_] = new UsbInstallReader(root);
            ++count_;
        }
        f_closedir(&dir);
    }
};

#endif // JLPICART_HOST_TEST

// ---------------------------------------------------------------------------
// UsbInstallScanner
// ---------------------------------------------------------------------------

UsbInstallScanner::UsbInstallScanner(UsbHost& host) : host_(host) {}

bool UsbInstallScanner::already_installed(const char* collection_id,
                                           const char* version) {
    ContentStore cs;
    CollectionRecord rec = {};
    if (!cs.load_collection(rec).ok()) return false;
    return (strncmp(rec.collection_id, collection_id, COL_ID_MAX) == 0 &&
            strncmp(rec.version,       version,       COL_VERSION_MAX) == 0);
}

void UsbInstallScanner::run_scan(InstallDirSource& dirs, const PolicyStore& policy) {
    if (!(policy.info().flags & POLICY_ALLOW_USB_COLLECTION_INSTALL)) {
        log_info("USB install: denied by policy");
        return;
    }

    size_t n = dirs.count();
    if (n > INSTALL_SCAN_MAX_DIRS) n = INSTALL_SCAN_MAX_DIRS;

    for (size_t i = 0; i < n; ++i) {
        const char*    name   = dirs.dir_name(i);
        InstallReader& reader = dirs.open_reader(i);

        uint8_t mbuf[MANIFEST_BYTES_MAX];
        size_t  mlen = 0;
        DiagStatus s = reader.read_file(BUNDLE_MANIFEST_FILE, mbuf, sizeof(mbuf), &mlen);
        if (!s.ok()) {
            char msg[96];
            snprintf(msg, sizeof(msg), "USB install %s: manifest read failed", name);
            log_info(msg);
            continue;
        }

        CollectionManifest manifest = {};
        s = parse_collection_manifest(reinterpret_cast<const char*>(mbuf), mlen, manifest);
        if (!s.ok()) {
            char msg[96];
            snprintf(msg, sizeof(msg), "USB install %s: manifest parse failed", name);
            log_info(msg);
            continue;
        }

        if (already_installed(manifest.collection_id, manifest.version)) {
            char msg[96];
            snprintf(msg, sizeof(msg), "USB install %s: already installed", name);
            log_info(msg);
            continue;
        }

        Installer installer;
        InstallResult result = {};
        installer.run(reader, policy, result);

        char msg[192];
        if (result.installed) {
            snprintf(msg, sizeof(msg), "USB install %s: ok id=%s ver=%s",
                     name, result.collection_id, result.version);
            log_info(msg);
        } else {
            snprintf(msg, sizeof(msg), "USB install %s: failed reason=%d",
                     name, static_cast<int>(result.reason));
            log_warn(msg);
        }
    }
}

void UsbInstallScanner::scan(const PolicyStore& policy) {
    if (!host_.is_msc_mounted()) {
        log_info("USB MSC not mounted — skipping install scan");
        return;
    }
#ifndef JLPICART_HOST_TEST
    FatFsInstallDirSource dirs;
    run_scan(dirs, policy);
#else
    (void)policy;
#endif
}
