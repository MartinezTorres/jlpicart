#pragma once
// profile_store.h — CRUD operations for user profiles.
//
// Profiles are persisted in:
//   1:/system/profiles.bin  — ProfileManifest (FAT, 484 bytes)
//
// When a Store is bound via bind_store(), each create/update also appends
// a PROFILE StoreRecord so the event is eligible for cloud sync.
//
// Thread safety: NOT thread-safe.

#include "profiles/profile_format.h"
#include "diag/diag.h"
#include <cstdint>

class Store;  // forward declaration — avoids pulling store.h into every TU

class ProfileStore {
public:
    // Bind an append-only Store for sync tracking.  Call before init().
    void bind_store(Store& s) { store_ = &s; }

    DiagStatus init();
    bool initialized() const { return initialized_; }

    uint8_t    list(ProfileRecord* buf, uint8_t max) const;
    uint8_t    count() const;

    // Return the UUID for a slot ID; Uuid::zero() if not found.
    Uuid       uuid_for(uint16_t profile_id) const;

    DiagStatus create(const char* name, const char* lang, uint16_t* id_out);
    DiagStatus get(uint16_t profile_id, ProfileRecord* out) const;
    DiagStatus remove(uint16_t profile_id);
    DiagStatus set_active(uint16_t profile_id);
    uint16_t   active() const { return active_id_; }

    void begin_guest();
    void end_guest();
    bool in_guest_session() const { return active_id_ == PROF_ID_GUEST; }

    DiagStatus wipe_all();

private:
    static constexpr const char* MANIFEST_PATH = "1:/system/profiles.bin";

    using Slot = ProfileManifest::Entry;

    Store*   store_         = nullptr;
    uint8_t  count_         = 0;
    uint16_t active_id_     = PROF_ID_NONE;
    uint16_t pre_guest_id_  = PROF_ID_NONE;
    Slot     slots_[PROF_MAX_PROFILES] = {};
    bool     initialized_   = false;

    DiagStatus save_manifest();
    void       append_to_store(const Slot& slot);
};
