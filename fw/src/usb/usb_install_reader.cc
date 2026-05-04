// usb_install_reader.cc — UsbInstallReader implementation (FatFs-backed).

#ifndef JLPICART_HOST_TEST

#include "usb/usb_install_reader.h"
#include "diag/diag.h"
#include "crypto/sha256.h"
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
    if (f_open(&fil, fpath, FA_READ) != FR_OK) {
        *out_len = 0;
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    UINT br = 0;
    FRESULT res = f_read(&fil, buf, static_cast<UINT>(max_len), &br);
    f_close(&fil);
    if (res != FR_OK) {
        *out_len = 0;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    *out_len = br;
    return DiagStatus::success();
}

DiagStatus UsbInstallReader::hash_file(const char* path,
                                        uint8_t digest[SHA256_DIGEST_SIZE]) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FIL fil;
    if (f_open(&fil, fpath, FA_READ) != FR_OK)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    Sha256Ctx ctx;
    sha256_init(&ctx);
    uint8_t chunk[CHUNK_SIZE];
    UINT br = 0;
    for (;;) {
        FRESULT res = f_read(&fil, chunk, sizeof(chunk), &br);
        if (res != FR_OK) { f_close(&fil); return DiagStatus::error(DiagCode::STORAGE_IO_ERROR); }
        if (br == 0) break;
        sha256_update(&ctx, chunk, br);
    }
    f_close(&fil);
    sha256_final(&ctx, digest);
    return DiagStatus::success();
}

bool UsbInstallReader::file_exists(const char* path) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);
    FILINFO info;
    return f_stat(fpath, &info) == FR_OK;
}

DiagStatus UsbInstallReader::copy_to_fat(const char* src_path,
                                          const char* dst_path,
                                          size_t* out_size) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), src_path);

    FIL src;
    if (f_open(&src, fpath, FA_READ) != FR_OK) {
        *out_size = 0;
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    FIL dst;
    if (f_open(&dst, dst_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        f_close(&src);
        *out_size = 0;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    uint8_t buf[CHUNK_SIZE];
    size_t  total = 0;
    UINT    br = 0, bw = 0;
    FRESULT res;
    for (;;) {
        res = f_read(&src, buf, sizeof(buf), &br);
        if (res != FR_OK || br == 0) break;
        res = f_write(&dst, buf, br, &bw);
        if (res != FR_OK || bw < br) break;
        total += br;
    }

    f_close(&src);
    f_close(&dst);
    *out_size = total;
    return (res == FR_OK) ? DiagStatus::success()
                          : DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
}

#endif // JLPICART_HOST_TEST
