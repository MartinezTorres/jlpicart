#pragma once
// user_data_store.h — Unified user data: profiles, settings, saves, stats.
//
// Replaces the four separate stores (ProfileStore, SystemSettingsStore,
// SaveStore, StatsStore) with a single class and a single init() call.
//
// Backed by:
//   1:/system/profiles.bin     — ProfileManifest (FAT, atomic write)
//   1:/system/settings.bin     — SystemSettings  (FAT, raw blob)
//   1:/system/events.bin       — append-only event log (stats/achievements/sync)
//   1:/saves/{prof}/{blob}.sav — game save blobs
//
// Thread safety: NOT thread-safe.

#include "store/uuid.h"
#include "store/store.h"
#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

class DeviceIdentity;

// ---------------------------------------------------------------------------
// Profile types and constants
// ---------------------------------------------------------------------------

static constexpr uint8_t  PROF_MAX_PROFILES = 8u;
static constexpr uint16_t PROF_ID_NONE      = 0x0000u;
static constexpr uint16_t PROF_ID_GUEST     = 0xFFFFu;
static constexpr uint8_t  PROF_NAME_MAX     = 32u;
static constexpr uint8_t  PROF_LANG_MAX     = 8u;

struct ProfileRecord {
    uint16_t profile_id;
    Uuid     uuid;
    char     name[PROF_NAME_MAX];
    char     lang[PROF_LANG_MAX];
    uint8_t  flags;
    uint8_t  _pad;
};

// ---------------------------------------------------------------------------
// System settings types and constants
// ---------------------------------------------------------------------------

static constexpr uint8_t SYS_VIDEO_AUTO    = 0u;
static constexpr uint8_t SYS_VIDEO_CRT     = 1u;
static constexpr uint8_t SYS_VIDEO_VGA     = 2u;

static constexpr uint8_t SYS_SRC_FLASH     = 0u;
static constexpr uint8_t SYS_SRC_USB       = 1u;
static constexpr uint8_t SYS_SRC_OPTICAL   = 2u;
static constexpr uint8_t SYS_SRC_NETWORK   = 3u;

#pragma pack(push, 1)
struct SystemSettings {
    char    wifi_ssid[64];
    char    wifi_pass[64];
    char    language[8];
    uint8_t video_mode;
    uint8_t network_enabled;
    uint8_t guest_allowed;
    uint8_t source_priority[4];
    uint8_t reserved[49];
};
#pragma pack(pop)
static_assert(sizeof(SystemSettings) == 192, "SystemSettings must be 192 bytes");

// ---------------------------------------------------------------------------
// Save data types and constants
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
// Stats / leaderboard types and constants
// ---------------------------------------------------------------------------

static constexpr int     STATS_TOKEN_SLOTS     = 4;
static constexpr uint8_t STATS_TOKEN_LEN       = 16u;
static constexpr size_t  STATS_PAYLOAD_KEY_MAX = 32u;
static constexpr size_t  LEADER_SIG_LEN        = 64u;

struct LeaderEntry {
    uint32_t score;
    uint32_t timestamp;
    uint8_t  signature[LEADER_SIG_LEN];
};
static_assert(sizeof(LeaderEntry) == 72, "LeaderEntry must be 72 bytes");

// ---------------------------------------------------------------------------
// UserDataStore
// ---------------------------------------------------------------------------

class UserDataStore {
public:
    // Initialize all domains.  FAT volume must be mounted before calling.
    // Missing files produce defaults; event log errors disable sync but do
    // not fail init.  Always returns success once the FAT is available.
    DiagStatus init();
    bool initialized() const { return initialized_; }

    // ---- Profiles ----

    uint8_t    profile_list(ProfileRecord* buf, uint8_t max) const;
    uint8_t    profile_count() const { return prof_count_; }
    Uuid       profile_uuid_for(uint16_t profile_id) const;
    DiagStatus profile_create(const char* name, const char* lang, uint16_t* id_out);
    DiagStatus profile_get(uint16_t profile_id, ProfileRecord* out) const;
    DiagStatus profile_remove(uint16_t profile_id);
    DiagStatus profile_set_active(uint16_t profile_id);
    uint16_t   profile_active() const { return prof_active_id_; }
    void       profile_begin_guest();
    void       profile_end_guest();
    bool       profile_in_guest_session() const { return prof_active_id_ == PROF_ID_GUEST; }
    DiagStatus profile_wipe_all();

    // ---- Settings ----

    const SystemSettings& settings() const { return settings_; }
    DiagStatus            settings_load();
    DiagStatus            settings_save();
    DiagStatus            settings_set(const SystemSettings& s);
    static SystemSettings settings_defaults();

    // ---- Saves ----

    uint8_t    save_list(uint16_t profile_id, uint8_t kind,
                         BlobInfo* out, uint8_t max) const;
    DiagStatus save_read(uint16_t profile_id, uint16_t blob_id,
                         uint32_t offset, uint8_t* buf, uint16_t len) const;
    DiagStatus save_write_begin(uint16_t profile_id, uint16_t blob_id,
                                uint16_t total_len, uint16_t flags,
                                uint8_t* handle_out);
    DiagStatus save_write_chunk(uint8_t handle, uint32_t offset,
                                const uint8_t* buf, uint16_t len);
    DiagStatus save_write_commit(uint8_t handle);
    DiagStatus save_delete(uint16_t profile_id, uint16_t blob_id);

    // ---- Stats / achievements / leaderboards ----

    DiagStatus stat_get(uint16_t profile_id, const char* payload_id,
                        uint16_t stat_id, int32_t* out) const;
    DiagStatus stat_set(uint16_t profile_id, const char* payload_id,
                        uint16_t stat_id, int32_t value, uint8_t op);
    DiagStatus ach_unlock(uint16_t profile_id, const char* payload_id,
                          uint16_t ach_id);
    DiagStatus ach_get(uint16_t profile_id, const char* payload_id,
                       uint16_t ach_id, bool* unlocked) const;
    DiagStatus leader_begin(uint16_t profile_id, const char* payload_id,
                             uint16_t lb_id,
                             uint8_t* token_out, uint8_t* handle_out);
    DiagStatus leader_submit(uint8_t handle, uint32_t score,
                              uint8_t proof_kind,
                              const uint8_t* proof_buf, uint16_t proof_len,
                              DeviceIdentity* dik = nullptr);

    // ---- Factory reset ----

    DiagStatus wipe_user_data();
    DiagStatus full_wipe();

private:
    bool initialized_ = false;
    bool store_ok_    = false;   // true when the event log initialized successfully

    // ---- Profile private state ----
    static constexpr const char* PROF_MANIFEST_PATH = "1:/system/profiles.bin";

    // On-disk entry layout.  60 bytes; must match the FAT binary format.
    struct ProfileSlot {
        uint16_t profile_id;
        Uuid     uuid;
        char     name[PROF_NAME_MAX];
        char     lang[PROF_LANG_MAX];
        uint8_t  flags;
        uint8_t  _pad2;
    };
    static_assert(sizeof(ProfileSlot) == 60u, "ProfileSlot must be 60 bytes");

    uint8_t     prof_count_     = 0;
    uint16_t    prof_active_id_ = PROF_ID_NONE;
    uint16_t    prof_pre_guest_ = PROF_ID_NONE;
    ProfileSlot prof_slots_[PROF_MAX_PROFILES] = {};

    // ---- Settings private state ----
    static constexpr const char* SETTINGS_PATH = "1:/system/settings.bin";
    SystemSettings settings_ = {};

    // ---- Save private state ----
    struct WriteHandle {
        bool     in_use;
        uint16_t profile_id;
        uint16_t blob_id;
        uint16_t total_len;
        uint16_t flags;
        uint16_t bytes_written;
        uint8_t  staging[SAVE_MAX_BLOB];
    };
    WriteHandle save_handles_[SAVE_WRITE_HANDLES] = {};

    // ---- Stats private state ----
    struct TokenSlot {
        bool     in_use;
        uint8_t  token[STATS_TOKEN_LEN];
        uint16_t profile_id;
        char     payload_id[STATS_PAYLOAD_KEY_MAX + 1];
        uint16_t lb_id;
    };
    TokenSlot stats_tokens_[STATS_TOKEN_SLOTS] = {};
    uint32_t  stats_token_counter_ = 0u;

    // ---- Event log ----
    Store store_;

    // ---- Profile helpers ----
    DiagStatus prof_save_manifest();
    void       prof_append_to_store(const ProfileSlot& slot);

    // ---- Save helpers ----
    static void save_make_prof_dir (char* buf, size_t sz, uint16_t pid);
    static void save_make_blob_path(char* buf, size_t sz, uint16_t pid, uint16_t bid);
    static void save_make_idx_path (char* buf, size_t sz, uint16_t pid);
    void save_update_registry(uint16_t pid, uint16_t bid, uint16_t flags, uint16_t size);
    void save_remove_from_registry(uint16_t pid, uint16_t bid);

    // ---- Stats helpers ----
    Uuid        stat_profile_uuid(uint16_t profile_id) const;
    static void stats_make_path(char* out, size_t sz, uint16_t pid,
                                 const char* payload_id, char type_char, uint16_t id);
    static void stat_id_str(char* buf, size_t sz, uint16_t id);
};
