#pragma once
// resource_model.h — Consumable resource budgets for the Stage 8 allocator.
//
// Tracks what the RP2350 has available for peripheral allocation, after
// deducting reserves consumed by the system (bus loop, stacks, API window,
// menu mailbox, etc.).
//
// Three resource classes (spec.md §5.1):
//   Quantifiable: allocated from a shared budget (SRAM bytes).
//   Exclusive:    one owner at a time (PIO state machines, DMA channels).
//   Shareable:    (future) shared under defined rules (e.g. network stack).
//
// ResourceRequirements is embedded in board and driver descriptors.
// A zero-valued struct means "no additional resources needed" and is the
// correct default for capabilities that run entirely within existing system
// budget (e.g. api.core, bus.msx).
//
// Thread safety: NOT thread-safe. Use only from the boot/preflight path.

#include <cstdint>

// RP2350 hardware totals (RP2350 datasheet §2.6, §3.4).
static constexpr uint32_t RP2350_TOTAL_SRAM    = 520u * 1024u;  // 520 KB
static constexpr uint8_t  RP2350_TOTAL_PIO_SMS = 12u;           // 3 PIOs × 4 SMs
static constexpr uint8_t  RP2350_TOTAL_DMA_CH  = 16u;

// System reserves: SRAM consumed by firmware infrastructure that is not
// modelled as a capability resource:
//   ~16 KB — menu mailbox page buffer (static in main.cc)
//   ~17 KB — BUS::cartridges[8] × 2 184 B per Cartridge struct
//   ~16 KB — Core 0 + Core 1 stacks (8 KB each)
//   ~64 KB — linker .bss / .data / heap headroom, tinyusb stack, log buffer
//   ──────
//   ~113 KB → rounded up to 128 KB
//
// api.core, sw.psg, sw.scc, sw.opl4 declare their own SRAM above this floor.
static constexpr uint32_t SYSTEM_SRAM_RESERVE   = 128u * 1024u;  // 128 KB
static constexpr uint8_t  SYSTEM_PIO_SM_RESERVE = 4u;   // bus loop uses PIO0 SMs 0-3
static constexpr uint8_t  SYSTEM_DMA_RESERVE    = 2u;   // reserved for future DMA use

// Available budget for peripheral allocation.
static constexpr uint32_t SRAM_ALLOC_BUDGET   = RP2350_TOTAL_SRAM    - SYSTEM_SRAM_RESERVE;
static constexpr uint8_t  PIO_SM_ALLOC_BUDGET = RP2350_TOTAL_PIO_SMS - SYSTEM_PIO_SM_RESERVE;
static constexpr uint8_t  DMA_CH_ALLOC_BUDGET = RP2350_TOTAL_DMA_CH  - SYSTEM_DMA_RESERVE;

// Resource requirements declared by a peripheral descriptor.
// Zero means "no extra resources needed" — safe default for existing entries.
struct ResourceRequirements {
    uint32_t sram_bytes;    // quantifiable: SRAM consumed while active
    uint8_t  pio_sms;       // exclusive: PIO state machines needed
    uint8_t  dma_channels;  // exclusive: DMA channels needed
};

class ResourceModel {
public:
    // Initialises with system reserves already deducted from the available budget.
    ResourceModel();

    // Returns true if req can be satisfied from the remaining budget.
    bool can_allocate(const ResourceRequirements& req) const;

    // Deduct req from the budget.  Call can_allocate first.
    void apply_allocation(const ResourceRequirements& req);

    // Return resources to the budget (rollback or deactivation).
    void release(const ResourceRequirements& req);

    uint32_t sram_available()   const { return SRAM_ALLOC_BUDGET   - sram_used_; }
    uint8_t  pio_sms_available() const { return PIO_SM_ALLOC_BUDGET - pio_sms_used_; }
    uint8_t  dma_ch_available()  const { return DMA_CH_ALLOC_BUDGET - dma_ch_used_; }

private:
    uint32_t sram_used_    = 0;
    uint8_t  pio_sms_used_ = 0;
    uint8_t  dma_ch_used_  = 0;
};
