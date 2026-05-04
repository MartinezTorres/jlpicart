#pragma once
// profile_format.h — Persistent profile record format.

#include "storage/uuid.h"
#include <cstdint>
#include <cstddef>

static constexpr uint8_t  PROF_MAX_PROFILES = 8u;
static constexpr uint16_t PROF_ID_NONE      = 0x0000u;
static constexpr uint16_t PROF_ID_GUEST     = 0xFFFFu;
static constexpr uint8_t  PROF_NAME_MAX     = 32u;
static constexpr uint8_t  PROF_LANG_MAX     = 8u;

// API-visible profile record.  Returned by ProfileStore::list/get.
// Not serialized directly — see ProfileManifest for the FAT format.
struct ProfileRecord {
    uint16_t profile_id;            // wire-protocol slot ID (1..0xFFFE)
    Uuid     uuid;                  // persistent global identity for sync
    char     name[PROF_NAME_MAX];
    char     lang[PROF_LANG_MAX];
    uint8_t  flags;
    uint8_t  _pad;
};

// Payload written to Store for PROFILE records.  Fits in STORE_INLINE_MAX=64.
struct ProfilePayload {
    char    name[PROF_NAME_MAX];    // 32 bytes
    char    lang[PROF_LANG_MAX];    // 8 bytes
    uint8_t flags;
    uint8_t _pad[23];
};
static_assert(sizeof(ProfilePayload) == 64u, "ProfilePayload must be 64 bytes");

// FAT manifest: 1:/system/profiles.bin — atomic single-file write.
// Maps uint16_t slot → UUID + name/lang/flags, plus active slot.
struct ProfileManifest {
    uint16_t active_id;
    uint8_t  count;
    uint8_t  _pad;
    struct Entry {
        uint16_t profile_id;
        Uuid     uuid;
        char     name[PROF_NAME_MAX];
        char     lang[PROF_LANG_MAX];
        uint8_t  flags;
        uint8_t  _pad2;
    } entries[PROF_MAX_PROFILES];
};
static_assert(sizeof(ProfileManifest::Entry) == 60u,
              "ProfileManifest::Entry must be 60 bytes");
