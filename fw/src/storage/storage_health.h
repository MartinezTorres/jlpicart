#pragma once
// storage_health.h — Lightweight scan of flash partitions for corruption.

#include "diag/diag.h"
#include "storage/flash_device.h"
#include <cstdint>

struct StorageHealth {
    bool     system_kv_ok;        // no corruption detected in SYSTEM_KV
    bool     event_log_ok;        // no corruption detected in EVENT_LOG
    uint32_t kv_record_count;     // valid records found in SYSTEM_KV
    uint32_t log_record_count;    // valid records found in EVENT_LOG
};

// Scan both partitions and populate `out`.  Always returns success unless
// the flash device itself is unreadable.
DiagStatus storage_check_health(FlashDevice& dev, StorageHealth& out);
