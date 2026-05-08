#pragma once
// user_data_store.h — Device-local user data: system settings and save blobs.
//
// One user per device.  No profiles, no sync, no cloud.
//
// Backed by:
//   1:/system/settings.bin   — SystemSettings flat blob
//   1:/saves/{id:04x}.sav    — save blobs
//   1:/saves/index.bin       — save index (blob_id → size/flags)

#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

// ---------------------------------------------------------------------------
// System settings
// ---------------------------------------------------------------------------

static constexpr uint8_t SYS_VIDEO_AUTO  = 0u;
static constexpr uint8_t SYS_VIDEO_CRT   = 1u;
static constexpr uint8_t SYS_VIDEO_VGA   = 2u;

static constexpr uint8_t SYS_SRC_FLASH   = 0u;
static constexpr uint8_t SYS_SRC_USB     = 1u;
static constexpr uint8_t SYS_SRC_OPTICAL = 2u;
static constexpr uint8_t SYS_SRC_NETWORK = 3u;

#pragma pack(push, 1)
struct SystemSettings {
    char    wifi_ssid[64];
    char    wifi_pass[64];
    char    language[8];
    uint8_t video_mode;
    uint8_t network_enabled;
    uint8_t source_priority[4];
    uint8_t reserved[50];
};
#pragma pack(pop)
static_assert(sizeof(SystemSettings) == 192, "SystemSettings must be 192 bytes");

// ---------------------------------------------------------------------------
// Save data
// ---------------------------------------------------------------------------

static constexpr int      SAVE_WRITE_HANDLES = 4;
static constexpr uint16_t SAVE_MAX_BLOB      = 512u;

struct BlobInfo {
    uint16_t blob_id;
    uint16_t size;
    uint16_t max_bytes;
    uint16_t flags;
};

// ---------------------------------------------------------------------------
// UserDataStore
// ---------------------------------------------------------------------------

class UserDataStore {
public:
    DiagStatus init();
    bool initialized() const { return initialized_; }

    // ---- Settings ----

    const SystemSettings& settings() const { return settings_; }
    DiagStatus            settings_load();
    DiagStatus            settings_save();
    DiagStatus            settings_set(const SystemSettings& s);
    static SystemSettings settings_defaults();

    // ---- Saves ----

    uint8_t    save_list(uint8_t kind, BlobInfo* out, uint8_t max) const;
    DiagStatus save_read(uint16_t blob_id, uint32_t offset,
                         uint8_t* buf, uint16_t len) const;
    DiagStatus save_write_begin(uint16_t blob_id, uint16_t total_len,
                                uint16_t flags, uint8_t* handle_out);
    DiagStatus save_write_chunk(uint8_t handle, uint32_t offset,
                                const uint8_t* buf, uint16_t len);
    DiagStatus save_write_commit(uint8_t handle);
    DiagStatus save_delete(uint16_t blob_id);

    // ---- Factory reset ----

    DiagStatus wipe_user_data(); // clears saves only
    DiagStatus full_wipe();      // clears saves + settings

private:
    bool initialized_ = false;

    static constexpr const char* SETTINGS_PATH = "1:/system/settings.bin";
    SystemSettings settings_ = {};

    struct WriteHandle {
        bool     in_use       = false;
        uint16_t blob_id      = 0;
        uint16_t total_len    = 0;
        uint16_t flags        = 0;
        uint16_t bytes_written = 0;
        uint8_t  staging[SAVE_MAX_BLOB] = {};
    };
    WriteHandle save_handles_[SAVE_WRITE_HANDLES] = {};

    static void save_make_blob_path(char* buf, size_t sz, uint16_t bid);
    static void save_make_idx_path (char* buf, size_t sz);
    void save_update_registry(uint16_t bid, uint16_t flags, uint16_t size);
    void save_remove_from_registry(uint16_t bid);
};
