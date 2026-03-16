#pragma once
// profile_format.h — Persistent profile records for JLPiCart.
//
// Profiles are stored in PROFILES_KV (flash_layout.h) via a KvStore.
// All on-flash data uses these packed structs.
//
// KV layout (within PROFILES_KV):
//   KV_PROF_INDEX  → ProfileIndex (up to PROF_MAX_PROFILES entries)
//   KV_PROF_ACTIVE → uint16_t profile_id (active profile; PROF_ID_NONE = none)
//
// Spec reference: spec.md §4.5, §6.2 (jlpicart.profile.v1)

#include <cstdint>

static constexpr uint8_t  PROF_MAX_PROFILES = 8u;
static constexpr uint8_t  PROF_NAME_MAX     = 32u;  // display name, null-terminated
static constexpr uint8_t  PROF_LANG_MAX     = 8u;   // language tag, e.g. "en"

static constexpr uint16_t PROF_ID_NONE  = 0x0000u;  // no active profile
static constexpr uint16_t PROF_ID_GUEST = 0xFFFFu;  // ephemeral guest session

// KV keys (in PROFILES_KV store)
static constexpr const char* KV_PROF_INDEX  = "prof.index";   // ProfileIndex bytes
static constexpr const char* KV_PROF_ACTIVE = "prof.active";  // uint16_t, little-endian

#pragma pack(push, 1)

// One profile slot.
// 44 bytes: 2 + 32 + 8 + 1 + 1
struct ProfileRecord {
    uint16_t profile_id;          // 1..0xFFFE  (PROF_ID_NONE and PROF_ID_GUEST reserved)
    char     name[PROF_NAME_MAX]; // display name, null-terminated
    char     lang[PROF_LANG_MAX]; // IETF language tag, null-terminated
    uint8_t  flags;               // reserved, MUST be 0
    uint8_t  _pad;                // padding to even size
};
static_assert(sizeof(ProfileRecord) == 44, "ProfileRecord must be 44 bytes");

// All profiles on the device, as stored in KV_PROF_INDEX.
// 2 + 8×44 = 354 bytes ≤ KV_MAX_VAL_LEN (512).
struct ProfileIndex {
    uint8_t       count;                        // 0..PROF_MAX_PROFILES
    uint8_t       _pad;
    ProfileRecord entries[PROF_MAX_PROFILES];
};
static_assert(sizeof(ProfileIndex) == 354, "ProfileIndex must be 354 bytes");

#pragma pack(pop)
