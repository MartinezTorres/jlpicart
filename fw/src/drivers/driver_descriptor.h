#pragma once
#include <cstddef>
#include "allocator/resource_model.h"

// driver_descriptor.h — software capability declarations.
//
// A DriverDescriptor says "this firmware provides software capability X".
// All driver descriptors are declared in exactly one place:
//   fw/src/drivers/driver_descriptor_table.cc
// No other file may add entries to that table.

struct DriverDescriptor {
    const char*          name;       // stable capability name (e.g. "api.core")
    ResourceRequirements resources = {};  // budget consumed when active (zero = none)
};

extern const DriverDescriptor kDriverDescriptors[];
extern const size_t           kDriverDescriptorCount;
