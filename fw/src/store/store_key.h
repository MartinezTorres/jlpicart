#pragma once
// store_key.h — 32-byte key identifying what an event record modifies.
//
// Layout: [key_type:1][scope:31]
//
// String IDs that exceed the scope budget are hashed with FNV-1a 32-bit.
// Collision probability across typical game libraries (<=10k distinct IDs)
// is negligible (<1e-4).

#include "store/uuid.h"
#include <cstdint>
#include <cstring>

enum class StoreKeyType : uint8_t {
    SAVE         = 0x01,  // scope: [profile:16][fnv32(slot_str):4][pad:11]
    ACHIEVEMENT  = 0x02,  // scope: [profile:16][fnv32(payload_id):4][fnv32(ach_id):4][pad:7]
    STAT         = 0x03,  // scope: [profile:16][fnv32(payload_id):4][fnv32(stat_name):4][pad:7]
    LEADER_ENTRY = 0x04,  // scope: [profile:16][fnv32(payload_id):4][fnv32(board_id):4][pad:7]
    PROFILE      = 0x05,  // scope: [profile:16][pad:15]
};

struct StoreKey {
    uint8_t data[32] = {};

    StoreKeyType type() const { return static_cast<StoreKeyType>(data[0]); }
    bool operator==(const StoreKey& o) const { return memcmp(data, o.data, 32) == 0; }
    bool operator!=(const StoreKey& o) const { return !(*this == o); }

    static StoreKey for_save(const Uuid& profile, const char* slot_str);
    static StoreKey for_achievement(const Uuid& profile, const char* payload_id,
                                     const char* ach_id);
    static StoreKey for_stat(const Uuid& profile, const char* payload_id,
                              const char* stat_name);
    static StoreKey for_leaderboard(const Uuid& profile, const char* payload_id,
                                     const char* board_id);
    static StoreKey for_profile(const Uuid& profile);

private:
    static uint32_t fnv32(const char* s) {
        uint32_t h = 2166136261u;
        for (; *s; ++s) { h ^= static_cast<uint8_t>(*s); h *= 16777619u; }
        return h;
    }
    static void put_u32le(uint8_t* dst, uint32_t v) {
        dst[0] = v & 0xFFu; dst[1] = (v >> 8) & 0xFFu;
        dst[2] = (v >> 16) & 0xFFu; dst[3] = (v >> 24) & 0xFFu;
    }
};

inline StoreKey StoreKey::for_save(const Uuid& p, const char* slot_str)
{
    StoreKey k;
    k.data[0] = static_cast<uint8_t>(StoreKeyType::SAVE);
    memcpy(k.data + 1, p.bytes, 16);
    put_u32le(k.data + 17, fnv32(slot_str));
    return k;
}

inline StoreKey StoreKey::for_achievement(const Uuid& p, const char* pid, const char* aid)
{
    StoreKey k;
    k.data[0] = static_cast<uint8_t>(StoreKeyType::ACHIEVEMENT);
    memcpy(k.data + 1, p.bytes, 16);
    put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(aid));
    return k;
}

inline StoreKey StoreKey::for_stat(const Uuid& p, const char* pid, const char* sname)
{
    StoreKey k;
    k.data[0] = static_cast<uint8_t>(StoreKeyType::STAT);
    memcpy(k.data + 1, p.bytes, 16);
    put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(sname));
    return k;
}

inline StoreKey StoreKey::for_leaderboard(const Uuid& p, const char* pid, const char* bid)
{
    StoreKey k;
    k.data[0] = static_cast<uint8_t>(StoreKeyType::LEADER_ENTRY);
    memcpy(k.data + 1, p.bytes, 16);
    put_u32le(k.data + 17, fnv32(pid));
    put_u32le(k.data + 21, fnv32(bid));
    return k;
}

inline StoreKey StoreKey::for_profile(const Uuid& p)
{
    StoreKey k;
    k.data[0] = static_cast<uint8_t>(StoreKeyType::PROFILE);
    memcpy(k.data + 1, p.bytes, 16);
    return k;
}
