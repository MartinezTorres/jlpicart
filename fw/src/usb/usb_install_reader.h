#pragma once
// usb_install_reader.h — InstallReader backed by FatFs over tinyusb MSC.
//
// Reads files from a single Install Intent directory on the USB stick
// (e.g. "0:/JLPICART/INSTALL/com.example.game/").
//
// Compiled for firmware only (not in JLPICART_HOST_TEST builds).
// Tests use MemInstallReader from tests/host/test_helpers.h instead.

#ifndef JLPICART_HOST_TEST

#include "content/installer.h"
#include "crypto/sha256.h"
#include <cstddef>
#include <cstdint>

class UsbInstallReader : public InstallReader {
public:
    // root must include the trailing slash, e.g.
    //   "0:/JLPICART/INSTALL/com.example.game/"
    explicit UsbInstallReader(const char* root);

    DiagStatus read_file(const char* path, uint8_t* buf,
                         size_t max_len, size_t* out_len) override;

    DiagStatus hash_file(const char* path,
                         uint8_t digest[SHA256_DIGEST_SIZE]) override;

    bool file_exists(const char* path) override;

private:
    // Full FatFs path = root_ + path.  root_ is always <= 64 chars.
    static constexpr size_t ROOT_MAX  = 80;
    static constexpr size_t CHUNK_SIZE = 512;   // hash streaming chunk

    char root_[ROOT_MAX];

    // Build the full path into buf (buf must be at least ROOT_MAX + path_len).
    void full_path(char* buf, size_t buf_size, const char* path) const;
};

#endif // JLPICART_HOST_TEST
