// peripheral_manager.cc — PeripheralManager implementation.

#include "peripherals/peripheral_manager.h"
#include "peripherals/psg.h"
#include "bus/bus_map.h"
#include "mappers/mappers.h"
#include "log/log.h"
#include <cstdio>
#include <cstring>

#ifndef JLPICART_HOST_TEST
#  include "bus/bus.h"
#endif

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

    return plan.ok;
}

bool PeripheralManager::apply_mapping(const MappingPlan& plan) {
    char buf[96];
    if (plan.entry_count == 0) {
        log_info("mapping: no entries (no collection loaded or no mapper_type specified)");
        return true;
    }

    for (size_t i = 0; i < plan.entry_count; ++i) {
        const MappingEntry& e = plan.entries[i];
        snprintf(buf, sizeof(buf), "mapping[%zu]: mapper=%s subslot=%u rom_data=%s",
                 i, mapper_type_to_string(e.mapper_type), e.subslot,
                 e.rom_data ? "loaded" : "pending");
        log_info(buf);

        if (e.rom_data == nullptr && e.ram_data == nullptr) {
            // ROM not yet loaded from flash storage — deferred to content-load stage.
            log_info("  ROM data pending content-load stage; bus wiring skipped");
            continue;
        }

#ifndef JLPICART_HOST_TEST
        // Wire the mapper into BUS::cartridges[subslot].
        Cartridge& slot = BUS::cartridges[e.subslot];
        switch (e.mapper_type) {
            case MapperType::ROM:
                mapper_setup_rom(slot, e.rom_data, e.rom_size);       break;
            case MapperType::ROM_32K_MIRRORED:
                mapper_setup_rom_32k_mirrored(slot, e.rom_data);      break;
            case MapperType::KONAMI:
                mapper_setup_konami(slot, e.rom_data);                 break;
            case MapperType::KONAMI_Z:
                mapper_setup_konami_z(slot, e.rom_data);               break;
            case MapperType::ASCII8:
                mapper_setup_ascii8(slot, e.rom_data);                 break;
            case MapperType::ASCII16:
                mapper_setup_ascii16(slot, e.rom_data);                break;
            case MapperType::RAM:
                mapper_setup_ram(slot, e.ram_data, e.ram_size);        break;
            case MapperType::NONE:
                slot.clear();                                          break;
        }
#endif
    }

#ifndef JLPICART_HOST_TEST
    BUS::is_expanded = plan.expanded;
#endif

    return true;
}

void PeripheralManager::map_psg(PsgState& state)
{
#ifndef JLPICART_HOST_TEST
    psg_setup(BUS::cartridges[4], state);
    psg_audio_init(state);
#else
    // Host test build: BUS::cartridges not available.  Wire into a local dummy
    // so psg_setup() can record the state pointer; callbacks are never invoked.
    static Cartridge dummy_slot;
    psg_setup(dummy_slot, state);
#endif
    log_info("PSG (AY-3-8910) wired at IO ports 0xA0/0xA1/0xA2 (slot 4)");
}

void PeripheralManager::map_menu_page(uint8_t* page) {
    BusMap::map_rw_region(1, 0x4000, page);
#ifndef JLPICART_HOST_TEST
    BUS::is_expanded = true;
#endif
    log_info("Menu page mapped: subslot 1 page 1 (0x4000-0x7FFF) RW");
}

void PeripheralManager::map_api_window(const uint8_t* buf) {
    BusMap::map_ro_region(2, 0x8000, buf);
    log_info("API window mapped: subslot 2 page 2 (0x8000-0xBFFF) RO");
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
