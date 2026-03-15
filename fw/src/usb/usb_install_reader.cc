// usb_install_reader.cc — UsbInstallReader implementation (FatFs-backed).

#ifndef JLPICART_HOST_TEST

#include "usb/usb_install_reader.h"
#include "diag/diag.h"
#include <cstring>
#include <cstdio>

#include "ff.h"

UsbInstallReader::UsbInstallReader(const char* root) {
    size_t len = strlen(root);
    if (len >= ROOT_MAX) len = ROOT_MAX - 1;
    memcpy(root_, root, len);
    root_[len] = '\0';
}

void UsbInstallReader::full_path(char* buf, size_t buf_size,
                                  const char* path) const {
    snprintf(buf, buf_size, "%s%s", root_, path);
}

DiagStatus UsbInstallReader::read_file(const char* path, uint8_t* buf,
                                        size_t max_len, size_t* out_len) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FIL fil;
    FRESULT res = f_open(&fil, fpath, FA_READ);
    if (res != FR_OK) {
        *out_len = 0;
        return diag_status(DiagCode::STORAGE_NOT_FOUND);
    }

    UINT bytes_read = 0;
    res = f_read(&fil, buf, static_cast<UINT>(max_len), &bytes_read);
    f_close(&fil);

    if (res != FR_OK) {
        *out_len = 0;
        return diag_status(DiagCode::STORAGE_IO_ERROR);
    }

    *out_len = bytes_read;
    return diag_ok();
}

DiagStatus UsbInstallReader::hash_file(const char* path,
                                        uint8_t digest[SHA256_DIGEST_SIZE]) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FIL fil;
    FRESULT res = f_open(&fil, fpath, FA_READ);
    if (res != FR_OK) {
        return diag_status(DiagCode::STORAGE_NOT_FOUND);
    }

    sha256_ctx ctx;
    sha256_init(&ctx);

    uint8_t chunk[CHUNK_SIZE];
    UINT bytes_read = 0;
    for (;;) {
        res = f_read(&fil, chunk, sizeof(chunk), &bytes_read);
        if (res != FR_OK) {
            f_close(&fil);
            return diag_status(DiagCode::STORAGE_IO_ERROR);
        }
        if (bytes_read == 0) break;
        sha256_update(&ctx, chunk, bytes_read);
    }

    f_close(&fil);
    sha256_final(&ctx, digest);
    return diag_ok();
}

bool UsbInstallReader::file_exists(const char* path) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FILINFO info;
    return f_stat(fpath, &info) == FR_OK;
}

#endif // JLPICART_HOST_TEST
