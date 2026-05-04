#pragma once
// usb_install_reader.h — InstallReader backed by FatFs over tinyusb MSC.
//
// Reads files from a single Install Intent directory on the USB stick
// (e.g. "0:/JLPICART/INSTALL/com.example.game/").
//
// Hardware-only: not compiled in JLPICART_HOST_TEST builds.

#ifndef JLPICART_HOST_TEST

#include "content/installer.h"
#include "crypto/sha256.h"
#include <cstddef>

class UsbInstallReader : public InstallReader {
public:
    explicit UsbInstallReader(const char* root);

    DiagStatus read_file(const char* path, uint8_t* buf,
                         size_t max_len, size_t* out_len) override;

    DiagStatus hash_file(const char* path,
                         uint8_t digest[SHA256_DIGEST_SIZE]) override;

    bool file_exists(const char* path) override;

    // Copy a payload file from the USB drive (drive "0:") to a FAT path
    // on the internal flash drive (drive "1:").
    DiagStatus copy_to_fat(const char* src_path, const char* dst_path,
                            size_t* out_size) override;

private:
    static constexpr size_t ROOT_MAX   = 80;
    static constexpr size_t CHUNK_SIZE = 512;

    char root_[ROOT_MAX];

    void full_path(char* buf, size_t buf_size, const char* path) const;
};

#endif // JLPICART_HOST_TEST
