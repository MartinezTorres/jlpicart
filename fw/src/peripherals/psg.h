#pragma once
// psg.h — AY-3-8910 / YM2149 PSG emulation state and interface.
//
// The AY-3-8910 is the standard MSX sound chip, exposed to MSX software at:
//   Port 0xA0  (write): register select
//   Port 0xA1  (write): register data write
//   Port 0xA2  (read):  register data read
//
// This module provides:
//   1. Register state machine — correct IO port behavior (Core 0 bus callbacks).
//   2. Audio synthesis — 3-channel tone + noise + envelope, mixed to 8-bit PCM
//      and output via PWM on GPIO64_SND (Core 1 service tick).
//
// Design notes:
//   - PsgState.regs[] is marked volatile: Core 0 writes (bus callbacks),
//     Core 1 reads (audio synthesis). No explicit barriers needed at audio
//     granularity (~22 µs per sample).
//   - All bus callbacks are RAMFUNC to avoid XIP stalls in the Core 0 loop.
//   - psg_service() must be called from Core 1 at ~44100 Hz (or as fast as
//     possible); it uses time_us_64() internally to self-rate-limit.
//   - psg_audio_init() must be called once before psg_service().
//

#include "bus/cartridge.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// AY-3-8910 register map (R0–R15)
// ---------------------------------------------------------------------------

// R0–R1:  Channel A tone period  (12-bit: R1[3:0]||R0[7:0])
// R2–R3:  Channel B tone period
// R4–R5:  Channel C tone period
// R6:     Noise period (5-bit, R6[4:0])
// R7:     Mixer control (6-bit active-low: IOB,IOA,/noise_C,/noise_B,/noise_A,/tone_C,/tone_B,/tone_A)
// R8:     Channel A amplitude (bit4=envelope_enable, bits3:0=level 0–15)
// R9:     Channel B amplitude
// R10:    Channel C amplitude
// R11:    Envelope period low  (8-bit)
// R12:    Envelope period high (8-bit)  → EP = (R12<<8)|R11
// R13:    Envelope shape (4-bit: CONT[3], ATT[2], ALT[1], HOLD[0])
// R14:    I/O port A (joystick port, reads 0xFF when direction is input)
// R15:    I/O port B

// Reset values (applied by psg_reset()).  All registers 0 except:
//   R7  = 0xFF — all tone/noise muted, both IO ports as inputs
//   R14 = 0xFF — no joystick connected (all lines high)
//   R15 = 0xFF — no joystick connected

// ---------------------------------------------------------------------------
// PsgState — in-memory state for one PSG instance
// ---------------------------------------------------------------------------

struct PsgState {
    // --- Shared state: Core 0 writes, Core 1 reads ---
    volatile uint8_t reg_select;   // currently selected register (0–15)
    volatile uint8_t regs[16];     // AY-3-8910 register bank

    // --- Synthesis state: Core 1 only ---

    // Tone generators (3 channels): Q16 fixed-point counters.
    // Advance by PSG_TICKS_PER_SAMPLE_FP16 per audio sample; toggle when >= period.
    uint32_t tone_counter[3];  // Q16 units
    uint8_t  tone_output[3];   // current square-wave phase: 0 or 1

    // Noise generator: 17-bit LFSR, Q16 counter.
    uint32_t noise_counter;    // Q16 units
    uint32_t noise_lfsr;       // 17-bit LFSR (init to 0x00001)
    uint8_t  noise_output;     // current noise bit

    // Envelope generator: uint64_t counter for large period values.
    uint64_t env_counter;      // Q16 units (advances toward env_step_period_fp16)
    uint32_t env_pos;          // monotonically increasing step position
    uint8_t  env_level;        // current envelope level (0–15), cached

    // Hardware audio init flag (hardware path only).
    bool     audio_initialized;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reset all registers to MSX power-on defaults and zero synthesis state.
// Call once before psg_setup().
void psg_reset(PsgState& state);

// Install PSG IO port callbacks into a Cartridge (bus IO-only slot 4 or higher).
// Stores &state in c.ram_base.  Wires 0xA0 write, 0xA1 write, 0xA2 read.
// Call psg_reset() before this.
void psg_setup(Cartridge& c, PsgState& state);

// One-time hardware audio init: configure PWM on GPIO64_SND for audio output.
// No-op in host test builds.  Must be called once before psg_service().
void psg_audio_init(PsgState& state);

// Audio synthesis service tick.  Call from Core 1 as fast as possible.
// Self-rate-limits to ~44100 Hz using time_us_64().
// Computes next audio sample from register state and updates PWM duty.
// No-op in host test builds.
void psg_service(PsgState& state);

// Compute one audio sample (0–255) from the current register state.
// Advances all synthesis counters (tone, noise, envelope).
// Exposed for testing; on hardware psg_service() calls this internally.
uint8_t psg_compute_sample(PsgState& state);
