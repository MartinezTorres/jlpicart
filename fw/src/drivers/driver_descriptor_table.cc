// driver_descriptor_table.cc — the ONLY software capability declaration list.
//
// Add a new entry here when a new software capability is implemented.
// Remove the entry when the capability is permanently removed.
// Never scatter capability names across multiple files.
// See bootstrapping.md Appendix.
//
// Resource accounting (spec.md §5.1 "Resource and capability model contract v1"):
//   sram_bytes   — bytes of SRAM consumed while this capability is active
//                  (statically allocated objects owned exclusively by this cap)
//   pio_sms      — exclusive PIO state machines consumed
//   dma_channels — exclusive DMA channels consumed
//
// Zero means "no additional resources" and is correct for capabilities that
// run within system reserves or use non-modelled hardware (e.g. UART, PWM).
// The Allocator checks that the total declared usage does not exceed
// (RP2350_TOTAL_SRAM − SYSTEM_SRAM_RESERVE) before activating.

#include "driver_descriptor.h"

const DriverDescriptor kDriverDescriptors[] = {
    // Core API service — always present; provides core.ping, core.get_info, etc.
    // Owns a 16 384-byte (16 KB) buffer embedded in the ApiWindow object
    // (ApiWindow::buf_[WIN_BYTES] in api_window.h).
    { "api.core", { .sram_bytes = 16384u } },

    // MSX mapper emulation.  ROM lives in XIP flash (zero SRAM) and mapper
    // register state (current bank per page) is a handful of bytes inside the
    // BUS::cartridges[] structs, which are already counted in SYSTEM_SRAM_RESERVE.
    { "sw.mapper", {} },

    // PSG audio emulation (AY-3-8910 / YM2149).
    // PsgState ≈ 64 B of synthesis state + per-sample mixing on Core 1.
    // Audio output via PWM (hardware_pwm); PWM is not a modelled resource.
    { "sw.psg",  { .sram_bytes = 128u } },

    // SCC audio emulation (Konami SCC wavetable synthesiser).
    // SccState ≈ 176 B (4 × 32-byte waveform tables + counters + synthesis state).
    { "sw.scc",  { .sram_bytes = 256u } },

    // OPL4 / YMF278B audio emulation (Moonsound PCM + OPL3 FM synthesis).
    // Opl4State ≈ 3 400 B (24 PCM channels + Opl3State 36 operators/18 channels
    // + register mirrors).  opl3fm.cc also uses two runtime-initialised
    // log-sin/pow2 tables (kLogSin[256] + kPow2[256] = 1 024 B SRAM).
    { "sw.opl4", { .sram_bytes = 5120u } },

    // ESP32 AT network transport.
    // TransportEspAt is effectively a single initialized_ bool (≤ 4 B).
    // UART0 is HW and not a modelled resource.
    { "net.esp32", {} },

    // Sunrise ATA-IDE compatible interface (Nextor mass storage).
    // IdeState ≈ 560 B: 512-byte sector buffer + task file registers + state.
    // Nextor ROM lives in XIP flash (zero SRAM); disk image also in XIP flash.
    { "sw.sunrise_ide", { .sram_bytes = 1024u } },
};

const size_t kDriverDescriptorCount =
    sizeof(kDriverDescriptors) / sizeof(kDriverDescriptors[0]);
