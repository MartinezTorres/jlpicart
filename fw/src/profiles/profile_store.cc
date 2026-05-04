// profile_store.cc — ProfileStore over FAT manifest + optional Store.

#include "profiles/profile_store.h"
#include "storage/fat_util.h"
#include "storage/store.h"
#include "storage/store_record.h"
#include "storage/store_key.h"
#include <cstring>

// ---------------------------------------------------------------------------
// init / persist
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::init()
{
    count_        = 0;
    active_id_    = PROF_ID_NONE;
    pre_guest_id_ = PROF_ID_NONE;
    initialized_  = false;
    memset(slots_, 0, sizeof(slots_));

    ProfileManifest m = {};
    size_t actual = 0;
    if (fat_read_file(MANIFEST_PATH, &m, sizeof(m), &actual)
        && actual == sizeof(ProfileManifest))
    {
        active_id_ = m.active_id;
        uint8_t n = m.count < PROF_MAX_PROFILES ? m.count : PROF_MAX_PROFILES;
        for (uint8_t i = 0; i < n; ++i)
            slots_[i] = m.entries[i];
        count_ = n;
    }

    initialized_ = true;
    return DiagStatus::success();
}

DiagStatus ProfileStore::save_manifest()
{
    ProfileManifest m = {};
    m.active_id = active_id_;
    m.count     = count_;
    for (uint8_t i = 0; i < count_; ++i)
        m.entries[i] = slots_[i];
    fat_ensure_dir("1:/system");
    if (!fat_write_file(MANIFEST_PATH, &m, sizeof(m)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);
    return DiagStatus::success();
}

void ProfileStore::append_to_store(const Slot& slot)
{
    if (!store_) return;

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

    store_->append(rec);  // best-effort
}

// ---------------------------------------------------------------------------
// list / count / uuid_for
// ---------------------------------------------------------------------------

uint8_t ProfileStore::list(ProfileRecord* buf, uint8_t max) const
{
    uint8_t n = count_ < max ? count_ : max;
    for (uint8_t i = 0; i < n; ++i) {
        buf[i].profile_id = slots_[i].profile_id;
        buf[i].uuid       = slots_[i].uuid;
        memcpy(buf[i].name, slots_[i].name, PROF_NAME_MAX);
        memcpy(buf[i].lang, slots_[i].lang, PROF_LANG_MAX);
        buf[i].flags = slots_[i].flags;
        buf[i]._pad  = 0;
    }
    return n;
}

uint8_t ProfileStore::count() const { return count_; }

Uuid ProfileStore::uuid_for(uint16_t profile_id) const
{
    for (uint8_t i = 0; i < count_; ++i) {
        if (slots_[i].profile_id == profile_id)
            return slots_[i].uuid;
    }
    return Uuid::zero();
}

// ---------------------------------------------------------------------------
// create / get / remove / set_active
// ---------------------------------------------------------------------------

DiagStatus ProfileStore::create(const char* name, const char* lang,
                                 uint16_t* id_out)
{
    if (count_ >= PROF_MAX_PROFILES)
        return DiagStatus::error(DiagCode::STORAGE_FULL);

    uint16_t new_id = 1u;
    for (;;) {
        bool used = false;
        for (uint8_t i = 0; i < count_; ++i) {
            if (slots_[i].profile_id == new_id) { used = true; break; }
        }
        if (!used) break;
        if (new_id == 0xFFFEu) return DiagStatus::error(DiagCode::STORAGE_FULL);
        ++new_id;
    }

    Slot& s = slots_[count_];
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

    ++count_;
    DiagStatus st = save_manifest();
    if (!st.ok()) { --count_; return st; }

    append_to_store(s);

    if (id_out) *id_out = new_id;
    return DiagStatus::success();
}

DiagStatus ProfileStore::get(uint16_t profile_id, ProfileRecord* out) const
{
    for (uint8_t i = 0; i < count_; ++i) {
        if (slots_[i].profile_id == profile_id) {
            if (out) {
                out->profile_id = slots_[i].profile_id;
                out->uuid       = slots_[i].uuid;
                memcpy(out->name, slots_[i].name, PROF_NAME_MAX);
                memcpy(out->lang, slots_[i].lang, PROF_LANG_MAX);
                out->flags = slots_[i].flags;
                out->_pad  = 0;
            }
            return DiagStatus::success();
        }
    }
    return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
}

DiagStatus ProfileStore::remove(uint16_t profile_id)
{
    uint8_t found = PROF_MAX_PROFILES;
    for (uint8_t i = 0; i < count_; ++i) {
        if (slots_[i].profile_id == profile_id) { found = i; break; }
    }
    if (found == PROF_MAX_PROFILES) return DiagStatus::success();

    for (uint8_t i = found; i + 1u < count_; ++i)
        slots_[i] = slots_[i + 1u];
    memset(&slots_[count_ - 1u], 0, sizeof(Slot));
    --count_;

    if (active_id_ == profile_id) active_id_ = PROF_ID_NONE;
    return save_manifest();
}

DiagStatus ProfileStore::set_active(uint16_t profile_id)
{
    if (profile_id != PROF_ID_NONE) {
        bool found = false;
        for (uint8_t i = 0; i < count_; ++i) {
            if (slots_[i].profile_id == profile_id) { found = true; break; }
        }
        if (!found) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);
    }
    active_id_ = profile_id;
    return save_manifest();
}

// ---------------------------------------------------------------------------
// guest session
// ---------------------------------------------------------------------------

void ProfileStore::begin_guest()
{
    pre_guest_id_ = active_id_;
    active_id_    = PROF_ID_GUEST;
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
    count_        = 0;
    active_id_    = PROF_ID_NONE;
    pre_guest_id_ = PROF_ID_NONE;
    memset(slots_, 0, sizeof(slots_));
    fat_delete_file(MANIFEST_PATH);
    return DiagStatus::success();
}
