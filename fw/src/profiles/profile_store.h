#pragma once
// profile_store.h — CRUD operations for user profiles stored in PROFILES_KV.
//
// ProfileStore wraps a KvStore backed by the PROFILES_KV flash partition.
// All public methods are safe to call after a successful init().
//
// Thread safety: NOT thread-safe (same contract as KvStore).

#include "profiles/profile_format.h"
#include "storage/flash_device.h"
#include "storage/kv_store.h"
#include "diag/diag.h"
#include <cstdint>

class ProfileStore {
public:
    // Initialise storage: mount the PROFILES_KV partition and load the active
    // profile ID.  Succeeds even if the partition is blank (no profiles yet).
    DiagStatus init(FlashDevice& dev, uint32_t part_ofs, uint32_t part_size);

    bool initialized() const { return initialized_; }

    // Fill buf[0..max-1] with up to max ProfileRecords.
    // Returns the actual number of profiles copied (≤ max).
    uint8_t list(ProfileRecord* buf, uint8_t max) const;

    // Number of stored profiles.
    uint8_t count() const;

    // Create a new profile with the given display name and language tag.
    // On success, *id_out receives the assigned profile_id (≥ 1).
    // Returns STORAGE_FULL if PROF_MAX_PROFILES already exist.
    DiagStatus create(const char* name, const char* lang, uint16_t* id_out);

    // Look up a single profile by ID.
    // Returns STORAGE_NOT_FOUND if the ID is not in the index.
    DiagStatus get(uint16_t profile_id, ProfileRecord* out) const;

    // Delete a profile.  If the removed profile was active, resets the active
    // ID to PROF_ID_NONE and persists that change.
    // Returns success even if the ID is not found.
    DiagStatus remove(uint16_t profile_id);

    // Persist the active profile selection.
    // Accepts PROF_ID_NONE (clears selection) or any existing profile_id.
    // Returns API_E_NOT_FOUND if the id is nonzero but not in the index.
    DiagStatus set_active(uint16_t profile_id);

    // Return the cached active profile ID (PROF_ID_NONE if none set).
    uint16_t active() const { return active_id_; }

    // Wipe all profile data: deletes all "prof.*" keys from the underlying KV
    // and resets the active ID to PROF_ID_NONE.  Called by factory reset.
    DiagStatus wipe_all();

private:
    KvStore  kv_;
    uint16_t active_id_  = PROF_ID_NONE;
    bool     initialized_= false;

    DiagStatus load_index(ProfileIndex& idx) const;
    DiagStatus save_index(const ProfileIndex& idx);
    DiagStatus save_active(uint16_t id);
};
