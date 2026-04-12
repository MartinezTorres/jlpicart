#pragma once
// opl3fm.h — Yamaha YMF262 (OPL3) FM synthesis.
//
// Implements the FM section of the YMF278B (OPL4) chip. OPL3 is a superset
// of OPL2 (YM3812): 18 operators, 9 melody channels, stereo output, optional
// 4-operator channels (pairs), 8 waveforms, and a rhythm mode.
//
// ─────────────────────────────────────────────────────────────────────────
// Register map summary (primary bank 0x000-0x0FF; secondary 0x100-0x1FF):
//
//   0x01        TEST (bit 5 = waveform enable in OPL2 compat mode)
//   0x08        NOTE-SEL (bit 6 = key scale select)
//   0x20+slot   [7]=AM [6]=VIB [5]=EG [4]=KSR [3:0]=MULTI
//   0x40+slot   [7:6]=KSL [5:0]=TL
//   0x60+slot   [7:4]=AR [3:0]=DR
//   0x80+slot   [7:4]=SL [3:0]=RR
//   0xA0-0xA8   F-Num[7:0]  (channel 0-8)
//   0xB0-0xB8   [5]=KEYON [4:2]=BLOCK [1:0]=F-Num[9:8]
//   0xBD        [7]=deepAM [6]=deepVIB [5]=rhythm [4:0]=rhythm key-on
//   0xC0-0xC8   [7]=L [6]=R [5:4]=4-op CNT [3:1]=FB [0]=CNT(2-op)
//   0xE0+slot   [2:0]=WS (waveform select)
//
// Primary-only:
//   0x104       [5:0] = 4-op pair enable (ch pairs 0+3, 1+4, 2+5, 9+12, ...)
//   0x105       [1]=OPL3-mode  [0]=OPL3-enable
//
// Operator slot layout within each register bank (18 of 32 slots used):
//   slot offset: 00 01 02 03 04 05   08 09 0A 0B 0C 0D   10 11 12 13 14 15
//   op index:     0  1  2  3  4  5    6  7  8  9 10 11   12 13 14 15 16 17
//   channel:      0  1  2  0  1  2    3  4  5  3  4  5    6  7  8  6  7  8
//   role(0=mod):  0  0  0  1  1  1    0  0  0  1  1  1    0  0  0  1  1  1
//
// Secondary bank ops 18-35 → channels 9-17 (same layout).
// ─────────────────────────────────────────────────────────────────────────
// Output scaling:
//   opl3_compute_sample() returns per-operator-normalized output.
//   Each carrier output: ±512 at peak.
//   Caller should mix FM samples with PCM at the same scale.

#include <cstdint>

// ---------------------------------------------------------------------------
// Envelope state
// ---------------------------------------------------------------------------

enum class Opl3Env : uint8_t {
    DAMP    = 0,   // fast silence before re-attack (on key-on while playing)
    ATTACK  = 1,
    DECAY   = 2,
    SUSTAIN = 3,   // held at sustain level (eg_typ=1) or continues decaying (eg_typ=0)
    RELEASE = 4,
    OFF     = 5,
};

// ---------------------------------------------------------------------------
// Per-operator state  (Core 0 writes register fields; Core 1 reads all)
// ---------------------------------------------------------------------------

struct Opl3Op {
    // ---- register-decoded params ----
    uint8_t  multi;     // MULTI  0-15
    uint8_t  ksl;       // KSL    0-3
    uint8_t  tl;        // TL     0-63  (0=max volume, 63=min)
    uint8_t  ar;        // AR     0-15
    uint8_t  dr;        // DR     0-15
    uint8_t  sl;        // SL     0-15  (sustain level, 15=max attenuation)
    uint8_t  rr;        // RR     0-15
    uint8_t  waveform;  // WS     0-7
    bool     tremolo;   // AM enable
    bool     vibrato;   // VIB enable
    bool     ksr;       // key scale rate
    bool     eg_typ;    // 1=sustain, 0=non-sustain (decays through sustain)

    // ---- channel-derived (updated when channel freq/keyon changes) ----
    uint32_t phase_inc;  // Q20 phase increment per sample
    uint32_t ksl_att;    // KSL attenuation in envelope units

    // ---- synthesis state (Core 1 only) ----
    uint32_t phase;      // Q20 phase accumulator (top 10 bits = waveform index)
    uint32_t env_acc;    // Q8 fractional accumulator for slow envelope rates
    uint32_t env_level;  // current attenuation (0=loud, OPL3_ENV_MAX=silent)
    Opl3Env  env_state;
    bool     key_on;     // last key-on state (for edge detection)

    int16_t  out;        // last output sample (for FM modulation / feedback)
    int16_t  out_prev;   // previous output (for 2-sample feedback average)
};

// ---------------------------------------------------------------------------
// Per-channel state
// ---------------------------------------------------------------------------

struct Opl3Ch {
    uint16_t fnum;     // 10-bit F-number
    uint8_t  block;    // 3-bit block (0-7, adds 0-7 octaves)
    bool     key_on;   // current key-on bit
    uint8_t  feedback; // op1 feedback 0-7 (0=none)
    bool     algo;     // 0=FM series, 1=additive
    bool     out_l;    // left output enable  (OPL3 mode)
    bool     out_r;    // right output enable (OPL3 mode)
};

// ---------------------------------------------------------------------------
// Full OPL3 FM chip state
// ---------------------------------------------------------------------------

static constexpr uint32_t OPL3_ENV_MAX = 511u;  // 9-bit (0=loud, 511=silent)

struct Opl3State {
    // 36 operators: [0..17] = primary bank (ch 0-8), [18..35] = secondary (ch 9-17)
    Opl3Op ops[36];
    // 18 channels: [0..8] = primary bank, [9..17] = secondary bank
    Opl3Ch ch[18];

    // Global FM LFO state (fixed 3.7 Hz, Q16 accumulator)
    uint32_t lfo_acc;       // Q16 phase accumulator (wraps at 2^16)
    uint8_t  lfo_am_out;    // current tremolo depth value (0-255)
    int8_t   lfo_vib_out;   // current vibrato value (-127..127)
    bool     deep_tremolo;  // 0=1.0dB, 1=4.8dB  (0xBD bit 7)
    bool     deep_vibrato;  // 0=7 cents, 1=14 cents (0xBD bit 6)
    bool     rhythm;        // rhythm mode (0xBD bit 5)
    uint8_t  rhythm_key;    // rhythm key-on bits [4:0] (0xBD bits 4:0)
    bool     opl3_mode;     // OPL3 mode enable (0x105 bit 0)
    uint8_t  fourop_en;     // 4-op pair enable (0x104 bits 5:0)
    uint8_t  note_sel;      // NOTE-SEL (0x08 bit 6)
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Reset all FM state to power-on defaults. Also initialises internal tables
// the first time it is called (cheap, once-only floating-point computation).
void opl3_reset(Opl3State& s);

// Write to an OPL3 register.
// reg[8] = bank select (0=primary 0x000-0x0FF, 1=secondary 0x100-0x1FF).
// reg[7:0] = register offset within bank.
void opl3_write_reg(Opl3State& s, uint16_t reg, uint8_t val);

// Compute one stereo FM sample pair. Advances all counters.
// Output range per channel: each of the 9 (per bank) carrier outputs ±512.
// Returned left/right are the sum of all 18 channels; range ±4608 maximum.
void opl3_compute_sample(Opl3State& s, int16_t& out_l, int16_t& out_r);
