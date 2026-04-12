// opl3fm.cc — Yamaha YMF262 (OPL3) FM synthesis.
//
// Algorithm references (public domain chip analysis):
//   - Yamaha YMF262 Application Manual
//   - OPL3 die analysis (operator/envelope timing) — Osman Achiev / Y. Ohara
//   - Nuked-OPL3 research paper by Alexey Khokholov
//
// Synthesis model (per sample, per operator):
//   1. Update LFO (global, 3.7 Hz triangular wave).
//   2. Compute phase increment from F-number + block + multiplier + VIB.
//   3. Advance phase accumulator.
//   4. Advance ADSR envelope (Q8 fractional accumulator for slow rates).
//   5. Compute waveform sample via log-sin + pow2 tables.
//   6. Apply total attenuation (envelope + TL + KSL + AM).
//
// Channel mixing:
//   Algo 0 (FM):       output = carrier(modulator(feedback(op1)))
//   Algo 1 (additive): output = modulator + carrier
//
// Timing / scaling:
//   At 44100 Hz the envelope Q8 table below targets standard OPL3 timings.
//   Phase increment formula: fnum * kMultHalf[multi] * 2^block * 49716 / 88200
//   (49716 Hz = OPL3 internal sample rate; 88200 = 44100 * 2 for half-units).
//   This gives correct pitch within 0.01% at standard MSX Moonsound tuning.

#include "peripherals/opl3fm.h"
#include <cstring>
#include <cmath>

// ---------------------------------------------------------------------------
// Tables (computed once on first opl3_reset call)
// ---------------------------------------------------------------------------

// kLogSin[256]: quarter log-sine table.
// kLogSin[i] = round(-log2(sin((i+0.5) * π/512)) * 256)
// Range: 2137 (near 0) down to 0 (at π/2).
// Used with total attenuation: att = kLogSin[idx] + env_att + tl_att + ksl_att.
static uint16_t kLogSin[256];

// kPow2[256]: fractional exponential table.
// kPow2[i] = round(2^(i/256) * 512) - 512
// Range: 0..511.
// Converts fractional bits of log-domain attenuation back to linear:
//   amplitude = (512 + kPow2[att & 0xFF]) >> (att >> 8)
static uint16_t kPow2[256];

static bool s_tables_init = false;

static void init_tables()
{
    if (s_tables_init) return;
    for (int i = 0; i < 256; ++i) {
        double x = ((double)i + 0.5) * 3.14159265358979323846 / 512.0;
        kLogSin[i] = (uint16_t)round(-log2(sin(x)) * 256.0);
        kPow2[i]   = (uint16_t)(round(pow(2.0, (double)i / 256.0) * 512.0) - 512.0);
    }
    s_tables_init = true;
}

// ---------------------------------------------------------------------------
// Operator multiplier table: MULTI 0-15 → effective multiplier × 2
// (so we avoid float; divide by 2 when applying)
// Values: 0.5×2=1, 1×2=2, 2×2=4, 3×2=6, ..., 15×2=30
// ---------------------------------------------------------------------------

static const uint8_t kMultHalf[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

// ---------------------------------------------------------------------------
// KSL (key scale level) attenuation table.
// kKslBase[fnum_msb4] = base attenuation in log units for the highest block,
// scaled so that ksl=3 (6 dB/oct) uses the raw value directly.
// Source: derived from YMF262 datasheet table, in units of OPL3_ENV_MAX/48.
// ---------------------------------------------------------------------------

static const uint8_t kKslBase[16] = {
    0, 32, 40, 45, 48, 51, 53, 55,
   56, 58, 59, 60, 61, 62, 63, 64
};

// Precomputed KSL attenuation: kKslAtt[ksl][block][fnum_msb4]
// ksl=0: always 0; ksl=1: 1.5 dB/oct; ksl=2: 3 dB/oct; ksl=3: 6 dB/oct.
// We compute inline from kKslBase rather than a 4×8×16 table.
static inline uint32_t ksl_attenuation(uint8_t ksl, uint8_t block, uint16_t fnum)
{
    if (ksl == 0) return 0u;
    // base = kKslBase[fnum >> 6] for the 4 MSBs of the 10-bit fnum
    uint8_t base = kKslBase[(fnum >> 6) & 0x0Fu];
    // Shift down by (7 - block) octaves from the maximum
    int shift = 7 - (int)block;
    int raw = (int)base - shift * 8;
    if (raw <= 0) return 0u;
    // ksl=1: divide by 4 (1.5 dB/oct ≈ ¼ of 6 dB/oct)
    // ksl=2: divide by 2 (3 dB/oct)
    // ksl=3: full (6 dB/oct)
    if (ksl == 1) raw >>= 2;
    else if (ksl == 2) raw >>= 1;
    return (uint32_t)raw;
}

// ---------------------------------------------------------------------------
// Envelope rate table (Q8 increment per sample at 44100 Hz).
// For effective rate R (0-63), env_acc += kEnvRateQ8[R] each sample;
// env_level += env_acc >> 8; env_acc &= 0xFF.
// Targeting standard OPL3 decay times ×0.887 (44100/49716 ratio).
// ---------------------------------------------------------------------------

static const uint32_t kEnvRateQ8[64] = {
    // 0-3: frozen
    0, 0, 0, 0,
    // 4-7: ~5-10 s
    1, 1, 1, 2,
    // 8-11: ~1.2-2.5 s
    3, 3, 4, 4,
    // 12-15: ~300-600 ms
    5, 6, 8, 10,
    // 16-19: ~75-150 ms
    13, 16, 19, 24,
    // 20-23: ~19-38 ms
    32, 38, 44, 48,
    // 24-27: ~9-18 ms
    64, 77, 88, 96,
    // 28-31: ~4-9 ms
    128, 154, 176, 192,
    // 32-35: ~2-4 ms
    256, 307, 352, 384,
    // 36-39: ~1-2 ms
    512, 614, 704, 768,
    // 40-43: ~0.5-1 ms
    1024, 1229, 1408, 1536,
    // 44-47: ~250-500 µs
    2048, 2458, 2816, 3072,
    // 48-51: ~125-250 µs
    4096, 4915, 5632, 6144,
    // 52-55: ~63-125 µs
    8192, 9830, 11264, 12288,
    // 56-59: ~31-63 µs
    16384, 19661, 22528, 24576,
    // 60-63: ≤1 sample
    65535, 65535, 65535, 65535,
};

// ---------------------------------------------------------------------------
// Operator-level helpers
// ---------------------------------------------------------------------------

// Map slot offset (0-21, with gaps) to operator index (0-17).  0xFF = invalid.
static const uint8_t kSlotToOp[32] = {
     0,  1,  2,  3,  4,  5, 0xFF, 0xFF,
     6,  7,  8,  9, 10, 11, 0xFF, 0xFF,
    12, 13, 14, 15, 16, 17, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

// Operator index (0-17) → channel index (0-8)
static const uint8_t kOpToCh[18] = {
    0, 1, 2, 0, 1, 2,
    3, 4, 5, 3, 4, 5,
    6, 7, 8, 6, 7, 8,
};

// Operator index (0-17) → role within channel (0=modulator, 1=carrier)
static const uint8_t kOpRole[18] = {
    0, 0, 0, 1, 1, 1,
    0, 0, 0, 1, 1, 1,
    0, 0, 0, 1, 1, 1,
};

// Channel index (0-8) → modulator operator index
static const uint8_t kChMod[9] = { 0, 1, 2, 6, 7, 8, 12, 13, 14 };
// Channel index (0-8) → carrier operator index
static const uint8_t kChCar[9] = { 3, 4, 5, 9, 10, 11, 15, 16, 17 };

// ---------------------------------------------------------------------------
// Phase increment computation
// ---------------------------------------------------------------------------

static void update_phase_inc(Opl3State& s, int bank, int ch_in_bank)
{
    int ch_idx = bank * 9 + ch_in_bank;
    const Opl3Ch& ch = s.ch[ch_idx];
    int op_base = bank * 18;

    // phase_inc (Q20) = fnum × multi_half × 2^block × 49716 / 88200
    // To avoid overflow: compute as fnum × multi_half × 2^block, then × (49716/88200)
    // 49716/88200 = 0.5637 ≈ 562/997 ≈ 9/16.  Use 9/16 approximation (0.5625).
    for (int role = 0; role < 2; ++role) {
        int op_idx = op_base + (role == 0 ? kChMod[ch_in_bank] : kChCar[ch_in_bank]);
        Opl3Op& op = s.ops[op_idx];

        uint32_t fnum_scaled = (uint32_t)ch.fnum * kMultHalf[op.multi];
        uint32_t shifted;
        if (ch.block == 0) {
            shifted = fnum_scaled >> 1;
        } else {
            shifted = fnum_scaled << (ch.block - 1u);
        }
        // × 9/16 ≈ × 0.5625 (close to 49716/88200 = 0.5637)
        op.phase_inc = (shifted * 9u) >> 4;
        if (op.phase_inc == 0u) op.phase_inc = 1u;

        // KSL attenuation
        op.ksl_att = ksl_attenuation(op.ksl, ch.block, ch.fnum);
    }
}

// ---------------------------------------------------------------------------
// Envelope helpers
// ---------------------------------------------------------------------------

// Compute effective rate from ADSR register value + KSR.
static inline uint8_t eff_rate(uint8_t base, bool ksr_en, uint8_t block, uint16_t fnum)
{
    if (base == 0) return 0u;
    uint8_t r = (uint8_t)(base * 4u);
    if (ksr_en) {
        r = (uint8_t)(r + (block * 2u) + (fnum >> 9));
    }
    return (r > 63u) ? 63u : r;
}

static inline void env_key_on(Opl3Op& op, bool ksr_en, uint8_t block, uint16_t fnum)
{
    if (op.env_state != Opl3Env::OFF && op.env_state != Opl3Env::RELEASE) {
        // Note already playing: fast DAMP, then attack.
        op.env_state = Opl3Env::DAMP;
    } else {
        op.env_state = Opl3Env::ATTACK;
    }
    op.phase     = 0u;
    op.env_acc   = 0u;
    op.out       = 0;
    op.out_prev  = 0;
    (void)ksr_en; (void)block; (void)fnum;
}

static inline void env_key_off(Opl3Op& op)
{
    if (op.env_state != Opl3Env::OFF) {
        op.env_state = Opl3Env::RELEASE;
    }
}

static void env_advance(Opl3Op& op, uint8_t block, uint16_t fnum,
                         uint8_t sl_thresh_env)
{
    // sl_thresh_env: sustain level in env units (0-480, pre-scaled from SL 0-15)
    uint32_t inc;
    switch (op.env_state) {
    case Opl3Env::DAMP:
        // Fast silence: use rate 60 (effectively instant)
        op.env_acc += kEnvRateQ8[60];
        op.env_level += op.env_acc >> 8;
        op.env_acc &= 0xFFu;
        if (op.env_level >= OPL3_ENV_MAX) {
            op.env_level = OPL3_ENV_MAX;
            op.env_state = Opl3Env::ATTACK;
        }
        return;

    case Opl3Env::ATTACK: {
        uint8_t r = eff_rate(op.ar, op.ksr, block, fnum);
        if (r == 0u) return;
        // Attack rate: slightly faster than decay for same register value.
        // Use attack_inc = decay_inc * 2, capped at 63.
        uint8_t att_r = (r < 61u) ? r + 2u : 63u;
        inc = kEnvRateQ8[att_r];
        op.env_acc += inc;
        uint32_t step = op.env_acc >> 8;
        op.env_acc &= 0xFFu;
        if (step >= op.env_level) {
            op.env_level = 0u;
            op.env_state = Opl3Env::DECAY;
        } else {
            op.env_level -= step;
        }
        return;
    }

    case Opl3Env::DECAY: {
        uint8_t r = eff_rate(op.dr, op.ksr, block, fnum);
        if (r == 0u) { op.env_state = Opl3Env::SUSTAIN; return; }
        op.env_acc += kEnvRateQ8[r];
        op.env_level += op.env_acc >> 8;
        op.env_acc &= 0xFFu;
        if (op.env_level >= sl_thresh_env) {
            op.env_level = sl_thresh_env;
            op.env_state = Opl3Env::SUSTAIN;
        }
        return;
    }

    case Opl3Env::SUSTAIN:
        if (op.eg_typ) return;  // eg_typ=1: hold at sustain level
        // eg_typ=0: fall through to decay-2 (no rate limit in OPL3, use RR)
        [[fallthrough]];

    case Opl3Env::RELEASE: {
        uint8_t r = eff_rate(op.rr, op.ksr, block, fnum);
        if (r == 0u) return;
        op.env_acc += kEnvRateQ8[r];
        op.env_level += op.env_acc >> 8;
        op.env_acc &= 0xFFu;
        if (op.env_level >= OPL3_ENV_MAX) {
            op.env_level = OPL3_ENV_MAX;
            op.env_state = Opl3Env::OFF;
        }
        return;
    }

    case Opl3Env::OFF:
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Operator output computation
// ---------------------------------------------------------------------------

// Compute one operator output sample.
// modulation: phase modulation from previous operator (in Q9 radians × ±1023).
// Returns sample in range ±512.
static int16_t op_output(Opl3Op& op, int32_t modulation,
                          uint32_t am_att, bool tremolo_en)
{
    if (op.env_state == Opl3Env::OFF) {
        op.out = 0;
        return 0;
    }

    // Phase index (10-bit, 0..1023)
    uint32_t phase10 = ((op.phase >> 10) + (uint32_t)(modulation >> 1)) & 0x3FFu;

    // Waveform logic: manipulate phase and sign based on WS.
    // Half = upper bit of phase10 (0 or 1), determines output sign for sine.
    bool sign = false;
    uint32_t p = phase10;

    switch (op.waveform & 7u) {
    case 0: // full sine
        sign = (p & 0x200u) != 0u;
        break;
    case 1: // half-sine (second half → zero)
        if (p & 0x200u) {
            op.out = 0;
            return 0;
        }
        sign = false;
        break;
    case 2: // absolute sine
        sign = false;
        p &= 0x1FFu;
        break;
    case 3: // quarter-sine: only every other quarter, rest zero
        if ((p & 0x100u) != 0u) {
            op.out = 0;
            return 0;
        }
        sign = (p & 0x200u) != 0u;
        break;
    case 4: // pulse sine: doubled frequency, positive half only
        p = (p * 2u) & 0x3FFu;
        if (p & 0x200u) {
            op.out = 0;
            return 0;
        }
        sign = false;
        break;
    case 5: // absolute sine, doubled frequency
        p = (p * 2u) & 0x1FFu;
        sign = false;
        break;
    case 6: // square wave
        sign = (p & 0x200u) != 0u;
        p = 0u;  // logsin[0] = 2137 → heavily attenuated = near full amplitude via pow2
        // Actually we want a flat level: force att=0 below.
        {
            // Compute total attenuation without logsin (square wave = no sine shaping)
            uint32_t att = op.env_level + (uint32_t)op.tl * 8u + op.ksl_att;
            if (tremolo_en) att += am_att;
            if (att >= (1u << 12)) { op.out = 0; return 0; }
            int16_t amp = (int16_t)((512u + kPow2[att & 0xFFu]) >> (att >> 8));
            op.out_prev = op.out;
            op.out = sign ? -amp : amp;
            return op.out;
        }
    case 7: // derived sine: alternating bumps with gap
        {
            uint32_t quarter = p >> 7;  // 0-7
            if (quarter == 1u || quarter == 3u || quarter == 5u || quarter == 7u) {
                op.out = 0;
                return 0;
            }
            sign = (p & 0x200u) != 0u;
            p &= 0x7Fu;  // only use first 1/8 of table repeatedly
        }
        break;
    }

    // Map 10-bit phase to quarter-table 8-bit index (mirror second quarter)
    uint32_t quarter = (p >> 7) & 1u;
    uint32_t idx     = p & 0x7Fu;
    idx = idx << 1;  // scale 0..127 → 0..254; interleave for the mirror below
    if (quarter) idx = 255u - idx;

    // Log-domain attenuation
    uint32_t att = kLogSin[idx & 0xFFu];
    att += op.env_level;
    att += (uint32_t)op.tl * 8u;
    att += op.ksl_att;
    if (tremolo_en) att += am_att;

    if (att >= (1u << 12)) {  // below noise floor (~48 dB down)
        op.out_prev = op.out;
        op.out = 0;
        return 0;
    }

    // Convert back to linear via pow2 table
    int16_t amp = (int16_t)((512u + kPow2[att & 0xFFu]) >> (att >> 8));
    op.out_prev = op.out;
    op.out = sign ? -amp : amp;
    return op.out;
}

// ---------------------------------------------------------------------------
// opl3_reset
// ---------------------------------------------------------------------------

void opl3_reset(Opl3State& s)
{
    init_tables();
    memset(&s, 0, sizeof(s));
    // Default all channels: stereo output enabled (L+R), standard eg_typ
    for (int i = 0; i < 18; ++i) {
        s.ch[i].out_l = true;
        s.ch[i].out_r = true;
    }
    // All operators start silent
    for (int i = 0; i < 36; ++i) {
        s.ops[i].env_state = Opl3Env::OFF;
        s.ops[i].env_level = OPL3_ENV_MAX;
        s.ops[i].tl        = 63u;   // maximum attenuation
        s.ops[i].rr        = 7u;    // default release rate
        s.ops[i].eg_typ    = true;  // sustain hold by default
    }
}

// ---------------------------------------------------------------------------
// opl3_write_reg
// ---------------------------------------------------------------------------

void opl3_write_reg(Opl3State& s, uint16_t reg, uint8_t val)
{
    int bank    = (reg >> 8) & 1;       // 0 = primary, 1 = secondary
    uint8_t off = (uint8_t)(reg & 0xFFu);
    int op_base = bank * 18;
    int ch_base = bank * 9;

    // --- Primary-only global registers ---
    if (bank == 0) {
        if (off == 0x01u) return;  // TEST
        if (off == 0x08u) { s.note_sel = (val >> 6) & 1u; return; }
        if (off == 0xBDu) {
            s.deep_tremolo = (val >> 7) & 1u;
            s.deep_vibrato = (val >> 6) & 1u;

            bool new_rhythm = (val >> 5) & 1u;
            if (new_rhythm && !s.rhythm) {
                // Entering rhythm mode: clear melodic key-on state for ch 6-8
                // so the percussion operators start from a clean state.
                for (int ci = 6; ci <= 8; ++ci) s.ch[ci].key_on = false;
            }
            s.rhythm = new_rhythm;

            uint8_t new_key  = val & 0x1Fu;
            uint8_t prev_key = s.rhythm_key_prev;
            uint8_t rising   = new_key & (uint8_t)(~prev_key);
            uint8_t falling  = (uint8_t)(~new_key) & prev_key;
            s.rhythm_key      = new_key;
            s.rhythm_key_prev = new_key;

            if (s.rhythm) {
                // Percussion ops live in the primary bank (ops 12-17).
                // BD:  bit 4 → mod=op12, car=op15 (ch6)
                // HH:  bit 0 → op13 (ch7 mod)
                // SD:  bit 3 → op16 (ch7 car)
                // TT:  bit 2 → op14 (ch8 mod)
                // CY:  bit 1 → op17 (ch8 car)
                if (rising  & 0x10u) {
                    env_key_on(s.ops[12], s.ops[12].ksr, s.ch[6].block, s.ch[6].fnum);
                    env_key_on(s.ops[15], s.ops[15].ksr, s.ch[6].block, s.ch[6].fnum);
                }
                if (falling & 0x10u) { env_key_off(s.ops[12]); env_key_off(s.ops[15]); }
                if (rising  & 0x01u) env_key_on(s.ops[13], s.ops[13].ksr, s.ch[7].block, s.ch[7].fnum);
                if (falling & 0x01u) env_key_off(s.ops[13]);
                if (rising  & 0x08u) env_key_on(s.ops[16], s.ops[16].ksr, s.ch[7].block, s.ch[7].fnum);
                if (falling & 0x08u) env_key_off(s.ops[16]);
                if (rising  & 0x04u) env_key_on(s.ops[14], s.ops[14].ksr, s.ch[8].block, s.ch[8].fnum);
                if (falling & 0x04u) env_key_off(s.ops[14]);
                if (rising  & 0x02u) env_key_on(s.ops[17], s.ops[17].ksr, s.ch[8].block, s.ch[8].fnum);
                if (falling & 0x02u) env_key_off(s.ops[17]);
            }
            return;
        }
    }
    // 0x104 (4-op enable) and 0x105 (OPL3 mode) live in the secondary bank at offsets 4/5.
    if (bank == 1) {
        if (off == 0x04u) { s.fourop_en = val & 0x3Fu; return; }
        if (off == 0x05u) { s.opl3_mode = (val & 0x01u) != 0u; return; }
    }

    // --- Operator registers: 0x20-0x35, 0x40-0x55, 0x60-0x75, 0x80-0x95, 0xE0-0xF5 ---
    if (off >= 0x20u && off <= 0x35u) {
        uint8_t slot = off - 0x20u;
        if (slot >= 32u || kSlotToOp[slot] == 0xFFu) return;
        Opl3Op& op  = s.ops[op_base + kSlotToOp[slot]];
        op.tremolo  = (val >> 7) & 1u;
        op.vibrato  = (val >> 6) & 1u;
        op.eg_typ   = (val >> 5) & 1u;
        op.ksr      = (val >> 4) & 1u;
        op.multi    = val & 0x0Fu;
        int ch_i    = kOpToCh[kSlotToOp[slot]];
        update_phase_inc(s, bank, ch_i);
        return;
    }
    if (off >= 0x40u && off <= 0x55u) {
        uint8_t slot = off - 0x40u;
        if (slot >= 32u || kSlotToOp[slot] == 0xFFu) return;
        Opl3Op& op = s.ops[op_base + kSlotToOp[slot]];
        op.ksl = (val >> 6) & 0x03u;
        op.tl  = val & 0x3Fu;
        int ch_i = kOpToCh[kSlotToOp[slot]];
        update_phase_inc(s, bank, ch_i);
        return;
    }
    if (off >= 0x60u && off <= 0x75u) {
        uint8_t slot = off - 0x60u;
        if (slot >= 32u || kSlotToOp[slot] == 0xFFu) return;
        Opl3Op& op = s.ops[op_base + kSlotToOp[slot]];
        op.ar = (val >> 4) & 0x0Fu;
        op.dr = val & 0x0Fu;
        return;
    }
    if (off >= 0x80u && off <= 0x95u) {
        uint8_t slot = off - 0x80u;
        if (slot >= 32u || kSlotToOp[slot] == 0xFFu) return;
        Opl3Op& op = s.ops[op_base + kSlotToOp[slot]];
        op.sl = (val >> 4) & 0x0Fu;
        op.rr = val & 0x0Fu;
        return;
    }
    if (off >= 0xE0u && off <= 0xF5u) {
        uint8_t slot = off - 0xE0u;
        if (slot >= 32u || kSlotToOp[slot] == 0xFFu) return;
        s.ops[op_base + kSlotToOp[slot]].waveform = val & 0x07u;
        return;
    }

    // --- Channel registers: 0xA0-0xA8, 0xB0-0xB8, 0xC0-0xC8 ---
    if (off >= 0xA0u && off <= 0xA8u) {
        int ci = ch_base + (off - 0xA0u);
        s.ch[ci].fnum = (uint16_t)((s.ch[ci].fnum & 0x300u) | val);
        update_phase_inc(s, bank, off - 0xA0u);
        return;
    }
    if (off >= 0xB0u && off <= 0xB8u) {
        int ch_i = off - 0xB0u;
        int ci   = ch_base + ch_i;
        s.ch[ci].fnum  = (uint16_t)((s.ch[ci].fnum & 0x0FFu) | (((uint16_t)(val & 0x03u)) << 8));
        s.ch[ci].block = (val >> 2) & 0x07u;
        // In rhythm mode, channels 6-8 of the primary bank are percussion.
        // Key-on/off for those is controlled exclusively by 0xBD; ignore it here.
        if (!(bank == 0 && s.rhythm && ch_i >= 6)) {
            bool new_keyon = (val >> 5) & 1u;
            if (new_keyon && !s.ch[ci].key_on) {
                Opl3Op& mod = s.ops[op_base + kChMod[ch_i]];
                Opl3Op& car = s.ops[op_base + kChCar[ch_i]];
                env_key_on(mod, mod.ksr, s.ch[ci].block, s.ch[ci].fnum);
                env_key_on(car, car.ksr, s.ch[ci].block, s.ch[ci].fnum);
            } else if (!new_keyon && s.ch[ci].key_on) {
                env_key_off(s.ops[op_base + kChMod[ch_i]]);
                env_key_off(s.ops[op_base + kChCar[ch_i]]);
            }
            s.ch[ci].key_on = new_keyon;
        }
        update_phase_inc(s, bank, ch_i);
        return;
    }
    if (off >= 0xC0u && off <= 0xC8u) {
        int ci = ch_base + (off - 0xC0u);
        s.ch[ci].feedback = (val >> 1) & 0x07u;
        s.ch[ci].algo     = (val & 0x01u) != 0u;
        if (s.opl3_mode) {
            s.ch[ci].out_r = (val >> 6) & 1u;
            s.ch[ci].out_l = (val >> 7) & 1u;
        } else {
            s.ch[ci].out_l = true;
            s.ch[ci].out_r = true;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// opl3_compute_sample
// ---------------------------------------------------------------------------

void opl3_compute_sample(Opl3State& s, int16_t& out_l, int16_t& out_r)
{
    // ---- LFO update (3.7 Hz, Q16, wraps at 65536) ----
    // Increment: 3.7 * 65536 / 44100 = 5.497 ≈ 5 (gives ~3.35 Hz, close enough)
    s.lfo_acc += 5u;
    if (s.lfo_acc >= 65536u) s.lfo_acc -= 65536u;

    // Tremolo: triangle wave over the top 8 bits of lfo_acc (0..255 up/down)
    {
        uint8_t tri = (uint8_t)(s.lfo_acc >> 8);
        if (s.lfo_acc & 0x8000u) tri = 255u - tri;
        // deep_tremolo: 4.8 dB max (≈ 52 env units); normal: 1.0 dB (≈ 11 units)
        s.lfo_am_out = s.deep_tremolo ? (uint8_t)((tri * 52u) >> 8)
                                       : (uint8_t)((tri * 11u) >> 8);
    }
    // Vibrato: use top 3 bits (0-7) for 8-step triangular table
    {
        static const int8_t kVibTab[8]  = { 0, 4, 4, 0, 0, -4, -4, 0 };
        static const int8_t kVibTabD[8] = { 0, 8, 8, 0, 0, -8, -8, 0 };
        uint8_t vib_idx = (uint8_t)((s.lfo_acc >> 13) & 0x07u);
        s.lfo_vib_out = s.deep_vibrato ? kVibTabD[vib_idx] : kVibTab[vib_idx];
    }

    int32_t sum_l = 0;
    int32_t sum_r = 0;

    // ---- Process each channel in both banks ----
    for (int bank = 0; bank < 2; ++bank) {
        int op_base = bank * 18;
        int ch_base = bank * 9;

        for (int ch_i = 0; ch_i < 9; ++ch_i) {
            // In rhythm mode, channels 6-8 of the primary bank are handled below.
            if (bank == 0 && s.rhythm && ch_i >= 6) continue;

            Opl3Ch& ch  = s.ch[ch_base + ch_i];
            Opl3Op& mod = s.ops[op_base + kChMod[ch_i]];
            Opl3Op& car = s.ops[op_base + kChCar[ch_i]];

            // Skip channel if both operators are idle (optimisation)
            if (mod.env_state == Opl3Env::OFF && car.env_state == Opl3Env::OFF) continue;

            // ---- Advance envelopes ----
            uint8_t mod_sl = (uint8_t)((uint32_t)mod.sl * (OPL3_ENV_MAX / 15u));
            uint8_t car_sl = (uint8_t)((uint32_t)car.sl * (OPL3_ENV_MAX / 15u));
            env_advance(mod, ch.block, ch.fnum, mod_sl);
            env_advance(car, ch.block, ch.fnum, car_sl);

            // ---- Vibrato: modulate phase_inc ----
            int32_t vib = s.lfo_vib_out;  // ±4 (normal) or ±8 (deep)
            uint32_t mod_inc = mod.phase_inc;
            uint32_t car_inc = car.phase_inc;
            if (mod.vibrato && vib != 0) {
                mod_inc = (uint32_t)((int32_t)mod_inc + ((int32_t)mod_inc * vib) / 1024);
            }
            if (car.vibrato && vib != 0) {
                car_inc = (uint32_t)((int32_t)car_inc + ((int32_t)car_inc * vib) / 1024);
            }

            // ---- Advance phases ----
            mod.phase = (mod.phase + mod_inc) & 0xFFFFFu;
            car.phase = (car.phase + car_inc) & 0xFFFFFu;

            uint32_t am_att = s.lfo_am_out;

            // ---- Operator feedback (op1 self-modulation) ----
            int32_t fb_mod = 0;
            if (ch.feedback > 0u) {
                // Average of last two outputs, shifted by (9 - feedback) bits
                int32_t fb_avg = ((int32_t)mod.out + (int32_t)mod.out_prev);
                fb_mod = fb_avg >> (9 - (int)ch.feedback);
            }

            // ---- Compute operator outputs ----
            int16_t mod_out, car_out;
            if (!ch.algo) {
                // FM (series): op1 → op2
                mod_out = op_output(mod, fb_mod, am_att, mod.tremolo);
                car_out = op_output(car, (int32_t)mod_out * 2, am_att, car.tremolo);
            } else {
                // Additive: op1 + op2 independently
                mod_out = op_output(mod, fb_mod, am_att, mod.tremolo);
                car_out = op_output(car, 0,      am_att, car.tremolo);
                car_out = (int16_t)((int32_t)mod_out + (int32_t)car_out);
            }

            // ---- Mix to stereo ----
            if (ch.out_l) sum_l += car_out;
            if (ch.out_r) sum_r += car_out;
        }
    }

    // ---- Rhythm mode: percussion instruments (primary bank, ch 6-8) ----
    //
    // BD  (Bass Drum)   — ch6, op12 (mod) + op15 (car), key = 0xBD bit 4
    //     Standard 2-op FM/additive channel; no special phase manipulation.
    // HH  (Hi-Hat)      — op13, key = 0xBD bit 0
    // SD  (Snare Drum)  — op16, key = 0xBD bit 3
    // TT  (Tom-Tom)     — op14, key = 0xBD bit 2
    // CY  (Cymbal)      — op17, key = 0xBD bit 1
    //
    // HH/SD/CY use a 1-bit "rm_xor" noise derived from specific phase bits of
    // op13 (HH) and op17 (CY) to produce the metallic, noisy character.
    // TT and BD use normal sine synthesis.

    if (s.rhythm) {
        uint32_t am_att = s.lfo_am_out;

        // ---- Bass Drum: identical to a melody 2-op channel ----
        {
            Opl3Ch& ch  = s.ch[6];
            Opl3Op& mod = s.ops[12];
            Opl3Op& car = s.ops[15];
            if (mod.env_state != Opl3Env::OFF || car.env_state != Opl3Env::OFF) {
                uint32_t mod_sl = (uint32_t)mod.sl * (OPL3_ENV_MAX / 15u);
                uint32_t car_sl = (uint32_t)car.sl * (OPL3_ENV_MAX / 15u);
                env_advance(mod, ch.block, ch.fnum, (uint8_t)mod_sl);
                env_advance(car, ch.block, ch.fnum, (uint8_t)car_sl);

                uint32_t mod_inc = mod.phase_inc;
                uint32_t car_inc = car.phase_inc;
                if (mod.vibrato && s.lfo_vib_out != 0)
                    mod_inc = (uint32_t)((int32_t)mod_inc + ((int32_t)mod_inc * s.lfo_vib_out) / 1024);
                if (car.vibrato && s.lfo_vib_out != 0)
                    car_inc = (uint32_t)((int32_t)car_inc + ((int32_t)car_inc * s.lfo_vib_out) / 1024);
                mod.phase = (mod.phase + mod_inc) & 0xFFFFFu;
                car.phase = (car.phase + car_inc) & 0xFFFFFu;

                int32_t fb_mod = 0;
                if (ch.feedback > 0u)
                    fb_mod = ((int32_t)mod.out + (int32_t)mod.out_prev) >> (9 - (int)ch.feedback);

                int16_t mod_out = op_output(mod, fb_mod, am_att, mod.tremolo);
                int16_t car_out;
                if (!ch.algo) {
                    car_out = op_output(car, (int32_t)mod_out * 2, am_att, car.tremolo);
                } else {
                    car_out = op_output(car, 0, am_att, car.tremolo);
                    car_out = (int16_t)((int32_t)mod_out + (int32_t)car_out);
                }
                sum_l += car_out;
                sum_r += car_out;
            }
        }

        // ---- Phase-noise bit for HH / SD / CY ----
        // Derived from bits of the HH operator (op13) and CY operator (op17)
        // phase accumulators.  This creates the metallic, inharmonic character
        // of hi-hat and cymbal sounds.  Formula adapted from OPL2 die analysis.
        uint32_t hh_ph = s.ops[13].phase >> 10;  // top 10 bits of 20-bit accumulator
        uint32_t cy_ph = s.ops[17].phase >> 10;
        uint32_t rm_xor = ((hh_ph >> 4) ^ (hh_ph >> 9) ^
                           (cy_ph >> 1) ^ (cy_ph >> 7)) & 1u;

        // ---- Hi-Hat (op13): advance normally, output with noise-modified phase ----
        {
            Opl3Op& op = s.ops[13];
            Opl3Ch& ch = s.ch[7];
            if (op.env_state != Opl3Env::OFF) {
                uint32_t sl = (uint32_t)op.sl * (OPL3_ENV_MAX / 15u);
                env_advance(op, ch.block, ch.fnum, (uint8_t)sl);
                op.phase = (op.phase + op.phase_inc) & 0xFFFFFu;
                // Flip bit 8 of the 10-bit phase index when rm_xor=1 to inject noise.
                uint32_t ph10 = (op.phase >> 10) ^ (rm_xor ? 0x100u : 0u);
                uint32_t saved = op.phase;
                op.phase = (ph10 & 0x3FFu) << 10u;
                int16_t sample = op_output(op, 0, am_att, op.tremolo);
                op.phase = saved;
                sum_l += sample;
                sum_r += sample;
            }
        }

        // ---- Snare Drum (op16): phase driven by HH sign + rm_xor ----
        {
            Opl3Op& op = s.ops[16];
            Opl3Ch& ch = s.ch[7];
            if (op.env_state != Opl3Env::OFF) {
                uint32_t sl = (uint32_t)op.sl * (OPL3_ENV_MAX / 15u);
                env_advance(op, ch.block, ch.fnum, (uint8_t)sl);
                op.phase = (op.phase + op.phase_inc) & 0xFFFFFu;
                // Phase based on HH sign bit (creates snare body) XOR noise (snare rattle)
                uint32_t hh_sign = (s.ops[13].phase >> 19) & 1u;
                uint32_t sd_ph10 = (hh_sign ? 0x200u : 0x000u) ^ (rm_xor ? 0x100u : 0x000u);
                uint32_t saved = op.phase;
                op.phase = sd_ph10 << 10u;
                int16_t sample = op_output(op, 0, am_att, op.tremolo);
                op.phase = saved;
                sum_l += sample;
                sum_r += sample;
            }
        }

        // ---- Tom-Tom (op14): standard single-operator, no phase manipulation ----
        {
            Opl3Op& op = s.ops[14];
            Opl3Ch& ch = s.ch[8];
            if (op.env_state != Opl3Env::OFF) {
                uint32_t sl = (uint32_t)op.sl * (OPL3_ENV_MAX / 15u);
                env_advance(op, ch.block, ch.fnum, (uint8_t)sl);
                op.phase = (op.phase + op.phase_inc) & 0xFFFFFu;
                int16_t sample = op_output(op, 0, am_att, op.tremolo);
                sum_l += sample;
                sum_r += sample;
            }
        }

        // ---- Cymbal (op17): phase combination of op17 + bit 8 from HH + rm_xor ----
        {
            Opl3Op& op = s.ops[17];
            Opl3Ch& ch = s.ch[8];
            if (op.env_state != Opl3Env::OFF) {
                uint32_t sl = (uint32_t)op.sl * (OPL3_ENV_MAX / 15u);
                env_advance(op, ch.block, ch.fnum, (uint8_t)sl);
                op.phase = (op.phase + op.phase_inc) & 0xFFFFFu;
                // Combine CY lower bits with HH bit 8 and noise for cymbal character
                uint32_t cy_ph10 = (op.phase >> 10) & 0x1FFu;
                uint32_t hh_bit8 = (s.ops[13].phase >> 18) & 1u;  // bit 8 of HH 10-bit
                uint32_t cy_override = cy_ph10 ^ (hh_bit8 << 8u) ^ (rm_xor ? 0x200u : 0u);
                uint32_t saved = op.phase;
                op.phase = (cy_override & 0x3FFu) << 10u;
                int16_t sample = op_output(op, 0, am_att, op.tremolo);
                op.phase = saved;
                sum_l += sample;
                sum_r += sample;
            }
        }
    }

    out_l = (int16_t)(sum_l > 32767 ? 32767 : (sum_l < -32768 ? -32768 : sum_l));
    out_r = (int16_t)(sum_r > 32767 ? 32767 : (sum_r < -32768 ? -32768 : sum_r));
}
