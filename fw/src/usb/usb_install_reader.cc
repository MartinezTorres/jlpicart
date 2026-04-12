// usb_install_reader.cc — UsbInstallReader implementation (FatFs-backed).

#ifndef JLPICART_HOST_TEST

#include "usb/usb_install_reader.h"
#include "diag/diag.h"
#include "crypto/sha256.h"
#include "storage/flash_device.h"
#include "storage/flash_layout.h"
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
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    UINT bytes_read = 0;
    res = f_read(&fil, buf, static_cast<UINT>(max_len), &bytes_read);
    f_close(&fil);

    if (res != FR_OK) {
        *out_len = 0;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    *out_len = bytes_read;
    return DiagStatus::success();
}

DiagStatus UsbInstallReader::hash_file(const char* path,
                                        uint8_t digest[SHA256_DIGEST_SIZE]) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FIL fil;
    FRESULT res = f_open(&fil, fpath, FA_READ);
    if (res != FR_OK) {
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    Sha256Ctx ctx;
    sha256_init(&ctx);

    uint8_t chunk[CHUNK_SIZE];
    UINT bytes_read = 0;
    for (;;) {
        res = f_read(&fil, chunk, sizeof(chunk), &bytes_read);
        if (res != FR_OK) {
            f_close(&fil);
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        if (bytes_read == 0) break;
        sha256_update(&ctx, chunk, bytes_read);
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

DiagStatus UsbInstallReader::copy_to_flash(const char* path,
                                             FlashDevice& flash,
                                             uint32_t flash_offset,
                                             size_t* out_size) {
    char fpath[ROOT_MAX + 256];
    full_path(fpath, sizeof(fpath), path);

    FIL fil;
    if (f_open(&fil, fpath, FA_READ) != FR_OK) {
        *out_size = 0;
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }

    uint8_t sector_buf[FLASH_SECTOR_SIZE];
    uint32_t offset = flash_offset;
    size_t total = 0;

    for (;;) {
        UINT bytes_read = 0;
        FRESULT res = f_read(&fil, sector_buf, sizeof(sector_buf), &bytes_read);
        if (res != FR_OK) {
            f_close(&fil);
            *out_size = total;
            return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
        }
        if (bytes_read == 0) break;

        DiagStatus s = flash.erase(offset, 1u);
        if (!s.ok()) { f_close(&fil); *out_size = total; return s; }

        s = flash.write(offset, sector_buf, bytes_read);
        if (!s.ok()) { f_close(&fil); *out_size = total; return s; }

        offset += FLASH_SECTOR_SIZE;
        total  += bytes_read;
        if (bytes_read < sizeof(sector_buf)) break;
    }

    f_close(&fil);
    *out_size = total;
    return DiagStatus::success();
}

#endif // JLPICART_HOST_TEST
