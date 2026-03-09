// storage_health.cc — Flash partition health check.

#include "storage/storage_health.h"
#include "storage/kv_store.h"
#include "storage/append_log.h"

DiagStatus storage_check_health(FlashDevice& dev, StorageHealth& out,
                                 uint32_t kv_ofs,  uint32_t kv_size,
                                 uint32_t log_ofs, uint32_t log_size)
{
    out = {};

    // Check SYSTEM_KV by running a read-only init on a temporary KvStore.
    {
        KvStore kv;
        DiagStatus s = kv.init(dev, kv_ofs, kv_size);
        if (!s.ok()) return s;
        out.system_kv_ok     = kv.initialized();
        out.kv_record_count  = static_cast<uint32_t>(kv.live_count());
    }

    // Check EVENT_LOG by running a read-only init on a temporary AppendLog.
    {
        AppendLog log;
        DiagStatus s = log.init(dev, log_ofs, log_size);
        if (!s.ok()) return s;
        out.event_log_ok    = log.initialized();
        out.log_record_count = log.record_count();
    }

    return DiagStatus::success();
}
