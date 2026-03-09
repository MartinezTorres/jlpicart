#include "capability_registry.h"
#include <cstring>

void CapabilityRegistry::init(const BoardDescriptor&  board,
                              const DriverDescriptor* drivers,
                              size_t                  driver_count,
                              const PolicyInfo&       policy) {
    count_       = 0;
    initialized_ = true;

    // Add hardware capabilities from board descriptor.
    for (size_t i = 0; i < board.capability_count && count_ < CAPABILITY_REGISTRY_MAX; i++) {
        CapabilityEntry& e = entries_[count_++];
        e.name        = board.capabilities[i].name;
        e.is_hw       = true;
        e.safe_verify = board.capabilities[i].safe_verify;
        e.allowed     = !is_masked_by_policy(e.name, policy);
    }

    // Add software capabilities from driver descriptor table.
    for (size_t i = 0; i < driver_count && count_ < CAPABILITY_REGISTRY_MAX; i++) {
        CapabilityEntry& e = entries_[count_++];
        e.name        = drivers[i].name;
        e.is_hw       = false;
        e.safe_verify = false;
        e.allowed     = !is_masked_by_policy(e.name, policy);
    }
}

bool CapabilityRegistry::is_declared(const char* name) const {
    for (size_t i = 0; i < count_; i++) {
        if (strcmp(entries_[i].name, name) == 0) return true;
    }
    return false;
}

bool CapabilityRegistry::is_allowed(const char* name) const {
    for (size_t i = 0; i < count_; i++) {
        if (strcmp(entries_[i].name, name) == 0) return entries_[i].allowed;
    }
    return false;
}

size_t CapabilityRegistry::list_declared(const char** out, size_t max) const {
    size_t n = 0;
    for (size_t i = 0; i < count_ && n < max; i++) {
        out[n++] = entries_[i].name;
    }
    return n;
}

size_t CapabilityRegistry::list_allowed(const char** out, size_t max) const {
    size_t n = 0;
    for (size_t i = 0; i < count_ && n < max; i++) {
        if (entries_[i].allowed) out[n++] = entries_[i].name;
    }
    return n;
}

size_t CapabilityRegistry::allowed_count() const {
    size_t n = 0;
    for (size_t i = 0; i < count_; i++) {
        if (entries_[i].allowed) n++;
    }
    return n;
}

bool CapabilityRegistry::is_masked_by_policy(const char* name, const PolicyInfo& policy) {
    // Stage 3: no capabilities are policy-gated yet.
    // Future entries follow this pattern:
    //
    //   if (strcmp(name, "net.esp32") == 0) {
    //       // Network requires POLICY_ALLOW_NETWORK_COLLECTION_INSTALL
    //       // spec.md §5.1: board/driver must declare it; policy may restrict it.
    //       return !(policy.flags & POLICY_ALLOW_NETWORK_COLLECTION_INSTALL);
    //   }
    //
    (void)name;
    (void)policy;
    return false;
}
