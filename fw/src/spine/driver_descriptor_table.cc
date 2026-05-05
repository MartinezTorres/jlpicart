// driver_descriptor_table.cc — software capability declarations.
// Add/remove entries here only; never scatter capability names across files.
//
// Resource accounting:
//   sram_bytes   — bytes of SRAM consumed while this capability is active
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

    // ESP32 AT network transport.
    // TransportEspAt is effectively a single initialized_ bool (≤ 4 B).
    // UART0 is HW and not a modelled resource.
    { "net.esp32", {} },

};

const size_t kDriverDescriptorCount =
    sizeof(kDriverDescriptors) / sizeof(kDriverDescriptors[0]);
