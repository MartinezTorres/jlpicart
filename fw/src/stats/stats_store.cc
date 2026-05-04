// stats_store.cc — StatsStore backed by the append-only Store.
//
// Stats and achievements are Store-only (no FAT files).
// Leaderboard entries are written to FAT (72-byte LeaderEntry exceeds the
// 64-byte Store inline limit); a compact LEADER_ENTRY Store record
// (score+timestamp, 8 bytes) is appended best-effort for sync tracking.

#include "stats/stats_store.h"
#include "identity/device_identity.h"
#include "storage/fat_util.h"
#include "storage/store.h"
#include "storage/store_record.h"
#include "profiles/profile_store.h"
#include <cstring>
#include <cstdio>

#ifndef JLPICART_HOST_TEST
#include "hardware/structs/rosc.h"
#include "pico/time.h"
#endif

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Return the profile UUID, synthesizing a deterministic one from the
// profile_id when no matching profile is registered (common in tests).
static Uuid profile_uuid(const ProfileStore* ps, uint16_t profile_id)
{
    if (ps) {
        Uuid u = ps->uuid_for(profile_id);
        if (!u.is_zero()) return u;
    }
    Uuid u = Uuid::zero();
    u.bytes[0] = static_cast<uint8_t>(profile_id & 0xFFu);
    u.bytes[1] = static_cast<uint8_t>((profile_id >> 8) & 0xFFu);
    return u;
}

static void id_str(char* buf, size_t sz, uint16_t id)
{
    snprintf(buf, sz, "%u", static_cast<unsigned>(id));
}

// ---------------------------------------------------------------------------
// make_path (leaderboards only)
// ---------------------------------------------------------------------------

void StatsStore::make_path(char* out, size_t sz,
                            uint16_t profile_id, const char* payload_id,
                            char type_char, uint16_t id)
{
    char pay[STATS_PAYLOAD_KEY_MAX + 1];
    strncpy(pay, payload_id ? payload_id : "_", STATS_PAYLOAD_KEY_MAX);
    pay[STATS_PAYLOAD_KEY_MAX] = '\0';
    if (pay[0] == '\0') { pay[0] = '_'; pay[1] = '\0'; }

    snprintf(out, sz, "1:/saves/%04x/st_%s_%c_%04x.bin",
             static_cast<unsigned>(profile_id),
             pay,
             type_char,
             static_cast<unsigned>(id));
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void StatsStore::init(ProfileStore& ps)
{
    ps_            = &ps;
    initialized_   = true;
    token_counter_ = 0u;
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i) tokens_[i].in_use = false;
}

// ---------------------------------------------------------------------------
// stat_get — read latest absolute value from Store; absent = 0
// ---------------------------------------------------------------------------

DiagStatus StatsStore::stat_get(uint16_t profile_id, const char* payload_id,
                                  uint16_t stat_id, int32_t* out_value) const
{
    if (out_value) *out_value = 0;
    if (!initialized_ || !out_value) return DiagStatus::success();
    if (!store_) return DiagStatus::success();

    Uuid pu = profile_uuid(ps_, profile_id);
    char ids[8];
    id_str(ids, sizeof(ids), stat_id);
    StoreKey key = StoreKey::for_stat(pu, payload_id ? payload_id : "", ids);

    StoreRecord rec;
    if (!store_->find_latest(key, rec).ok()) return DiagStatus::success();
    if (rec.payload_len < sizeof(int32_t)) return DiagStatus::success();

    int32_t val = 0;
    memcpy(&val, rec.payload, sizeof(val));
    *out_value = val;
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// stat_set — compute final value (considering op), append STAT record
// ---------------------------------------------------------------------------

DiagStatus StatsStore::stat_set(uint16_t profile_id, const char* payload_id,
                                  uint16_t stat_id, int32_t value, uint8_t op)
{
    if (!initialized_ || !store_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    Uuid pu = profile_uuid(ps_, profile_id);
    char ids[8];
    id_str(ids, sizeof(ids), stat_id);
    StoreKey key = StoreKey::for_stat(pu, payload_id ? payload_id : "", ids);

    int32_t final_val = value;
    if (op == 1u || op == 2u) {
        int32_t cur = 0;
        StoreRecord prev;
        if (store_->find_latest(key, prev).ok() && prev.payload_len >= sizeof(int32_t))
            memcpy(&cur, prev.payload, sizeof(cur));
        if (op == 1u) final_val = cur + value;
        else          final_val = (value > cur) ? value : cur;
    }

    StoreRecord rec = {};
    memcpy(rec.key, key.data, 32);
    rec.event_type  = static_cast<uint8_t>(StoreEntryType::STAT);
    rec.payload_len = sizeof(int32_t);
    memcpy(rec.payload, &final_val, sizeof(final_val));
    return store_->append(rec);
}

// ---------------------------------------------------------------------------
// ach_unlock — idempotent: check before appending
// ---------------------------------------------------------------------------

DiagStatus StatsStore::ach_unlock(uint16_t profile_id, const char* payload_id,
                                    uint16_t ach_id)
{
    if (!initialized_ || !store_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    Uuid pu = profile_uuid(ps_, profile_id);
    char ids[8];
    id_str(ids, sizeof(ids), ach_id);
    StoreKey key = StoreKey::for_achievement(pu, payload_id ? payload_id : "", ids);

    StoreRecord prev;
    if (store_->find_latest(key, prev).ok() &&
        prev.payload_len >= 1 && prev.payload[0] == 1u)
        return DiagStatus::success();  // already unlocked

    StoreRecord rec = {};
    memcpy(rec.key, key.data, 32);
    rec.event_type  = static_cast<uint8_t>(StoreEntryType::ACHIEVEMENT);
    rec.payload_len = 1u;
    rec.payload[0]  = 1u;
    return store_->append(rec);
}

// ---------------------------------------------------------------------------
// ach_get — check whether achievement is unlocked in the Store
// ---------------------------------------------------------------------------

DiagStatus StatsStore::ach_get(uint16_t profile_id, const char* payload_id,
                                 uint16_t ach_id, bool* unlocked) const
{
    if (!unlocked) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    *unlocked = false;
    if (!initialized_ || !store_) return DiagStatus::success();

    Uuid pu = profile_uuid(ps_, profile_id);
    char ids[8];
    id_str(ids, sizeof(ids), ach_id);
    StoreKey key = StoreKey::for_achievement(pu, payload_id ? payload_id : "", ids);

    StoreRecord rec;
    if (!store_->find_latest(key, rec).ok()) return DiagStatus::success();
    *unlocked = (rec.payload_len >= 1 && rec.payload[0] == 1u);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// leader_begin
// ---------------------------------------------------------------------------

DiagStatus StatsStore::leader_begin(uint16_t profile_id, const char* payload_id,
                                      uint16_t lb_id,
                                      uint8_t* token_out, uint8_t* handle_out)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);

    int slot = -1;
    for (int i = 0; i < STATS_TOKEN_SLOTS; ++i) {
        if (!tokens_[i].in_use) { slot = i; break; }
    }
    if (slot < 0) return DiagStatus::error(DiagCode::STORAGE_FULL);

    TokenSlot& ts = tokens_[slot];
    ts.in_use     = true;
    ts.profile_id = profile_id;
    ts.lb_id      = lb_id;
    strncpy(ts.payload_id, payload_id ? payload_id : "",
            STATS_PAYLOAD_KEY_MAX);
    ts.payload_id[STATS_PAYLOAD_KEY_MAX] = '\0';

#ifdef JLPICART_HOST_TEST
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i)
        ts.token[i] = static_cast<uint8_t>((token_counter_ >> (i % 4u) * 8u) + i);
    ++token_counter_;
#else
    for (uint8_t i = 0; i < STATS_TOKEN_LEN; ++i) {
        ts.token[i] = static_cast<uint8_t>(rosc_hw->randombit & 0xFFu);
        for (uint8_t b = 0; b < 7u; ++b)
            ts.token[i] = static_cast<uint8_t>((ts.token[i] << 1u) | (rosc_hw->randombit & 1u));
    }
#endif

    if (token_out)  memcpy(token_out, ts.token, STATS_TOKEN_LEN);
    if (handle_out) *handle_out = static_cast<uint8_t>(slot);
    return DiagStatus::success();
}

// ---------------------------------------------------------------------------
// leader_submit — write FAT entry + append Store record best-effort
// ---------------------------------------------------------------------------

DiagStatus StatsStore::leader_submit(uint8_t handle, uint32_t score,
                                       uint8_t /*proof_kind*/,
                                       const uint8_t* /*proof_buf*/,
                                       uint16_t /*proof_len*/,
                                       DeviceIdentity* dik)
{
    if (!initialized_) return DiagStatus::error(DiagCode::STORAGE_CORRUPT);
    if (handle >= STATS_TOKEN_SLOTS) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    TokenSlot& ts = tokens_[handle];
    if (!ts.in_use) return DiagStatus::error(DiagCode::STORAGE_NOT_FOUND);

    LeaderEntry entry = {};
    entry.score = score;
#ifdef JLPICART_HOST_TEST
    entry.timestamp = 0u;
#else
    entry.timestamp = static_cast<uint32_t>(time_us_64() / 1000000u);
#endif

    if (dik != nullptr && dik->initialized()) {
        uint8_t msg[28];
        memcpy(msg + 0,  &entry.score,     4u);
        memcpy(msg + 4,  &entry.timestamp, 4u);
        memcpy(msg + 8,  ts.token,         STATS_TOKEN_LEN);
        memcpy(msg + 24, &ts.profile_id,   2u);
        memcpy(msg + 26, &ts.lb_id,        2u);
        dik->sign(msg, sizeof(msg), entry.signature);
    }

    char path[128];
    make_path(path, sizeof(path), ts.profile_id, ts.payload_id, 'l', ts.lb_id);

    char dir[32];
    snprintf(dir, sizeof(dir), "1:/saves/%04x", static_cast<unsigned>(ts.profile_id));
    fat_ensure_dir("1:/saves");
    fat_ensure_dir(dir);

    // Capture slot fields before marking it free.
    const uint16_t cap_profile_id = ts.profile_id;
    const uint16_t cap_lb_id      = ts.lb_id;
    char cap_payload_id[STATS_PAYLOAD_KEY_MAX + 1];
    memcpy(cap_payload_id, ts.payload_id, sizeof(ts.payload_id));

    ts.in_use = false;

    if (!fat_write_file(path, &entry, sizeof(entry)))
        return DiagStatus::error(DiagCode::STORAGE_IO_ERROR);

    if (store_) {
        Uuid pu = profile_uuid(ps_, cap_profile_id);
        char ids[8];
        id_str(ids, sizeof(ids), cap_lb_id);
        StoreKey skey = StoreKey::for_leaderboard(pu, cap_payload_id, ids);
        StoreRecord srec = {};
        memcpy(srec.key, skey.data, 32);
        srec.event_type  = static_cast<uint8_t>(StoreEntryType::LEADER_ENTRY);
        srec.payload_len = 8u;
        memcpy(srec.payload + 0, &entry.score,     4u);
        memcpy(srec.payload + 4, &entry.timestamp, 4u);
        store_->append(srec);
    }

    return DiagStatus::success();
}
