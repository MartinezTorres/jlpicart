#pragma once
// fat_util.h — Thin inline helpers for FAT file I/O (FatFs).
//
// All helpers target logical drive "1:" (internal flash FAT volume).
// FatVolume must be mounted before any of these are called.

#include "ff.h"
#include <cstddef>
#include <cstdint>

// Read up to max_len bytes from a FAT file into buf.
// If actual != nullptr, *actual receives the byte count read.
// Returns true if the file existed and was read without error.
inline bool fat_read_file(const char* path, void* buf, size_t max_len,
                           size_t* actual = nullptr)
{
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return false;
    UINT br = 0;
    FRESULT r = f_read(&f, buf, static_cast<UINT>(max_len), &br);
    f_close(&f);
    if (r != FR_OK) return false;
    if (actual) *actual = static_cast<size_t>(br);
    return true;
}

// Write buf[len] to a FAT file, creating or atomically replacing it.
// Returns true on success.
inline bool fat_write_file(const char* path, const void* buf, size_t len)
{
    FIL f;
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    UINT bw = 0;
    FRESULT r = f_write(&f, buf, static_cast<UINT>(len), &bw);
    f_close(&f);
    return r == FR_OK && static_cast<size_t>(bw) == len;
}

// Return true if the file or directory exists.
inline bool fat_file_exists(const char* path)
{
    FILINFO fi;
    return f_stat(path, &fi) == FR_OK;
}

// Delete a file.  Returns true if deleted or did not exist.
inline bool fat_delete_file(const char* path)
{
    FRESULT r = f_unlink(path);
    return r == FR_OK || r == FR_NO_FILE || r == FR_NO_PATH;
}

// Create directory, succeeding silently if it already exists.
inline bool fat_ensure_dir(const char* path)
{
    FRESULT r = f_mkdir(path);
    return r == FR_OK || r == FR_EXIST;
}

// Append len bytes to a FAT file (creating it if absent).
// Returns true on success.
inline bool fat_append_bytes(const char* path, const void* buf, size_t len)
{
    FIL f;
    if (f_open(&f, path, FA_WRITE | FA_OPEN_APPEND) != FR_OK) return false;
    UINT bw = 0;
    FRESULT r = f_write(&f, buf, static_cast<UINT>(len), &bw);
    f_close(&f);
    return r == FR_OK && static_cast<size_t>(bw) == len;
}

// Return the byte size of a file, or 0 if it does not exist.
inline size_t fat_file_size(const char* path)
{
    FILINFO fi;
    if (f_stat(path, &fi) != FR_OK) return 0u;
    return static_cast<size_t>(fi.fsize);
}
