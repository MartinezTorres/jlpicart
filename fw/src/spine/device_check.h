#pragma once
// device_check.h — Compatibility check for a list of collection devices.
//
// Call check_device_compatibility() before activating a payload's device list
// to ensure no IO port conflicts, subslot conflicts, or SRAM overruns.

#include "bus/device_type.h"
#include <cstddef>
#include <cstdint>

static constexpr size_t DEVICE_CHECK_MAX = 8;

struct DeviceCheckEntry {
    DeviceType type;
    uint8_t    subslot;  // for memory-mapped devices; 0 for IO-only
};

enum class DeviceConflict : uint8_t {
    NONE = 0,
    IO_PORT_CONFLICT,   // two IO devices with overlapping port ranges
    SUBSLOT_CONFLICT,   // two memory-mapped devices in the same subslot
    SRAM_EXCEEDED,      // combined SRAM cost exceeds budget
    UNKNOWN_TYPE,       // DeviceType::UNKNOWN or unrecognised type
};

struct DeviceCheckResult {
    bool           ok;
    DeviceConflict conflict;
    uint8_t        device_a;  // index of first conflicting device
    uint8_t        device_b;  // index of second conflicting device (if applicable)
};

// Check if the given list of collection devices can be simultaneously active.
// sram_budget_bytes is the total SRAM available for collection devices.
// Returns ok=true if all devices are compatible; ok=false with conflict reason otherwise.
DeviceCheckResult check_device_compatibility(const DeviceCheckEntry* devices,
                                              size_t count,
                                              uint32_t sram_budget_bytes);
