// peripheral_manager.cc — PeripheralManager implementation.

#include "peripherals/peripheral_manager.h"
#include "log/log.h"
#include <cstdio>
#include <cstring>

// String table for LaunchFailureKind — must stay in sync with the enum.
static const char* const kFailureKindStr[] = {
    "MISSING_CAPABILITY",
    "POLICY_DISABLED",
    "VERIFY_FAILED",
    "ALLOC_FAILED",
    "MAPPING_CONFLICT",
};
static constexpr size_t kFailureKindCount =
    sizeof(kFailureKindStr) / sizeof(kFailureKindStr[0]);

bool PeripheralManager::apply(const LaunchPlan& plan, CapabilityRegistry& registry) {
    launch_ok_       = plan.ok;
    activated_count_ = plan.activated_count;

    // Mark each activated capability in the registry.
    for (size_t i = 0; i < plan.activated_count; ++i) {
        registry.mark_activated(plan.activated_ids[i]);
    }

    // TODO(bus-layer): apply MSX-visible hardware mappings from the MappingPlan
    // (port ranges, memory pages, subslots) once the bus layer is wired in
    // Stage 9+.

    return plan.ok;
}

void PeripheralManager::log_report(const LaunchPlan& plan) const {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "activation: activated=%zu failures=%zu ok=%d",
        plan.activated_count, plan.failure_count, plan.ok ? 1 : 0);
    log_info(buf);

    for (size_t i = 0; i < plan.activated_count; ++i) {
        snprintf(buf, sizeof(buf), "  [ACT]  %s", plan.activated_ids[i]);
        log_info(buf);
    }

    for (size_t i = 0; i < plan.failure_count; ++i) {
        const LaunchFailureReason& f = plan.failures[i];
        uint8_t ki = static_cast<uint8_t>(f.kind);
        const char* ks = ki < kFailureKindCount ? kFailureKindStr[ki] : "UNKNOWN";
        snprintf(buf, sizeof(buf), "  [FAIL] %s: %s%s",
            f.capability_id, ks,
            f.was_hard_requirement ? " (hard req)" : "");
        log_warn(buf);
    }
}
