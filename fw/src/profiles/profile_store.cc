// profile_store.cc — ProfileStore implementation.

#include "profiles/profile_store.h"
#include <cstring>
#include <cstdint>

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::init(FlashDevice& dev,
                               uint32_t part_ofs, uint32_t part_size)
{
    DiagStatus s = kv_.init(dev, part_ofs, part_size);
    if (!s.ok()) return s;

    // Load cached active_id_ (absent = PROF_ID_NONE, which is fine).
    uint8_t  buf[2] = {};
    uint16_t len    = 0;
    DiagStatus sa = kv_.get(KV_PROF_ACTIVE, buf, &len, sizeof(buf));
    if (sa.ok() && len == 2) {
        active_id_ = static_cast<uint16_t>(buf[0] | (buf[1] << 8u));
    } else {
        active_id_ = PROF_ID_NONE;
    }

    initialized_ = true;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::load_index(ProfileIndex& idx) const
{
    memset(&idx, 0, sizeof(idx));
    uint8_t  buf[sizeof(ProfileIndex)];
    uint16_t len = 0;
    DiagStatus s = kv_.get(KV_PROF_INDEX,
                            buf, &len, static_cast<uint16_t>(sizeof(buf)));
    if (!s.ok()) {
        // Absent key = empty index (normal on first boot).
        return DiagStatus::success();
    }
    if (len < sizeof(ProfileIndex)) {
        // Truncated record — treat as empty to avoid undefined behaviour.
        return DiagStatus::success();
    }
    memcpy(&idx, buf, sizeof(ProfileIndex));
    return DiagStatus::success();
}

DiagStatus ProfileStore::save_index(const ProfileIndex& idx)
{
    return kv_.put(KV_PROF_INDEX,
                   reinterpret_cast<const uint8_t*>(&idx),
                   static_cast<uint16_t>(sizeof(idx)));
}

DiagStatus ProfileStore::save_active(uint16_t id)
{
    uint8_t buf[2];
    buf[0] = static_cast<uint8_t>(id & 0xFFu);
    buf[1] = static_cast<uint8_t>(id >> 8u);
    DiagStatus s = kv_.put(KV_PROF_ACTIVE, buf, 2u);
    if (s.ok()) active_id_ = id;
    return s;
}

// ---------------------------------------------------------------------------
// list / count
// ---------------------------------------------------------------------------

uint8_t ProfileStore::list(ProfileRecord* buf, uint8_t max) const
{
    ProfileIndex idx;
    load_index(idx);
    uint8_t n = (idx.count < max) ? idx.count : max;
    if (n > 0) memcpy(buf, idx.entries, n * sizeof(ProfileRecord));
    return n;
}

uint8_t ProfileStore::count() const
{
    ProfileIndex idx;
    load_index(idx);
    return idx.count;
}

// ---------------------------------------------------------------------------
// create
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::create(const char* name, const char* lang,
                                 uint16_t* id_out)
{
    ProfileIndex idx;
    DiagStatus s = load_index(idx);
    if (!s.ok()) return s;

    if (idx.count >= PROF_MAX_PROFILES) {
        return DiagStatus::error(DiagCode::STORAGE_FULL);
    }

    // Find the smallest uint16 ≥ 1 not already used.
    uint16_t new_id = 1u;
    for (;;) {
        bool used = false;
        for (uint8_t i = 0; i < idx.count; ++i) {
            if (idx.entries[i].profile_id == new_id) { used = true; break; }
        }
        if (!used) break;
        if (new_id == 0xFFFEu) return DiagStatus::error(DiagCode::STORAGE_FULL);
        ++new_id;
    }

    ProfileRecord& rec = idx.entries[idx.count];
    memset(&rec, 0, sizeof(rec));
    rec.profile_id = new_id;

    size_t name_len = name ? strlen(name) : 0u;
    if (name_len >= PROF_NAME_MAX) name_len = PROF_NAME_MAX - 1u;
    memcpy(rec.name, name, name_len);

    size_t lang_len = lang ? strlen(lang) : 0u;
    if (lang_len >= PROF_LANG_MAX) lang_len = PROF_LANG_MAX - 1u;
    memcpy(rec.lang, lang, lang_len);

    ++idx.count;

    s = save_index(idx);
    if (!s.ok()) return s;

    if (id_out) *id_out = new_id;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// get
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::get(uint16_t profile_id, ProfileRecord* out) const
{
    ProfileIndex idx;
    DiagStatus s = load_index(idx);
    if (!s.ok()) return s;

    for (uint8_t i = 0; i < idx.count; ++i) {
        if (idx.entries[i].profile_id == profile_id) {
            if (out) *out = idx.entries[i];
            return DiagStatus::success();
        }
    }
    return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
}

// ---------------------------------------------------------------------------
// remove
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::remove(uint16_t profile_id)
{
    ProfileIndex idx;
    DiagStatus s = load_index(idx);
    if (!s.ok()) return s;

    uint8_t found = PROF_MAX_PROFILES;  // sentinel
    for (uint8_t i = 0; i < idx.count; ++i) {
        if (idx.entries[i].profile_id == profile_id) { found = i; break; }
    }

    if (found == PROF_MAX_PROFILES) return DiagStatus::success(); // not found, OK

    // Shift entries left to fill the gap.
    for (uint8_t i = found; i + 1u < idx.count; ++i) {
        idx.entries[i] = idx.entries[i + 1u];
    }
    memset(&idx.entries[idx.count - 1u], 0, sizeof(ProfileRecord));
    --idx.count;

    s = save_index(idx);
    if (!s.ok()) return s;

    if (active_id_ == profile_id) {
        save_active(PROF_ID_NONE);  // best-effort; ignore error
    }

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// begin_guest / end_guest
// ---------------------------------------------------------------------------

void ProfileStore::begin_guest()
{
    pre_guest_id_ = active_id_;
    active_id_    = PROF_ID_GUEST;
    // No flash write: guest sessions are ephemeral.
}

void ProfileStore::end_guest()
{
    active_id_    = pre_guest_id_;
    pre_guest_id_ = PROF_ID_NONE;
}

// ---------------------------------------------------------------------------
// wipe_all
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::wipe_all()
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    // Delete all "prof.*" keys from the underlying KV.
    DiagStatus s = kv_.del_prefix("prof.");
    // Reset in-memory state regardless of storage result.
    active_id_ = PROF_ID_NONE;
    return s;
}

// ---------------------------------------------------------------------------
// set_active
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::set_active(uint16_t profile_id)
{
    // PROF_ID_NONE always accepted (clears selection).
    if (profile_id != PROF_ID_NONE) {
        ProfileRecord dummy;
        DiagStatus s = get(profile_id, &dummy);
        if (!s.ok()) return s;  // STORAGE_NOT_FOUND
    }
    return save_active(profile_id);
}
