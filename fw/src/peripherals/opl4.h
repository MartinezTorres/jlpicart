#pragma once
// opl4.h — Yamaha YMF278B (OPL4-ML) emulation state and interface.
//
// The YMF278B is the sound chip in the Moonsound MSX cartridge.
// It has two independent sections:
//   1. OPL3 FM section (18 channel FM, YMF262-compatible) — fully implemented:
//      2-op melodic, rhythm mode (BD/HH/SD/TT/CY), 4-op paired channels.
//   2. Wave/PCM section (24-channel wavetable) — fully implemented.
//
// ─────────────────────────────────────────────────────────────────
// MSX I/O port map (Moonsound cartridge):
//   Port 0x7E (W): OPL3 primary register address (R=0x000-0x0FF)
//   Port 0x7F (R/W): OPL3 primary data
//   Port 0xF5 (W): memory configuration / status
//   Port 0xF6 (W): Wave register address
//   Port 0xF7 (R/W): Wave data
//
// ─────────────────────────────────────────────────────────────────
// Wave section register map (address via 0xF6, data via 0xF7):
//
//   0x00  LFO_SPEED[2:0]
//   0x01  TEST[0]
//   0x02  MEM_CONFIG[1:0] (0=2MB ROM, 1=4MB ROM, 2=+256KB RAM, 3=+512KB RAM)
//   0x03-0x07  reserved
//
//   Per channel n = 0..23:
//   0x08+n  WAVE[7:0]                       wave-table number, bits 7:0
//   0x20+n  [3:2]=WAVE[9:8], [1:0]=FN[9:8]  wave# MSBs + F-number MSBs
//   0x38+n  FN[7:0]                          F-number, bits 7:0 (10-bit total)
//   0x50+n  [7]=KEYON, [6:3]=OCT[3:0]        key-on trigger + octave (0=-8..15=+7)
//   0x68+n  [7:4]=AR[3:0], [3]=AM, [2]=VIB, [1:0]=LFO[1:0]
//   0x80+n  [7:4]=D1R[3:0], [3:0]=DL[3:0]   decay-1 rate + sustain level
//   0x98+n  [7:4]=D2R[3:0], [3:0]=RR[3:0]   decay-2 rate + release rate
//   0xB0+n  [7]=LD, [6:0]=TL[6:0]           level-direct + total level
//   0xC8+n  [7:4]=PAN[3:0]                  pan (0=hard-L, 15=hard-R, 7/8=centre)
//
// ─────────────────────────────────────────────────────────────────
// Wave ROM header (at start of wave ROM, 12 bytes per entry):
//   byte  0    [7:6]=FORMAT, [5:0]=LVL_SCALE
//              FORMAT: 0=8-bit, 1=12-bit, 2=16-bit, 3=ADPCM
//   bytes 1-3  start address (24-bit little-endian, byte offset in ROM)
//   bytes 4-5  loop-start offset from start (16-bit LE, in samples)
//   bytes 6-7  loop-end   offset from start (16-bit LE, in samples)
//   byte  8    base F-number [7:0]
//   byte  9    [3:0]=base OCT (0-15, as above)
//   byte  10   [7:4]=PAN default, [2:0]=LFO rate default
//   byte  11   reserved
//
// ─────────────────────────────────────────────────────────────────
// Timing:
//   YMF278B clock: 33.8688 MHz
//   Sample rate  : 33.8688e6 / 768 = 44100 Hz
//   Matches PSG/SCC sample rates — share the same PWM update tick.
//
// See spec.md "Audio devices — OPL4" and bootstrapping.md Stage 30.

#include "cartridges/cartridge.h"
#include "peripherals/opl3fm.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// OPL4 wave descriptor (12 bytes, parsed from ROM header)
// ---------------------------------------------------------------------------

struct Opl4WaveDesc {
    uint8_t  format;       // 0=8-bit signed, 1=12-bit, 2=16-bit signed, 3=ADPCM
    uint8_t  lvl_scale;    // level scale factor [5:0]
    uint32_t start_addr;   // absolute byte offset in wave ROM
    uint16_t loop_start;   // loop start (samples from start_addr)
    uint16_t loop_end;     // loop end   (samples from start_addr)
    uint8_t  base_fnum;    // base F-number for natural pitch
    uint8_t  base_oct;     // base octave (0=-8..15=+7)
};

// Parse the wave descriptor at index `n` from the ROM header.
// Returns false and zeroes desc if rom_size < (n+1)*12 bytes.
bool opl4_parse_wave_desc(const uint8_t* rom_base, uint32_t rom_size,
                           uint16_t n, Opl4WaveDesc& desc);

// ---------------------------------------------------------------------------
// OPL4 envelope phase
// ---------------------------------------------------------------------------

enum class Opl4EnvPhase : uint8_t {
    ATTACK  = 0,
    DECAY1  = 1,
    DECAY2  = 2,
    RELEASE = 3,
    OFF     = 4,   // note is silent (fully released or never started)
};

// ---------------------------------------------------------------------------
// OPL4 PCM channel state — one of 24 channels
// ---------------------------------------------------------------------------

struct Opl4Channel {
    // ---- Register-decoded fields (Core 0 writes, Core 1 reads) ----
    volatile uint16_t wave_num;  // 10-bit wave index into ROM header
    volatile uint16_t fnum;      // 10-bit F-number
    volatile uint8_t  oct;       // 4-bit (0-15 → -8..+7)
    volatile uint8_t  ar;        // attack  rate  0-15
    volatile uint8_t  d1r;       // decay-1 rate  0-15
    volatile uint8_t  dl;        // sustain level 0-15
    volatile uint8_t  d2r;       // decay-2 rate  0-15
    volatile uint8_t  rr;        // release rate  0-15
    volatile uint8_t  tl;        // total level   0-127 (0=max, 127=min)
    volatile bool     ld;        // level-direct (bypass envelope)
    volatile uint8_t  pan;       // 0=left, 15=right, 7/8=centre
    volatile bool     am;        // amplitude modulation enable
    volatile bool     vib;       // vibrato enable
    volatile uint8_t  lfo;       // LFO rate select  0-3

    // Trigger flags — written by Core 0, consumed by Core 1 synthesis.
    volatile bool     keyon;     // true while note held
    volatile bool     keyon_event;  // set by register write; cleared by synthesis

    // ---- Synthesis state (Core 1 only) ----
    uint32_t       phase_acc;   // Q16 phase accumulator (integer=sample index)
    Opl4EnvPhase   env_phase;
    uint32_t       env_level;   // current attenuation 0=max, OPL4_ENV_MAX=silent
    uint32_t       adpcm_step;  // ADPCM predictor state (if format==3)
    int16_t        adpcm_pred;  // ADPCM last output sample
    bool           adpcm_nibble_high;  // which nibble to decode next
};

// Envelope attenuation scale: 10-bit (0=max, 1023=silent).
static constexpr uint32_t OPL4_ENV_MAX    = 1023u;
// Sustain level threshold: DL=15 means silence (full attenuation).
static constexpr uint32_t OPL4_ENV_SCALE  = 64u;   // 1 DL unit = 64 attenuation units

// ---------------------------------------------------------------------------
// OPL4State — full chip state for one YMF278B instance
// ---------------------------------------------------------------------------

struct Opl4State {
    // ---- Wave ROM ----
    // Set by opl4_setup(); not modified after that.
    const uint8_t* wave_rom;     // pointer to wave ROM data (may be null)
    uint32_t       wave_rom_size;// bytes in wave ROM (0 if null)

    // ---- PCM channels: Core 0 writes regs, Core 1 does synthesis ----
    Opl4Channel    channels[24];

    // ---- Wave section register mirror (for readback) ----
    volatile uint8_t wave_regs[256];
    volatile uint8_t wave_addr;  // current wave register address

    // ---- OPL3 FM section: register mirror + live FM state ----
    // Mirror index [0..255] = primary bank, [256..511] = secondary bank.
    volatile uint8_t opl3_regs[512];
    volatile uint8_t opl3_addr_primary;    // last address written to 0x7E (port)
    volatile uint8_t opl3_addr_secondary;  // last address written to 0xC4 (port)

    // FM synthesis state — written by Core 0 (register writes) + Core 1 (synthesis).
    Opl3State        opl3;

    // ---- PCM LFO (global, set via wave reg 0x00) ----
    uint32_t         pcm_lfo_acc;   // Q16 phase accumulator (wraps at 65536)

    // ---- Status / global ----
    volatile uint8_t mem_config;   // MEM_CONFIG register
    bool             audio_initialized;
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reset all OPL4 state to power-on defaults.
void opl4_reset(Opl4State& state);

// Install OPL4 I/O callbacks into a Cartridge (IO-only slot).
// wave_rom / wave_rom_size: pointer and size of the wave ROM image (may be null
// for silent operation or host tests).  Stores &state in c.ram_base.
void opl4_setup(Cartridge& c, Opl4State& state,
                const uint8_t* wave_rom, uint32_t wave_rom_size);

// One-time hardware audio init.  No-op in host test builds.
void opl4_audio_init(Opl4State& state);

// Audio synthesis tick.  Call from Core 1 alongside psg_service/scc_service.
// Writes to g_opl4_sample.  No-op in host test builds.
void opl4_service(Opl4State& state);

// Compute one 8-bit audio sample (0-255) from current PCM channel state.
// Advances all synthesis counters.  Exposed for host tests.
uint8_t opl4_compute_sample(Opl4State& state);
