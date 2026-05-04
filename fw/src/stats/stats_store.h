#pragma once
// stats_store.h — Per-user, per-payload stat/achievement/leaderboard storage.
//
// Stats and achievements are stored in the append-only Store (bind_store()
// must be called before init() for these to work).
//
// Leaderboard entries exceed the 64-byte Store inline limit and are stored
// as FAT files: 1:/saves/{prof_4hex}/st_{pay32}_l_{id_4hex}.bin (72 bytes).
// A compact Store record (score+timestamp, 8 bytes) is also appended.
//
// FatVolume must be mounted before calling init().
// Thread safety: NOT thread-safe.

#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

class ProfileStore;
class DeviceIdentity;
class Store;

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

class StatsStore {
public:
    void bind_store(Store& s) { store_ = &s; }

    void init(ProfileStore& ps);
    bool initialized() const { return initialized_; }

    DiagStatus stat_get(uint16_t profile_id, const char* payload_id,
                        uint16_t stat_id, int32_t* out_value) const;

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

private:
    struct TokenSlot {
        bool     in_use;
        uint8_t  token[STATS_TOKEN_LEN];
        uint16_t profile_id;
        char     payload_id[STATS_PAYLOAD_KEY_MAX + 1];
        uint16_t lb_id;
    };

    Store*         store_        = nullptr;
    ProfileStore*  ps_           = nullptr;
    bool           initialized_  = false;
    TokenSlot      tokens_[STATS_TOKEN_SLOTS] = {};
    uint32_t       token_counter_ = 0u;

    // Build FAT path for leaderboard: 1:/saves/{prof_4hex}/st_{pay32}_l_{id_4hex}.bin
    static void make_path(char* out, size_t sz,
                           uint16_t profile_id, const char* payload_id,
                           char type_char, uint16_t id);
};
