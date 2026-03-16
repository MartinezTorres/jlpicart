#pragma once
// stats_store.h — Per-user, per-payload stat/achievement/leaderboard persistence (Stage 21).
//
// StatsStore backs the UserStats service (0x04) using the same KvStore partition as
// SaveStore (SAVES_KV), but under the "st.*" key namespace.
//
// Key scheme (all IDs in lowercase 4-hex, payload_id truncated to STATS_PAYLOAD_KEY_MAX):
//   stat:        "st.<profile_4hex>.<payload>.<s>.<stat_4hex>"  → little-endian int32_t
//   achievement: "st.<profile_4hex>.<payload>.<a>.<ach_4hex>"   → uint8_t (0=locked, 1=unlocked)
//   leaderboard: "st.<profile_4hex>.<payload>.<l>.<lb_4hex>"    → {u32 score, u32 ts} (8 bytes)
//
// Max key length: 3+4+1+32+1+1+1+4 = 47 ≤ KV_MAX_KEY_LEN(48).
//
// Stat operations: op 0 = set, op 1 = add, op 2 = max.
//
// Leaderboard write uses a two-phase begin/submit to prevent replay:
//   1. leader_begin() allocates a token slot and returns a nonce token + handle.
//   2. leader_submit() validates the handle, writes the entry, frees the slot.
//
// Spec reference: §7.2 "UserStats service (0x04)".

#include "diag/diag.h"
#include <cstdint>
#include <cstddef>

class KvStore;
class ProfileStore;

static constexpr int     STATS_TOKEN_SLOTS      = 4;   // max concurrent leaderboard runs
static constexpr uint8_t STATS_TOKEN_LEN        = 16u; // nonce token size in bytes
static constexpr size_t  STATS_PAYLOAD_KEY_MAX  = 32u; // max payload_id chars in a KV key

struct LeaderEntry {
    uint32_t score;
    uint32_t timestamp;
    // Stage 22 will add a 64-byte signature field here.
};
static_assert(sizeof(LeaderEntry) == 8, "LeaderEntry must be 8 bytes");

class StatsStore {
public:
    void init(KvStore& kv, ProfileStore& ps);
    bool initialized() const { return initialized_; }

    // Return the stat value for (profile, payload, stat_id).
    // If the key does not exist, *out_value is set to 0 and OK is returned.
    DiagStatus stat_get(uint16_t profile_id, const char* payload_id,
                        uint16_t stat_id, int32_t* out_value) const;

    // Write the stat value with the given operation.
    // op 0=set (overwrite), op 1=add (read-modify-write), op 2=max (keep larger).
    DiagStatus stat_set(uint16_t profile_id, const char* payload_id,
                        uint16_t stat_id, int32_t value, uint8_t op);

    // Unlock an achievement.  Idempotent: already-unlocked is not an error.
    DiagStatus ach_unlock(uint16_t profile_id, const char* payload_id,
                          uint16_t ach_id);

    // Allocate a leaderboard token slot.  Fills token_out[STATS_TOKEN_LEN] with a nonce.
    // Sets *handle_out to the slot index (0..STATS_TOKEN_SLOTS-1).
    // Returns STORAGE_FULL if no slot is available.
    DiagStatus leader_begin(uint16_t profile_id, const char* payload_id,
                             uint16_t lb_id,
                             uint8_t* token_out, uint8_t* handle_out);

    // Commit a leaderboard entry.  Validates the handle slot is in use.
    // proof_buf/proof_len are reserved for Stage 22 signing; ignored for now.
    // Frees the token slot on success.
    DiagStatus leader_submit(uint8_t handle, uint32_t score,
                              uint8_t proof_kind,
                              const uint8_t* proof_buf, uint16_t proof_len);

private:
    struct TokenSlot {
        bool     in_use;
        uint8_t  token[STATS_TOKEN_LEN];
        uint16_t profile_id;
        char     payload_id[STATS_PAYLOAD_KEY_MAX + 1];
        uint16_t lb_id;
    };

    KvStore*       kv_           = nullptr;
    ProfileStore*  ps_           = nullptr;
    bool           initialized_  = false;
    TokenSlot      tokens_[STATS_TOKEN_SLOTS] = {};
    uint32_t       token_counter_ = 0u; // deterministic counter for host tests

    // Build a KV key: "st.<profile_4hex>.<payload_truncated>.<type_char>.<id_4hex>"
    static void make_key(char* out, size_t out_size,
                          uint16_t profile_id, const char* payload_id,
                          char type_char, uint16_t id);
};
