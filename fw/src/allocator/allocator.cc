// allocator.cc — Allocator implementation.

#include "allocator/allocator.h"
#include "allocator/hw_probe.h"
#include <cstring>

// ---------------------------------------------------------------------------
// RequestedCapabilities helpers
// ---------------------------------------------------------------------------

bool RequestedCapabilities::is_required(const char* name) const {
    for (size_t i = 0; i < required_count; ++i) {
        if (strcmp(required[i], name) == 0) return true;
    }
    return false;
}

bool RequestedCapabilities::is_requested(const char* name) const {
    if (all_allowed) return true;
    if (is_required(name)) return true;
    for (size_t i = 0; i < optional_count; ++i) {
        if (strcmp(optional_caps[i], name) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// LaunchPlan helpers
// ---------------------------------------------------------------------------

bool LaunchPlan::is_activated(const char* name) const {
    for (size_t i = 0; i < activated_count; ++i) {
        if (strcmp(activated_ids[i], name) == 0) return true;
    }
    return false;
}

void LaunchPlan::add_failure(const char* cap_id, LaunchFailureKind kind, bool was_hard) {
    if (failure_count >= LAUNCH_MAX_FAILURES) return;
    LaunchFailureReason& f = failures[failure_count++];
    strncpy(f.capability_id, cap_id, CAP_ID_MAX - 1u);
    f.capability_id[CAP_ID_MAX - 1u] = '\0';
    f.kind                 = kind;
    f.was_hard_requirement = was_hard;
}

// ---------------------------------------------------------------------------
// Allocator::probe_hw_capability
// ---------------------------------------------------------------------------

bool Allocator::probe_hw_capability(const char* name) {
    return hw_probe(name);
}

// ---------------------------------------------------------------------------
// sort_by_name — insertion sort on a small index array by capability name.
// Operates on idx[0..count-1], where names[idx[i]] is the string to sort by.
// ---------------------------------------------------------------------------

static void sort_by_name(size_t* idx, size_t count, const char* const* names) {
    for (size_t i = 1; i < count; ++i) {
        size_t key = idx[i];
        size_t j   = i;
        while (j > 0 && strcmp(names[idx[j - 1]], names[key]) > 0) {
            idx[j] = idx[j - 1];
            --j;
        }
        idx[j] = key;
    }
}

// ---------------------------------------------------------------------------
// Allocator::compute
// ---------------------------------------------------------------------------

LaunchPlan Allocator::compute(const CapabilityRegistry& registry,
                               const RequestedCapabilities& requested,
                               ResourceModel& resources)
{
    LaunchPlan plan = {};
    plan.ok = true;

    // Collect all declared capability names for sorting.
    const char* all_names[CAPABILITY_REGISTRY_MAX];
    size_t all_count = registry.list_declared(all_names, CAPABILITY_REGISTRY_MAX);

    // Build sorted index (spec §5.1: "sort by (capability_id, instance_id)").
    size_t sorted_idx[CAPABILITY_REGISTRY_MAX];
    for (size_t i = 0; i < all_count; ++i) sorted_idx[i] = i;
    sort_by_name(sorted_idx, all_count, all_names);

    // Process each candidate in stable alphabetical order.
    for (size_t si = 0; si < all_count; ++si) {
        const char* name    = all_names[sorted_idx[si]];
        bool        allowed = registry.is_allowed(name);
        bool        req     = requested.is_requested(name);
        bool        hard    = requested.is_required(name);

        if (!req) continue;  // not requested — skip silently

        if (!allowed) {
            // Declared but blocked by policy.
            plan.add_failure(name, LaunchFailureKind::POLICY_DISABLED, hard);
            if (hard) plan.ok = false;
            continue;
        }

        if (registry.is_hw(name)) {
            // HW activation gate: optional verification (spec §5.1).
            // Only probe when safe_verify=true; always skip in Stage 8 (stub).
            if (registry.is_safe_verify(name) && !probe_hw_capability(name)) {
                plan.add_failure(name, LaunchFailureKind::VERIFY_FAILED, hard);
                if (hard) plan.ok = false;
                continue;
            }
        } else {
            // SW activation gate: resource allocation (spec §5.1).
            ResourceRequirements req_res = registry.get_requirements(name);
            if (!resources.can_allocate(req_res)) {
                plan.add_failure(name, LaunchFailureKind::ALLOC_FAILED, hard);
                if (hard) plan.ok = false;
                continue;
            }
            resources.apply_allocation(req_res);
        }

        // Capability cleared all gates — activate it.
        if (plan.activated_count < LAUNCH_MAX_ACTIVATED) {
            strncpy(plan.activated_ids[plan.activated_count], name, CAP_ID_MAX - 1u);
            plan.activated_ids[plan.activated_count][CAP_ID_MAX - 1u] = '\0';
            ++plan.activated_count;
        }
    }

    // Post-loop: check any explicitly requested capability (required or optional)
    // that was neither activated nor already recorded as a failure.  These are
    // capabilities not in the declared set at all (MISSING_CAPABILITY).
    auto check_not_seen = [&](const char* name, bool hard) {
        if (plan.is_activated(name)) return;
        for (size_t f = 0; f < plan.failure_count; ++f) {
            if (strcmp(plan.failures[f].capability_id, name) == 0) return;
        }
        plan.add_failure(name, LaunchFailureKind::MISSING_CAPABILITY, hard);
        if (hard) plan.ok = false;
    };

    for (size_t i = 0; i < requested.required_count; ++i) {
        check_not_seen(requested.required[i], true);
    }
    for (size_t i = 0; i < requested.optional_count; ++i) {
        check_not_seen(requested.optional_caps[i], false);
    }

    return plan;
}
