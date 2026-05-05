// peripheral_manager.cc — PeripheralManager implementation.

#include "peripherals/peripheral_manager.h"
#include "peripherals/psg.h"
#include "peripherals/scc.h"
#include "peripherals/opl4.h"
#include "peripherals/sunrise_ide.h"
#include "bus/bus.h"
#include "bus/bus.h"
#include "bus/mappers.h"
#include "content/content_store.h"
#include "platform/platform.h"
#include "diag/log.h"
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

        Subslot& slot = BUS::subslots[e.subslot];
        switch (e.mapper_type) {
            case MapperType::ROM:
                mapper_setup_rom(slot, e.rom_data, e.rom_size);       break;
            case MapperType::ROM_32K_MIRRORED:
                mapper_setup_rom_32k_mirrored(slot, e.rom_data);      break;
            case MapperType::KONAMI:
                mapper_setup_konami(slot, e.rom_data);                 break;
            case MapperType::KONAMI_SCC: {
                uint8_t si = e.subslot < MAPPING_MAX_ENTRIES ? e.subslot : 0;
                SccState& ss = scc_states_[si];
                scc_reset(ss);
                mapper_setup_konami_scc(slot, e.rom_data, ss);
                active_scc_ = &ss;  // expose to Core 1 service loop
                break;
            }
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
    }

    BUS::is_expanded = plan.expanded;

    for (size_t i = 0; i < plan.io_device_count; ++i) {
        const IoDeviceEntry& d = plan.io_devices[i];
        switch (d.type) {
            case IoDeviceType::PSG:
                map_psg();
                break;
            case IoDeviceType::OPL4:
                map_opl4(d.wave_payload_id[0] ? d.wave_payload_id : nullptr);
                break;
            case IoDeviceType::NONE:
                break;
        }
    }

    return true;
}

void PeripheralManager::map_psg()
{
    const size_t slot = next_io_subslot_++;
    psg_reset(psg_state_);
    psg_setup(BUS::subslots[slot], psg_state_);
    psg_audio_init(psg_state_);
    psg_active_ = true;
    char buf[56];
    snprintf(buf, sizeof(buf),
             "PSG (AY-3-8910) wired at IO 0xA0-0xA2 (subslot %zu)", slot);
    log_info(buf);
}

void PeripheralManager::map_opl4(const char* payload_id)
{
    const size_t slot = next_io_subslot_++;
    opl4_reset(opl4_state_);

    const uint8_t* wave_rom  = nullptr;
    uint32_t       wave_size = 0u;
    if (payload_id) {
        ContentStore cs;
        if (cs.has_active_collection()) {
            PayloadRecord pr = {};
            if (cs.load_payload(payload_id, pr).ok() && pr.data_size > 0u) {
                wave_rom  = Platform::xip_map(pr.data_flash_offset);
                wave_size = pr.data_size;
                log_info("OPL4: wave ROM mapped from XIP flash");
            }
        }
    }

    opl4_setup(BUS::subslots[slot], opl4_state_, wave_rom, wave_size);
    opl4_audio_init(opl4_state_);
    opl4_active_ = true;
    char buf[64];
    snprintf(buf, sizeof(buf),
             "OPL4 (YMF278B) wired at IO 0x7E/0x7F+0xF5-0xF7 (subslot %zu)", slot);
    log_info(buf);
}

void PeripheralManager::map_sunrise_ide(uint8_t slot, IdeState& state,
                                         const uint8_t* nextor_rom, uint32_t nextor_size,
                                         const uint8_t* disk_image, uint32_t disk_sectors)
{
    char buf[96];
    ide_setup(BUS::subslots[slot], state, nextor_rom, nextor_size,
              disk_image, disk_sectors);
    snprintf(buf, sizeof(buf),
             "Sunrise IDE wired: slot %u, nextor %s, disk %lu sectors",
             slot,
             nextor_rom ? "loaded" : "absent",
             (unsigned long)disk_sectors);
    log_info(buf);
}

void PeripheralManager::map_menu_page(uint8_t* page) {
    BusMap::map_rw_region(1, 0x4000, page);
    BUS::is_expanded = true;
    log_info("Menu page mapped: subslot 1 page 1 (0x4000-0x7FFF) RW");
}

void PeripheralManager::map_api_window(const uint8_t* buf) {
    BusMap::map_ro_region(2, 0x8000, buf);
    log_info("API window mapped: subslot 2 page 2 (0x8000-0xBFFF) RO");
}

void PeripheralManager::service_all() {
    // Order matters: SCC and OPL4 write their output samples first;
    // psg_service() reads them and mixes all three into PWM.
    if (active_scc_)   scc_service(*active_scc_);
    if (opl4_active_)  opl4_service(opl4_state_);
    if (psg_active_)   psg_service(psg_state_);
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
