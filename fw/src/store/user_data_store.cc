// user_data_store.cc — Device-local user data: settings and save blobs.

#include "store/user_data_store.h"
#include "filesystem/fat_util.h"
#include <cstring>
#include <cstdio>

// ===========================================================================
// init
// ===========================================================================

DiagStatus UserDataStore::init()
{
    initialized_ = false;

    settings_ = settings_defaults();
    {
        size_t act = 0;
        SystemSettings tmp = {};
        if (fat_read_file(SETTINGS_PATH, &tmp, sizeof(tmp), &act)
            && act == sizeof(SystemSettings))
        {
            memcpy(&settings_, &tmp, sizeof(settings_));
        }
    }

    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) save_handles_[i].in_use = false;

    initialized_ = true;
    return DiagStatus::success();
}

// ===========================================================================
// Settings
// ===========================================================================

SystemSettings UserDataStore::settings_defaults()
{
    SystemSettings s = {};
    memset(s.wifi_ssid, 0, sizeof(s.wifi_ssid));
    memset(s.wifi_pass, 0, sizeof(s.wifi_pass));
    strncpy(s.language, "en", sizeof(s.language) - 1u);
    s.video_mode      = SYS_VIDEO_AUTO;
    s.network_enabled = 1u;
    s.source_priority[0] = SYS_SRC_FLASH;
    s.source_priority[1] = SYS_SRC_USB;
    s.source_priority[2] = SYS_SRC_OPTICAL;
    s.source_priority[3] = SYS_SRC_NETWORK;
    return s;
}

DiagStatus UserDataStore::settings_load()
{
    settings_ = settings_defaults();
    size_t act = 0;
    SystemSettings tmp = {};
    if (!fat_read_file(SETTINGS_PATH, &tmp, sizeof(tmp), &act))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    if (act != sizeof(SystemSettings))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    memcpy(&settings_, &tmp, sizeof(settings_));
    return DiagStatus::success();
}

DiagStatus UserDataStore::settings_save()
{
    fat_ensure_dir("1:/system");
    if (!fat_write_file(SETTINGS_PATH, &settings_, sizeof(settings_)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

DiagStatus UserDataStore::settings_set(const SystemSettings& s)
{
    settings_ = s;
    return settings_save();
}

// ===========================================================================
// Saves — path helpers
// ===========================================================================

void UserDataStore::save_make_blob_path(char* buf, size_t sz, uint16_t bid)
{
    snprintf(buf, sz, "1:/saves/%04x.sav", static_cast<unsigned>(bid));
}

void UserDataStore::save_make_idx_path(char* buf, size_t sz)
{
    snprintf(buf, sz, "1:/saves/index.bin");
}

// ===========================================================================
// Saves — registry (single shared index file)
// ===========================================================================

static constexpr uint16_t REG_ENTRY_SIZE  = 6u;
static constexpr uint16_t REG_MAX_ENTRIES =
    static_cast<uint16_t>((SAVE_MAX_BLOB - 2u) / REG_ENTRY_SIZE);

void UserDataStore::save_update_registry(uint16_t bid, uint16_t flags, uint16_t size)
{
    char idx[32];
    save_make_idx_path(idx, sizeof(idx));

    uint8_t buf[SAVE_MAX_BLOB];
    size_t  actual = 0;
    fat_read_file(idx, buf, sizeof(buf), &actual);

    uint16_t n = 0;
    if (actual >= 2u) memcpy(&n, buf, 2u);

    bool found = false;
    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == bid) {
            memcpy(e + 2, &flags, 2u);
            memcpy(e + 4, &size,  2u);
            found = true;
            break;
        }
    }
    if (!found && n < REG_MAX_ENTRIES) {
        uint8_t* e = buf + 2u + n * REG_ENTRY_SIZE;
        memcpy(e + 0, &bid,   2u);
        memcpy(e + 2, &flags, 2u);
        memcpy(e + 4, &size,  2u);
        ++n;
    }
    memcpy(buf, &n, 2u);
    fat_ensure_dir("1:/saves");
    fat_write_file(idx, buf, static_cast<size_t>(2u + n * REG_ENTRY_SIZE));
}

void UserDataStore::save_remove_from_registry(uint16_t bid)
{
    char idx[32];
    save_make_idx_path(idx, sizeof(idx));

    uint8_t buf[SAVE_MAX_BLOB];
    size_t  actual = 0;
    if (!fat_read_file(idx, buf, sizeof(buf), &actual) || actual < 2u) return;

    uint16_t n = 0;
    memcpy(&n, buf, 2u);

    for (uint16_t i = 0; i < n; ++i) {
        uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        uint16_t id;
        memcpy(&id, e, 2u);
        if (id == bid) {
            uint16_t rem = static_cast<uint16_t>(n - i - 1u);
            if (rem > 0) memmove(e, e + REG_ENTRY_SIZE, rem * REG_ENTRY_SIZE);
            --n;
            memcpy(buf, &n, 2u);
            fat_write_file(idx, buf, static_cast<size_t>(2u + n * REG_ENTRY_SIZE));
            return;
        }
    }
}

// ===========================================================================
// Saves — public API
// ===========================================================================

uint8_t UserDataStore::save_list(uint8_t /*kind*/, BlobInfo* out, uint8_t max) const
{
    if (!initialized_) return 0u;
    char idx[32];
    save_make_idx_path(idx, sizeof(idx));

    uint8_t buf[SAVE_MAX_BLOB];
    size_t  actual = 0;
    if (!fat_read_file(idx, buf, sizeof(buf), &actual) || actual < 4u) return 0u;

    uint16_t n;
    memcpy(&n, buf, 2u);
    if (actual < static_cast<size_t>(2u + n * REG_ENTRY_SIZE)) return 0u;

    uint8_t count = 0;
    for (uint16_t i = 0; i < n && count < max; ++i) {
        const uint8_t* e = buf + 2u + i * REG_ENTRY_SIZE;
        BlobInfo& bi = out[count++];
        memcpy(&bi.blob_id,  e + 0, 2);
        memcpy(&bi.flags,    e + 2, 2);
        memcpy(&bi.size,     e + 4, 2);
        bi.max_bytes = SAVE_MAX_BLOB;
    }
    return count;
}

DiagStatus UserDataStore::save_read(uint16_t bid, uint32_t offset,
                                     uint8_t* buf, uint16_t len) const
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char path[32];
    save_make_blob_path(path, sizeof(path), bid);

    uint8_t val[SAVE_MAX_BLOB];
    size_t  val_len = 0;
    if (!fat_read_file(path, val, sizeof(val), &val_len))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    if (offset >= val_len) return DiagStatus::success();
    uint16_t avail = static_cast<uint16_t>(val_len - static_cast<uint16_t>(offset));
    if (len > avail) len = avail;
    memcpy(buf, val + offset, len);
    return DiagStatus::success();
}

DiagStatus UserDataStore::save_write_begin(uint16_t bid, uint16_t total_len,
                                            uint16_t flags, uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (total_len > SAVE_MAX_BLOB) return DiagStatus::error(DiagCode::STORAGE_FULL);

    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        if (!save_handles_[i].in_use) {
            save_handles_[i].in_use         = true;
            save_handles_[i].blob_id        = bid;
            save_handles_[i].total_len      = total_len;
            save_handles_[i].flags          = flags;
            save_handles_[i].bytes_written  = 0u;
            memset(save_handles_[i].staging, 0, sizeof(save_handles_[i].staging));
            if (handle_out) *handle_out = static_cast<uint8_t>(i);
            return DiagStatus::success();
        }
    }
    return DiagStatus::error(DiagCode::STORAGE_FULL);
}

DiagStatus UserDataStore::save_write_chunk(uint8_t handle, uint32_t offset,
                                            const uint8_t* buf, uint16_t len)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !save_handles_[handle].in_use)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    WriteHandle& h = save_handles_[handle];
    if (offset + len > h.total_len) return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    memcpy(h.staging + offset, buf, len);
    if (offset + len > h.bytes_written)
        h.bytes_written = static_cast<uint16_t>(offset + len);
    return DiagStatus::success();
}

DiagStatus UserDataStore::save_write_commit(uint8_t handle)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= SAVE_WRITE_HANDLES || !save_handles_[handle].in_use)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    WriteHandle& h = save_handles_[handle];
    fat_ensure_dir("1:/saves");

    char path[32];
    save_make_blob_path(path, sizeof(path), h.blob_id);
    if (!fat_write_file(path, h.staging, h.total_len)) {
        h.in_use = false;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    save_update_registry(h.blob_id, h.flags, h.total_len);
    h.in_use = false;
    return DiagStatus::success();
}

DiagStatus UserDataStore::save_delete(uint16_t bid)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    char path[32];
    save_make_blob_path(path, sizeof(path), bid);
    fat_delete_file(path);
    save_remove_from_registry(bid);
    return DiagStatus::success();
}

// ===========================================================================
// Factory reset
// ===========================================================================

DiagStatus UserDataStore::wipe_user_data()
{
    DIR dir;
    FILINFO fi;
    if (f_opendir(&dir, "1:/saves") == FR_OK) {
        while (f_readdir(&dir, &fi) == FR_OK && fi.fname[0]) {
            char path[9 + sizeof(fi.fname)];
            strcpy(path, "1:/saves/");
            strncat(path, fi.fname, sizeof(fi.fname) - 1u);
            f_unlink(path);
        }
        f_closedir(&dir);
        f_unlink("1:/saves");
    }
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) save_handles_[i].in_use = false;
    return DiagStatus::success();
}

DiagStatus UserDataStore::full_wipe()
{
    wipe_user_data();
    fat_delete_file(SETTINGS_PATH);
    settings_ = settings_defaults();
    return DiagStatus::success();
}
