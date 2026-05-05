// device_check.cc — Compatibility check for a list of collection devices.

#include "spine/device_check.h"

DeviceCheckResult check_device_compatibility(const DeviceCheckEntry* devices,
                                              size_t count,
                                              uint32_t sram_budget_bytes)
{
    DeviceCheckResult result = { true, DeviceConflict::NONE, 0u, 0u };
    if (count == 0) return result;

    uint32_t sram_used = 0u;

    for (size_t i = 0; i < count; ++i) {
        const DeviceTypeInfo* info_i = device_type_info(devices[i].type);
        if (!info_i) {
            result.ok       = false;
            result.conflict = DeviceConflict::UNKNOWN_TYPE;
            result.device_a = static_cast<uint8_t>(i);
            return result;
        }

        // SRAM budget check.
        sram_used += info_i->sram_bytes;
        if (sram_used > sram_budget_bytes) {
            result.ok       = false;
            result.conflict = DeviceConflict::SRAM_EXCEEDED;
            result.device_a = static_cast<uint8_t>(i);
            return result;
        }

        // Check against all earlier devices for port and subslot conflicts.
        for (size_t j = 0; j < i; ++j) {
            const DeviceTypeInfo* info_j = device_type_info(devices[j].type);
            if (!info_j) continue;

            // IO port conflict: two IO-only devices with overlapping port ranges.
            if (!info_i->memory_mapped && !info_j->memory_mapped) {
                uint16_t ai = info_i->io_port_base;
                uint16_t bi = static_cast<uint16_t>(ai + info_i->io_port_count);
                uint16_t aj = info_j->io_port_base;
                uint16_t bj = static_cast<uint16_t>(aj + info_j->io_port_count);
                if (ai < bj && aj < bi) {
                    result.ok       = false;
                    result.conflict = DeviceConflict::IO_PORT_CONFLICT;
                    result.device_a = static_cast<uint8_t>(j);
                    result.device_b = static_cast<uint8_t>(i);
                    return result;
                }
            }

            // Subslot conflict: two memory-mapped devices in the same subslot.
            if (info_i->memory_mapped && info_j->memory_mapped) {
                if (devices[i].subslot == devices[j].subslot) {
                    result.ok       = false;
                    result.conflict = DeviceConflict::SUBSLOT_CONFLICT;
                    result.device_a = static_cast<uint8_t>(j);
                    result.device_b = static_cast<uint8_t>(i);
                    return result;
                }
            }
        }
    }

    return result;
}
