// stats_store.cc — StatsStore implementation (Stage 21).

#include "stats/stats_store.h"
#include "storage/kv_store.h"
#include <cstring>
#include <cstdio>

#ifndef JLPICART_HOST_TEST
#include "hardware/structs/rosc.h"
#endif

// ---------------------------------------------------------------------------
// make_key
// ---------------------------------------------------------------------------

void StatsStore::make_key(char* out, size_t out_size,
                           uint16_t profile_id, const char* payload_id,
                           char type_char, uint16_t id)
{
    // Truncate payload_id to STATS_PAYLOAD_KEY_MAX chars.
    char pay[STATS_PAYLOAD_KEY_MAX + 1];
    strncpy(pay, payload_id ? payload_id : "", STATS_PAYLOAD_KEY_MAX);
    pay[STATS_PAYLOAD_KEY_MAX] = '\0';

    // If payload_id is empty, use "_" as a stand-in to keep the key well-formed.
    if (pay[0] == '\0') {
        pay[0] = '_';
        pay[1] = '\0';
    }

    snprintf(out, out_size, "st.%04x.%s.%c.%04x",
             static_cast<unsigned>(profile_id),
             pay,
             type_char,
             static_cast<unsigned>(id));
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void StatsStore::init(KvStore& kv, ProfileStore& ps)
{
    kv_          = &kv;
    ps_          = &ps;
    initialized_ = true;
    token_counter_ = 0u;
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i) {
        tokens_[i].in_use = false;
    }
}

// ---------------------------------------------------------------------------
// stat_get
// ---------------------------------------------------------------------------

DiagStatus StatsStore::stat_get(uint16_t profile_id, const char* payload_id,
                                  uint16_t stat_id, int32_t* out_value) const
{
    if (!initialized_ || !out_value) {
        return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    }

    char key[KV_MAX_KEY_LEN + 1];
    make_key(key, sizeof(key), profile_id, payload_id, 's', stat_id);

    int32_t  val    = 0;
    uint16_t vlen   = 0;
    DiagStatus s = kv_->get(key,
                             reinterpret_cast<uint8_t*>(&val),
                             &vlen,
                             static_cast<uint16_t>(sizeof(val)));
    if (s.code == DiagCode::STORAGE_NOT_FOUND) {
        *out_value = 0;
        return DiagStatus::success();
    }
    if (!s.ok()) return s;
    *out_value = val;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// stat_set
// ---------------------------------------------------------------------------

DiagStatus StatsStore::stat_set(uint16_t profile_id, const char* payload_id,
                                  uint16_t stat_id, int32_t value, uint8_t op)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char key[KV_MAX_KEY_LEN + 1];
    make_key(key, sizeof(key), profile_id, payload_id, 's', stat_id);

    int32_t final_val = value;

    if (op == 1u || op == 2u) {
        // Read current value for add/max.
        int32_t  cur  = 0;
        uint16_t vlen = 0;
        DiagStatus r = kv_->get(key,
                                 reinterpret_cast<uint8_t*>(&cur),
                                 &vlen,
                                 static_cast<uint16_t>(sizeof(cur)));
        if (r.ok()) {
            if (op == 1u) {           // add
                final_val = cur + value;
            } else {                   // max
                final_val = (value > cur) ? value : cur;
            }
        }
        // If not found, treat current value as 0 (final_val stays as value for add;
        // for max: max(value, 0) = value if value > 0, else 0).
    }

    return kv_->put(key,
                    reinterpret_cast<const uint8_t*>(&final_val),
                    static_cast<uint16_t>(sizeof(final_val)));
}

// ---------------------------------------------------------------------------
// ach_unlock
// ---------------------------------------------------------------------------

DiagStatus StatsStore::ach_unlock(uint16_t profile_id, const char* payload_id,
                                    uint16_t ach_id)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    char key[KV_MAX_KEY_LEN + 1];
    make_key(key, sizeof(key), profile_id, payload_id, 'a', ach_id);

    static const uint8_t one = 1u;
    return kv_->put(key, &one, 1u);
}

// ---------------------------------------------------------------------------
// leader_begin
// ---------------------------------------------------------------------------

DiagStatus StatsStore::leader_begin(uint16_t profile_id, const char* payload_id,
                                      uint16_t lb_id,
                                      uint8_t* token_out, uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    // Find a free slot.
    int slot = -1;
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i) {
        if (!tokens_[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return DiagStatus::error(DiagCode::STORAGE_FULL);

    TokenSlot& ts = tokens_[slot];
    ts.in_use    = true;
    ts.profile_id = profile_id;
    ts.lb_id      = lb_id;
    strncpy(ts.payload_id, payload_id ? payload_id : "",
            STATS_PAYLOAD_KEY_MAX);
    ts.payload_id[STATS_PAYLOAD_KEY_MAX] = '\0';

    // Fill token with deterministic counter (host tests) or ROSC bits (hardware).
#ifdef JLPICART_HOST_TEST
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i) {
        ts.token[i] = static_cast<uint8_t>((token_counter_ >> (i % 4u) * 8u) + i);
    }
    ++token_counter_;
#else
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i) {
        ts.token[i] = static_cast<uint8_t>(rosc_hw->randombit & 0xFFu);
        // Stir additional bits.
        for (uint8_t b = 0; b < 7u; ++b) {
            ts.token[i] = static_cast<uint8_t>(
                (ts.token[i] << 1u) | (rosc_hw->randombit & 1u));
        }
    }
#endif

    if (token_out) memcpy(token_out, ts.token, STATS_TOKEN_LEN);
    if (handle_out) *handle_out = static_cast<uint8_t>(slot);

    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// leader_submit
// ---------------------------------------------------------------------------

DiagStatus StatsStore::leader_submit(uint8_t handle, uint32_t score,
                                       uint8_t /* proof_kind */,
                                       const uint8_t* /* proof_buf */,
                                       uint16_t /* proof_len */)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= STATS_TOKEN_SLOTS) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    TokenSlot& ts = tokens_[handle];
    if (!ts.in_use) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    // Write leaderboard entry.
    char key[KV_MAX_KEY_LEN + 1];
    make_key(key, sizeof(key), ts.profile_id, ts.payload_id, 'l', ts.lb_id);

    LeaderEntry entry = {};
    entry.score     = score;
    entry.timestamp = 0u; // Stage 22: fill with real RTC/tick

    DiagStatus s = kv_->put(key,
                             reinterpret_cast<const uint8_t*>(&entry),
                             static_cast<uint16_t>(sizeof(entry)));

    // Free slot regardless of write result (avoids stuck handles).
    ts.in_use = false;

    return s;
}
