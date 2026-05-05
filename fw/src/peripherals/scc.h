#pragma once
// scc.h — Konami SCC (Sound Creative Chip) emulation state and interface.
//
// The SCC is a 5-channel wavetable synthesiser found inside Konami game
// cartridges (Nemesis, Salamander, Vampire Killer, etc.).  It is accessed
// through the Konami SCC mapper: writing bank value 0x3F to segment 4
// (0x8000-0x97FF) enables the SCC register space at 0x9800-0x9FFF.
//
// SCC Register map (offsets from 0x9800):
//   0x000-0x01F  Waveform channel 1  (32 signed bytes)
//   0x020-0x03F  Waveform channel 2  (32 signed bytes)
//   0x040-0x05F  Waveform channel 3  (32 signed bytes)
//   0x060-0x07F  Waveform channel 4  (32 signed bytes; channel 5 shares this)
//   0x080-0x081  Frequency channel 1  (12-bit, little-endian)
//   0x082-0x083  Frequency channel 2
//   0x084-0x085  Frequency channel 3
//   0x086-0x087  Frequency channel 4
//   0x088-0x089  Frequency channel 5
//   0x08A        Volume channel 1  (4-bit, 0-15)
//   0x08B        Volume channel 2
//   0x08C        Volume channel 3
//   0x08D        Volume channel 4
//   0x08E        Volume channel 5
//   0x08F        Channel enable  (bit 0-4, 1 = on)
//   0x0E0-0x0FF  Test/deformation (not emulated — writes silently accepted)
//
// This module provides:
//   1. Register state machine — memory read/write callbacks used by the
//      Konami SCC mapper (mapper_setup_konami_scc()).  Runs on Core 0.
//   2. Audio synthesis — 5-channel wavetable, mixed to 8-bit PCM output
//      via PWM on GPIO64_SND (shares the PSG audio output).  Runs on Core 1.
//
// Design notes:
//   - SccState.wave/freq/vol/enable are volatile: Core 0 writes (bus
//     callbacks), Core 1 reads (synthesis).
//   - All bus callbacks used by the mapper are RAMFUNC.
//   - scc_service() must be called from Core 1 alongside psg_service().
//     It writes the SCC sample to a separate variable; psg_service() mixes
//     both chips before updating the PWM duty cycle.
//
// SCC clock: 3.579545 MHz (full MSX Z80 clock, unlike PSG which uses /2).
// Output sample rate: 44100 Hz.
// SCC_TICKS_PER_SAMPLE_FP16 = floor(3579545/44100 × 65536) = 5319483.

#include "bus/cartridge.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// SCC timing constant
// ---------------------------------------------------------------------------

// Q16 MSX clock ticks per audio sample at 44100 Hz.
// = floor(3579545 / 44100 × 65536)
static constexpr uint32_t SCC_TICKS_PER_SAMPLE_FP16 = 5319483u;

// ---------------------------------------------------------------------------
// SccState — in-memory state for one SCC instance
// ---------------------------------------------------------------------------

struct SccState {
    // --- Registers: Core 0 writes (bus callbacks), Core 1 reads (synthesis) ---

    // Waveforms: 4 independent tables × 32 signed bytes.
    // Channel 5 shares table 3 (index [3]).
    volatile int8_t  wave[4][32];

    // 12-bit frequency registers (only bits 11:0 are used; stored in full uint16).
    volatile uint16_t freq[5];

    // 4-bit volume registers (0–15).
    volatile uint8_t  vol[5];

    // Channel enable bits (bit 0 = ch1, … bit 4 = ch5; 1 = enabled).
    volatile uint8_t  enable;

    // --- Mapper state: Core 0 only (written by konami_scc_write_cb) ---
    bool     scc_enabled;  // true when segment 4 bank select was written 0x3F

    // --- Synthesis state: Core 1 only ---

    // Q16 phase counters per channel.  Advances by SCC_TICKS_PER_SAMPLE_FP16
    // per audio sample; wraps at (freq + 1) × 65536, advancing wave_pos by 1.
    uint32_t phase_counter[5];

    // Current waveform position (0–31) for each channel.
    uint8_t  wave_pos[5];

    // Hardware audio init flag (firmware only).
    bool     audio_initialized;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reset SCC state to power-on defaults (all registers zero, synthesis reset).
void scc_reset(SccState& state);

// Register memory read/write callbacks for the SCC register space on a
// Cartridge that was already set up as Konami SCC (mapper_setup_konami_scc).
// Internal function — called automatically by mapper_setup_konami_scc().
void scc_setup(Cartridge& c, SccState& state);

// One-time hardware audio init: arm the SCC sample register.
// The SCC sample is mixed with the PSG sample in psg_service().
// No-op in host test builds.
void scc_audio_init(SccState& state);

// Audio synthesis service tick.  Call from Core 1 alongside psg_service().
// Computes next SCC sample and stores it in psg_service()'s mix path.
// No-op in host test builds.
void scc_service(SccState& state);

// Compute one audio sample (0–255) from the current SCC register state.
// Advances all synthesis counters (phase, waveform position).
// Exposed for host tests.
uint8_t scc_compute_sample(SccState& state);
