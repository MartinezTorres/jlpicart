// user_data_store.cc — Unified user data store implementation.

#include "store/user_data_store.h"
#include "filesystem/fat_util.h"
#include "spine/device_identity.h"
#include <cstring>
#include <cstdio>

#ifndef JLPICART_HOST_TEST
#include "hardware/structs/rosc.h"
#include "pico/time.h"
#endif

// ===========================================================================
// init
// ===========================================================================

DiagStatus UserDataStore::init()
{
    initialized_ = false;
    store_ok_    = false;

    // Event log — best-effort; failure disables sync but not the rest.
    store_ok_ = store_.init().ok();

    // ---- Profiles ----
    prof_count_     = 0;
    prof_active_id_ = PROF_ID_NONE;
    prof_pre_guest_ = PROF_ID_NONE;
    memset(prof_slots_, 0, sizeof(prof_slots_));

    struct ProfileManifest {
        uint16_t    active_id;
        uint8_t     count;
        uint8_t     _pad;
        ProfileSlot entries[PROF_MAX_PROFILES];
    };
    ProfileManifest m = {};
    size_t actual = 0;
    if (fat_read_file(PROF_MANIFEST_PATH, &m, sizeof(m), &actual)
        && actual == sizeof(ProfileManifest))
    {
        prof_active_id_ = m.active_id;
        uint8_t n = m.count < PROF_MAX_PROFILES ? m.count : PROF_MAX_PROFILES;
        for (uint8_t i = 0; i < n; ++i) prof_slots_[i] = m.entries[i];
        prof_count_ = n;
    }

    // ---- Settings ----
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

    // ---- Save handles ----
    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) save_handles_[i].in_use = false;

    // ---- Stats tokens ----
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i) stats_tokens_[i].in_use = false;
    stats_token_counter_ = 0u;

    initialized_ = true;
    return DiagStatus::success();
}

// ===========================================================================
// Profile
// ===========================================================================

DiagStatus UserDataStore::prof_save_manifest()
{
    struct ProfileManifest {
        uint16_t    active_id;
        uint8_t     count;
        uint8_t     _pad;
        ProfileSlot entries[PROF_MAX_PROFILES];
    };
    ProfileManifest m = {};
    m.active_id = prof_active_id_;
    m.count     = prof_count_;
    for (uint8_t i = 0; i < prof_count_; ++i) m.entries[i] = prof_slots_[i];
    fat_ensure_dir("1:/system");
    if (!fat_write_file(PROF_MANIFEST_PATH, &m, sizeof(m)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

void UserDataStore::prof_append_to_store(const ProfileSlot& slot)
{
    if (!store_ok_) return;

    struct ProfilePayload {
        char    name[PROF_NAME_MAX];
        char    lang[PROF_LANG_MAX];
        uint8_t flags;
        uint8_t _pad[23];
    };
    static_assert(sizeof(ProfilePayload) == 64u, "ProfilePayload must be 64 bytes");

    StoreRecord rec = {};
    StoreKey key = StoreKey::for_profile(slot.uuid);
    memcpy(rec.key, key.data, 32);
    rec.event_type = static_cast<uint8_t>(StoreEntryType::PROFILE);

    ProfilePayload pp = {};
    memcpy(pp.name,  slot.name, PROF_NAME_MAX);
    memcpy(pp.lang,  slot.lang, PROF_LANG_MAX);
    pp.flags = slot.flags;
    memcpy(rec.payload, &pp, sizeof(pp));
    rec.payload_len = sizeof(pp);

    store_.append(rec);
}

uint8_t UserDataStore::profile_list(ProfileRecord* buf, uint8_t max) const
{
    uint8_t n = prof_count_ < max ? prof_count_ : max;
    for (uint8_t i = 0; i < n; ++i) {
        buf[i].profile_id = prof_slots_[i].profile_id;
        buf[i].uuid       = prof_slots_[i].uuid;
        memcpy(buf[i].name, prof_slots_[i].name, PROF_NAME_MAX);
        memcpy(buf[i].lang, prof_slots_[i].lang, PROF_LANG_MAX);
        buf[i].flags = prof_slots_[i].flags;
        buf[i]._pad  = 0;
    }
    return n;
}

Uuid UserDataStore::profile_uuid_for(uint16_t profile_id) const
{
    for (uint8_t i = 0; i < prof_count_; ++i)
        if (prof_slots_[i].profile_id == profile_id) return prof_slots_[i].uuid;
    return Uuid::zero();
}

DiagStatus UserDataStore::profile_create(const char* name, const char* lang,
                                          uint16_t* id_out)
{
    if (prof_count_ >= PROF_MAX_PROFILES)
        return DiagStatus::error(DiagCode::STORAGE_FULL);

    uint16_t new_id = 1u;
    for (;;) {
        bool used = false;
        for (uint8_t i = 0; i < prof_count_; ++i)
            if (prof_slots_[i].profile_id == new_id) { used = true; break; }
        if (!used) break;
        if (new_id == 0xFFFEu) return DiagStatus::error(DiagCode::STORAGE_FULL);
        ++new_id;
    }

    ProfileSlot& s = prof_slots_[prof_count_];
    memset(&s, 0, sizeof(s));
    s.profile_id = new_id;
    s.uuid       = Uuid::generate();
    if (name) {
        size_t n = strlen(name);
        if (n >= PROF_NAME_MAX) n = PROF_NAME_MAX - 1u;
        memcpy(s.name, name, n);
    }
    if (lang) {
        size_t n = strlen(lang);
        if (n >= PROF_LANG_MAX) n = PROF_LANG_MAX - 1u;
        memcpy(s.lang, lang, n);
    }

    ++prof_count_;
    DiagStatus st = prof_save_manifest();
    if (!st.ok()) { --prof_count_; return st; }

    prof_append_to_store(s);
    if (id_out) *id_out = new_id;
    return DiagStatus::success();
}

DiagStatus UserDataStore::profile_get(uint16_t profile_id, ProfileRecord* out) const
{
    for (uint8_t i = 0; i < prof_count_; ++i) {
        if (prof_slots_[i].profile_id == profile_id) {
            if (out) {
                out->profile_id = prof_slots_[i].profile_id;
                out->uuid       = prof_slots_[i].uuid;
                memcpy(out->name, prof_slots_[i].name, PROF_NAME_MAX);
                memcpy(out->lang, prof_slots_[i].lang, PROF_LANG_MAX);
                out->flags = prof_slots_[i].flags;
                out->_pad  = 0;
            }
            return DiagStatus::success();
        }
    }
    return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
}

DiagStatus UserDataStore::profile_remove(uint16_t profile_id)
{
    uint8_t found = PROF_MAX_PROFILES;
    for (uint8_t i = 0; i < prof_count_; ++i)
        if (prof_slots_[i].profile_id == profile_id) { found = i; break; }
    if (found == PROF_MAX_PROFILES) return DiagStatus::success();

    for (uint8_t i = found; i + 1u < prof_count_; ++i)
        prof_slots_[i] = prof_slots_[i + 1u];
    memset(&prof_slots_[prof_count_ - 1u], 0, sizeof(ProfileSlot));
    --prof_count_;

    if (prof_active_id_ == profile_id) prof_active_id_ = PROF_ID_NONE;
    return prof_save_manifest();
}

DiagStatus UserDataStore::profile_set_active(uint16_t profile_id)
{
    if (profile_id != PROF_ID_NONE) {
        bool found = false;
        for (uint8_t i = 0; i < prof_count_; ++i)
            if (prof_slots_[i].profile_id == profile_id) { found = true; break; }
        if (!found) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    prof_active_id_ = profile_id;
    return prof_save_manifest();
}

void UserDataStore::profile_begin_guest()
{
    prof_pre_guest_ = prof_active_id_;
    prof_active_id_ = PROF_ID_GUEST;
}

void UserDataStore::profile_end_guest()
{
    prof_active_id_ = prof_pre_guest_;
    prof_pre_guest_ = PROF_ID_NONE;
}

DiagStatus UserDataStore::profile_wipe_all()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    prof_count_     = 0;
    prof_active_id_ = PROF_ID_NONE;
    prof_pre_guest_ = PROF_ID_NONE;
    memset(prof_slots_, 0, sizeof(prof_slots_));
    fat_delete_file(PROF_MANIFEST_PATH);
    return DiagStatus::success();
}

// ===========================================================================
// Settings
// ===========================================================================

SystemSettings UserDataStore::settings_defaults()
{
    SystemSettings s = {};
    s.language[0]      = 'e';
    s.language[1]      = 'n';
    s.video_mode       = SYS_VIDEO_AUTO;
    s.network_enabled  = 1u;
    s.guest_allowed    = 1u;
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
    if (act != sizeof(SystemSettings)) {
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }
    memcpy(&settings_, &tmp, sizeof(settings_));
    return DiagStatus::success();
}

DiagStatus UserDataStore::settings_save()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
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

void UserDataStore::save_make_prof_dir(char* buf, size_t sz, uint16_t pid)
{
    snprintf(buf, sz, "1:/saves/%04x", static_cast<unsigned>(pid));
}

void UserDataStore::save_make_blob_path(char* buf, size_t sz,
                                         uint16_t pid, uint16_t bid)
{
    snprintf(buf, sz, "1:/saves/%04x/%04x.sav",
             static_cast<unsigned>(pid), static_cast<unsigned>(bid));
}

void UserDataStore::save_make_idx_path(char* buf, size_t sz, uint16_t pid)
{
    snprintf(buf, sz, "1:/saves/%04x/_idx.bin", static_cast<unsigned>(pid));
}

// ===========================================================================
// Saves — registry (index file per profile)
// ===========================================================================

static constexpr uint16_t REG_ENTRY_SIZE  = 6u;
static constexpr uint16_t REG_MAX_ENTRIES =
    static_cast<uint16_t>((SAVE_MAX_BLOB - 2u) / REG_ENTRY_SIZE);

void UserDataStore::save_update_registry(uint16_t pid, uint16_t bid,
                                          uint16_t flags, uint16_t size)
{
    char idx[48];
    save_make_idx_path(idx, sizeof(idx), pid);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
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
    fat_write_file(idx, buf, static_cast<size_t>(2u + n * REG_ENTRY_SIZE));
}

void UserDataStore::save_remove_from_registry(uint16_t pid, uint16_t bid)
{
    char idx[48];
    save_make_idx_path(idx, sizeof(idx), pid);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
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

uint8_t UserDataStore::save_list(uint16_t pid, uint8_t /*kind*/,
                                  BlobInfo* out, uint8_t max) const
{
    if (!initialized_) return 0u;
    char idx[48];
    save_make_idx_path(idx, sizeof(idx), pid);

    uint8_t  buf[SAVE_MAX_BLOB];
    size_t   actual = 0;
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

DiagStatus UserDataStore::save_read(uint16_t pid, uint16_t bid,
                                     uint32_t offset, uint8_t* buf,
                                     uint16_t len) const
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char path[48];
    save_make_blob_path(path, sizeof(path), pid, bid);

    uint8_t  val[SAVE_MAX_BLOB];
    size_t   val_len = 0;
    if (!fat_read_file(path, val, sizeof(val), &val_len))
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    if (offset >= val_len) return DiagStatus::success();
    uint16_t avail = static_cast<uint16_t>(val_len - static_cast<uint16_t>(offset));
    if (len > avail) len = avail;
    memcpy(buf, val + offset, len);
    return DiagStatus::success();
}

DiagStatus UserDataStore::save_write_begin(uint16_t pid, uint16_t bid,
                                            uint16_t total_len, uint16_t flags,
                                            uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (total_len > SAVE_MAX_BLOB) return DiagStatus::error(DiagCode::STORAGE_FULL);

    for (int i = 0; i < SAVE_WRITE_HANDLES; ++i) {
        if (!save_handles_[i].in_use) {
            save_handles_[i].in_use        = true;
            save_handles_[i].profile_id    = pid;
            save_handles_[i].blob_id       = bid;
            save_handles_[i].total_len     = total_len;
            save_handles_[i].flags         = flags;
            save_handles_[i].bytes_written = 0u;
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
    char dir[32];
    save_make_prof_dir(dir, sizeof(dir), h.profile_id);
    fat_ensure_dir("1:/saves");
    fat_ensure_dir(dir);

    char path[48];
    save_make_blob_path(path, sizeof(path), h.profile_id, h.blob_id);
    if (!fat_write_file(path, h.staging, h.total_len)) {
        h.in_use = false;
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    }

    save_update_registry(h.profile_id, h.blob_id, h.flags, h.total_len);

    if (store_ok_) {
        Uuid puuid = profile_uuid_for(h.profile_id);
        char slot_str[8];
        snprintf(slot_str, sizeof(slot_str), "%u",
                 static_cast<unsigned>(h.blob_id));
        StoreRecord rec = {};
        StoreKey key = StoreKey::for_save(puuid, slot_str);
        memcpy(rec.key, key.data, 32);
        rec.event_type = static_cast<uint8_t>(StoreEntryType::SAVE);
        uint16_t bid_le  = h.blob_id;
        uint16_t size_le = h.total_len;
        uint16_t fl_le   = h.flags;
        memcpy(rec.payload + 0, &bid_le,  2);
        memcpy(rec.payload + 2, &size_le, 2);
        memcpy(rec.payload + 4, &fl_le,   2);
        rec.payload_len = 6u;
        store_.append(rec);
    }

    h.in_use = false;
    return DiagStatus::success();
}

DiagStatus UserDataStore::save_delete(uint16_t pid, uint16_t bid)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    char path[48];
    save_make_blob_path(path, sizeof(path), pid, bid);
    fat_delete_file(path);
    save_remove_from_registry(pid, bid);
    return DiagStatus::success();
}

// ===========================================================================
// Stats — helpers
// ===========================================================================

Uuid UserDataStore::stat_profile_uuid(uint16_t profile_id) const
{
    Uuid u = profile_uuid_for(profile_id);
    if (!u.is_zero()) return u;
    // Synthesize a deterministic UUID from the profile_id for unregistered profiles.
    u = Uuid::zero();
    u.bytes[0] = static_cast<uint8_t>(profile_id & 0xFFu);
    u.bytes[1] = static_cast<uint8_t>((profile_id >> 8) & 0xFFu);
    return u;
}

void UserDataStore::stat_id_str(char* buf, size_t sz, uint16_t id)
{
    snprintf(buf, sz, "%u", static_cast<unsigned>(id));
}

void UserDataStore::stats_make_path(char* out, size_t sz, uint16_t pid,
                                     const char* payload_id, char type_char,
                                     uint16_t id)
{
    char pay[STATS_PAYLOAD_KEY_MAX + 1];
    strncpy(pay, payload_id ? payload_id : "_", STATS_PAYLOAD_KEY_MAX);
    pay[STATS_PAYLOAD_KEY_MAX] = '\0';
    if (pay[0] == '\0') { pay[0] = '_'; pay[1] = '\0'; }
    snprintf(out, sz, "1:/saves/%04x/st_%s_%c_%04x.bin",
             static_cast<unsigned>(pid), pay, type_char,
             static_cast<unsigned>(id));
}

// ===========================================================================
// Stats — public API
// ===========================================================================

DiagStatus UserDataStore::stat_get(uint16_t profile_id, const char* payload_id,
                                    uint16_t stat_id, int32_t* out) const
{
    if (out) *out = 0;
    if (!initialized_ || !out) return DiagStatus::success();
    if (!store_ok_) return DiagStatus::success();

    Uuid pu = stat_profile_uuid(profile_id);
    char ids[8];
    stat_id_str(ids, sizeof(ids), stat_id);
    StoreKey key = StoreKey::for_stat(pu, payload_id ? payload_id : "", ids);

    StoreRecord rec;
    if (!store_.find_latest(key, rec).ok()) return DiagStatus::success();
    if (rec.payload_len < sizeof(int32_t)) return DiagStatus::success();

    int32_t val = 0;
    memcpy(&val, rec.payload, sizeof(val));
    *out = val;
    return DiagStatus::success();
}

DiagStatus UserDataStore::stat_set(uint16_t profile_id, const char* payload_id,
                                    uint16_t stat_id, int32_t value, uint8_t op)
{
    if (!initialized_ || !store_ok_)
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    Uuid pu = stat_profile_uuid(profile_id);
    char ids[8];
    stat_id_str(ids, sizeof(ids), stat_id);
    StoreKey key = StoreKey::for_stat(pu, payload_id ? payload_id : "", ids);

    int32_t final_val = value;
    if (op == 1u || op == 2u) {
        int32_t cur = 0;
        StoreRecord prev;
        if (store_.find_latest(key, prev).ok() && prev.payload_len >= sizeof(int32_t))
            memcpy(&cur, prev.payload, sizeof(cur));
        if (op == 1u) final_val = cur + value;
        else          final_val = (value > cur) ? value : cur;
    }

    StoreRecord rec = {};
    memcpy(rec.key, key.data, 32);
    rec.event_type  = static_cast<uint8_t>(StoreEntryType::STAT);
    rec.payload_len = sizeof(int32_t);
    memcpy(rec.payload, &final_val, sizeof(final_val));
    return store_.append(rec);
}

DiagStatus UserDataStore::ach_unlock(uint16_t profile_id, const char* payload_id,
                                      uint16_t ach_id)
{
    if (!initialized_ || !store_ok_)
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    Uuid pu = stat_profile_uuid(profile_id);
    char ids[8];
    stat_id_str(ids, sizeof(ids), ach_id);
    StoreKey key = StoreKey::for_achievement(pu, payload_id ? payload_id : "", ids);

    StoreRecord prev;
    if (store_.find_latest(key, prev).ok() &&
        prev.payload_len >= 1 && prev.payload[0] == 1u)
        return DiagStatus::success();

    StoreRecord rec = {};
    memcpy(rec.key, key.data, 32);
    rec.event_type  = static_cast<uint8_t>(StoreEntryType::ACHIEVEMENT);
    rec.payload_len = 1u;
    rec.payload[0]  = 1u;
    return store_.append(rec);
}

DiagStatus UserDataStore::ach_get(uint16_t profile_id, const char* payload_id,
                                   uint16_t ach_id, bool* unlocked) const
{
    if (!unlocked) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    *unlocked = false;
    if (!initialized_ || !store_ok_) return DiagStatus::success();

    Uuid pu = stat_profile_uuid(profile_id);
    char ids[8];
    stat_id_str(ids, sizeof(ids), ach_id);
    StoreKey key = StoreKey::for_achievement(pu, payload_id ? payload_id : "", ids);

    StoreRecord rec;
    if (!store_.find_latest(key, rec).ok()) return DiagStatus::success();
    *unlocked = (rec.payload_len >= 1 && rec.payload[0] == 1u);
    return DiagStatus::success();
}

DiagStatus UserDataStore::leader_begin(uint16_t profile_id, const char* payload_id,
                                        uint16_t lb_id,
                                        uint8_t* token_out, uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    int slot = -1;
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i)
        if (!stats_tokens_[i].in_use) { slot = i; break; }
    if (slot < 0) return DiagStatus::error(DiagCode::STORAGE_FULL);

    TokenSlot& ts = stats_tokens_[slot];
    ts.in_use     = true;
    ts.profile_id = profile_id;
    ts.lb_id      = lb_id;
    strncpy(ts.payload_id, payload_id ? payload_id : "",
            STATS_PAYLOAD_KEY_MAX);
    ts.payload_id[STATS_PAYLOAD_KEY_MAX] = '\0';

#ifdef JLPICART_HOST_TEST
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i)
        ts.token[i] = static_cast<uint8_t>((stats_token_counter_ >> (i % 4u) * 8u) + i);
    ++stats_token_counter_;
#else
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i) {
        ts.token[i] = static_cast<uint8_t>(rosc_hw->randombit & 0xFFu);
        for (uint8_t b = 0; b < 7u; ++b)
            ts.token[i] = static_cast<uint8_t>((ts.token[i] << 1u)
                                                | (rosc_hw->randombit & 1u));
    }
#endif

    if (token_out)  memcpy(token_out, ts.token, STATS_TOKEN_LEN);
    if (handle_out) *handle_out = static_cast<uint8_t>(slot);
    return DiagStatus::success();
}

DiagStatus UserDataStore::leader_submit(uint8_t handle, uint32_t score,
                                         uint8_t /*proof_kind*/,
                                         const uint8_t* /*proof_buf*/,
                                         uint16_t /*proof_len*/,
                                         DeviceIdentity* dik)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= STATS_TOKEN_SLOTS)
        return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    TokenSlot& ts = stats_tokens_[handle];
    if (!ts.in_use) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    LeaderEntry entry = {};
    entry.score = score;
#ifdef JLPICART_HOST_TEST
    entry.timestamp = 0u;
#else
    entry.timestamp = static_cast<uint32_t>(time_us_64() / 1000000u);
#endif

    if (dik != nullptr && dik->initialized()) {
        uint8_t msg[28];
        memcpy(msg + 0,  &entry.score,     4u);
        memcpy(msg + 4,  &entry.timestamp, 4u);
        memcpy(msg + 8,  ts.token,         STATS_TOKEN_LEN);
        memcpy(msg + 24, &ts.profile_id,   2u);
        memcpy(msg + 26, &ts.lb_id,        2u);
        dik->sign(msg, sizeof(msg), entry.signature);
    }

    char path[128];
    stats_make_path(path, sizeof(path), ts.profile_id, ts.payload_id, 'l', ts.lb_id);

    char dir[32];
    snprintf(dir, sizeof(dir), "1:/saves/%04x", static_cast<unsigned>(ts.profile_id));
    fat_ensure_dir("1:/saves");
    fat_ensure_dir(dir);

    const uint16_t cap_profile_id = ts.profile_id;
    const uint16_t cap_lb_id      = ts.lb_id;
    char cap_payload_id[STATS_PAYLOAD_KEY_MAX + 1];
    memcpy(cap_payload_id, ts.payload_id, sizeof(ts.payload_id));
    ts.in_use = false;

    if (!fat_write_file(path, &entry, sizeof(entry)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);

    if (store_ok_) {
        Uuid pu = stat_profile_uuid(cap_profile_id);
        char ids[8];
        stat_id_str(ids, sizeof(ids), cap_lb_id);
        StoreKey skey = StoreKey::for_leaderboard(pu, cap_payload_id, ids);
        StoreRecord srec = {};
        memcpy(srec.key, skey.data, 32);
        srec.event_type  = static_cast<uint8_t>(StoreEntryType::LEADER_ENTRY);
        srec.payload_len = 8u;
        memcpy(srec.payload + 0, &entry.score,     4u);
        memcpy(srec.payload + 4, &entry.timestamp, 4u);
        store_.append(srec);
    }

    return DiagStatus::success();
}

// ===========================================================================
// Factory reset
// ===========================================================================

DiagStatus UserDataStore::wipe_user_data()
{
    return profile_wipe_all();
}

DiagStatus UserDataStore::full_wipe()
{
    DiagStatus s = wipe_user_data();
    fat_delete_file(SETTINGS_PATH);
    settings_ = settings_defaults();
    return s;
}
