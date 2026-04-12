// opl4.cc — Yamaha YMF278B (OPL4-ML) emulation.
//
// See opl4.h for register map and design notes.
//
// PCM synthesis model (per sample):
//   1. Key-on events: restart envelope + phase accumulator.
//   2. Advance global PCM LFO counter (speed from wave reg 0x00).
//   3. Advance envelope ADSR state machine.
//   4. Advance phase accumulator; read wave sample with linear interpolation.
//   5. Apply AM (tremolo) and VIB (vibrato) from LFO if channel flags set.
//   6. Apply envelope attenuation + total level; mix with stereo pan.
//   7. Mix FM (opl3_compute_sample) and PCM to a mono 8-bit output.
//
// Envelope attenuation is 10-bit (0=max, OPL4_ENV_MAX=silent).
// Attack  decreases attenuation (simplified linear ramp).
// Decay1  increases attenuation at D1R rate until DL*OPL4_ENV_SCALE.
// Decay2  continues increasing at D2R rate toward OPL4_ENV_MAX.
// Release increases attenuation at RR  rate to OPL4_ENV_MAX.
//
// Wave ROM format: 12-byte descriptors at ROM byte 0 (see opl4.h).
// Supported formats: 8-bit signed PCM, 16-bit signed PCM.
// 12-bit and ADPCM: limited support (ADPCM has a basic IMA decoder).
//
// PCM LFO (global, wave reg 0x00 bits [2:0]):
//   Speed 0-7: 0.168, 0.337, 0.674, 1.011, 1.485, 2.359, 3.527, 7.066 Hz.
//   Per-channel: AM (tremolo) and VIB (vibrato) flags at reg 0x68+ch.
//   Per-channel LFO[1:0] selects depth: 0=none, 1=shallow, 2=medium, 3=deep.
//   VIB depth: ~±0, ±3.4, ±6.7, ±13.4 cents. AM depth: ~0, 1.8, 2.5, 3.0 dB.

#include "peripherals/opl4.h"
#include "boards/gpio_defs.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#  include "hardware/pwm.h"
#  include "hardware/gpio.h"
#  include "pico/time.h"
extern volatile uint8_t g_opl4_sample;
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Maximum attenuation step per sample for a given ADSR rate (0-15).
// Rate 0 = no change.  Rate 15 = 32 units/sample (fastest audible decay).
// Roughly models OPL4 timing where rate 15 decays in ~3 ms at 44100 Hz.
static constexpr uint32_t kEnvStep[16] = {
    0, 1, 1, 1, 2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 64, 128
};

// Attack steps: attack decrements attenuation.  Same rates, but applied
// as subtraction.  Attack is slightly faster by design.
static constexpr uint32_t kAttStep[16] = {
    0, 2, 2, 4, 4, 8, 8, 16, 16, 32, 32, 64, 64, 128, 256, 512
};

// ---------------------------------------------------------------------------
// PCM LFO constants
// ---------------------------------------------------------------------------

// PCM LFO Q16 increment per sample for speed 0-7.
// Speed[s] = freq[s] * 65536 / 44100.
// Frequencies: 0.168, 0.337, 0.674, 1.011, 1.485, 2.359, 3.527, 7.066 Hz.
static const uint32_t kPcmLfoInc[8] = { 250, 500, 1001, 1503, 2205, 3504, 5239, 10506 };

// AM (tremolo) depth in OPL4_ENV attenuation units for LFO[1:0]=0..3.
// Units: 0=0, then ~1.8 dB, 2.5 dB, 3.0 dB — mapped to env scale (64 units/DL).
static const uint32_t kAmDepth[4] = { 0, 19, 27, 32 };

// VIB (vibrato) depth multiplier (parts per 1024 of F-number) for LFO[1:0]=0..3.
// Corresponds to ±0, ±3.4, ±6.7, ±13.4 cents peak-to-peak.
static const int32_t kVibDepth[4] = { 0, 4, 8, 16 };

// ---------------------------------------------------------------------------
// Wave ROM helpers
// ---------------------------------------------------------------------------

bool opl4_parse_wave_desc(const uint8_t* rom_base, uint32_t rom_size,
                           uint16_t n, Opl4WaveDesc& desc)
{
    const uint32_t offset = (uint32_t)n * 12u;
    if (!rom_base || rom_size < offset + 12u) {
        desc = {};
        return false;
    }
    const uint8_t* p = rom_base + offset;
    desc.format     = (p[0] >> 6) & 0x03u;
    desc.lvl_scale  = p[0] & 0x3Fu;
    desc.start_addr = (uint32_t)p[1]
                    | ((uint32_t)p[2] << 8)
                    | ((uint32_t)p[3] << 16);
    desc.loop_start = (uint16_t)p[4] | ((uint16_t)p[5] << 8);
    desc.loop_end   = (uint16_t)p[6] | ((uint16_t)p[7] << 8);
    desc.base_fnum  = p[8];
    desc.base_oct   = p[9] & 0x0Fu;
    return true;
}

// Read one sample from the wave ROM.
// Returns sample as int16_t in range -32768..32767.
// Advances *byte_offset according to sample format; returns false at end.
static int16_t read_wave_sample(const uint8_t* rom, uint32_t rom_size,
                                 uint8_t format, uint32_t byte_addr)
{
    if (!rom || byte_addr >= rom_size) return 0;

    switch (format) {
    case 0: {  // 8-bit signed PCM
        return (int16_t)(int8_t)rom[byte_addr] << 8;
    }
    case 1: {  // 12-bit signed PCM (packed as 3 bytes per 2 samples)
        // Pair index = byte_addr / 2; offset within pair determines nibble.
        uint32_t pair  = byte_addr / 2u;
        uint32_t base  = pair * 3u;
        if (base + 2u >= rom_size) return 0;
        if ((byte_addr & 1u) == 0u) {
            // High 12-bit sample: bytes [0] full + [1] high nibble.
            int16_t raw = (int16_t)((uint16_t)rom[base] | ((uint16_t)(rom[base+1] & 0xF0u) << 4));
            return (raw << 4) >> 4;  // sign-extend 12→16
        } else {
            // Low 12-bit sample: [1] low nibble + [2] full.
            int16_t raw = (int16_t)(((uint16_t)(rom[base+1] & 0x0Fu) << 8) | rom[base+2]);
            return (raw << 4) >> 4;
        }
    }
    case 2: {  // 16-bit signed PCM (little-endian); byte_addr is actual byte offset
        if (byte_addr + 1u >= rom_size) return 0;
        return (int16_t)((uint16_t)rom[byte_addr] | ((uint16_t)rom[byte_addr+1] << 8));
    }
    case 3:    // ADPCM — handled separately in synthesis loop
    default:
        return 0;
    }
}

// IMA ADPCM step table and index table (standard IMA ADPCM).
static const uint16_t kAdpcmStepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};
static const int8_t kAdpcmIndexTable[8] = {-1,-1,-1,-1, 2, 4, 6, 8};

static int16_t adpcm_decode_nibble(uint8_t nibble, int16_t& pred, uint32_t& step_idx)
{
    uint32_t si  = step_idx < 88u ? step_idx : 88u;
    uint16_t step = kAdpcmStepTable[si];
    int32_t delta = 0;
    if (nibble & 4u) delta += step;
    if (nibble & 2u) delta += step >> 1;
    if (nibble & 1u) delta += step >> 2;
    delta += step >> 3;
    if (nibble & 8u) delta = -delta;
    int32_t new_pred = (int32_t)pred + delta;
    if (new_pred >  32767) new_pred =  32767;
    if (new_pred < -32768) new_pred = -32768;
    pred = (int16_t)new_pred;
    int8_t idx_delta = kAdpcmIndexTable[nibble & 7u];
    int32_t new_si   = (int32_t)si + idx_delta;
    if (new_si < 0)   new_si = 0;
    if (new_si > 88)  new_si = 88;
    step_idx = (uint32_t)new_si;
    return pred;
}

// ---------------------------------------------------------------------------
// Envelope helpers
// ---------------------------------------------------------------------------

static inline void opl4_env_update(Opl4Channel& ch, uint32_t dl_thresh)
{
    switch (ch.env_phase) {
    case Opl4EnvPhase::ATTACK:
        if (ch.ar == 0u) break;
        if (ch.env_level <= kAttStep[ch.ar]) {
            ch.env_level = 0u;
            ch.env_phase = Opl4EnvPhase::DECAY1;
        } else {
            ch.env_level -= kAttStep[ch.ar];
        }
        break;

    case Opl4EnvPhase::DECAY1:
        if (ch.d1r == 0u) break;
        ch.env_level += kEnvStep[ch.d1r];
        if (ch.env_level >= dl_thresh) {
            ch.env_level = dl_thresh;
            ch.env_phase = Opl4EnvPhase::DECAY2;
        }
        break;

    case Opl4EnvPhase::DECAY2:
        if (ch.d2r == 0u) break;
        ch.env_level += kEnvStep[ch.d2r];
        if (ch.env_level >= OPL4_ENV_MAX) {
            ch.env_level = OPL4_ENV_MAX;
            ch.env_phase = Opl4EnvPhase::OFF;
        }
        break;

    case Opl4EnvPhase::RELEASE:
        if (ch.rr == 0u) break;
        ch.env_level += kEnvStep[ch.rr];
        if (ch.env_level >= OPL4_ENV_MAX) {
            ch.env_level = OPL4_ENV_MAX;
            ch.env_phase = Opl4EnvPhase::OFF;
        }
        break;

    case Opl4EnvPhase::OFF:
    default:
        break;
    }
    if (ch.env_level > OPL4_ENV_MAX) ch.env_level = OPL4_ENV_MAX;
}

// ---------------------------------------------------------------------------
// Register decode helpers
// ---------------------------------------------------------------------------

static void opl4_update_channel(Opl4State& s, uint8_t reg, uint8_t val)
{
    // Per-channel registers are at offsets 0x08, 0x20, 0x38, 0x50, 0x68,
    // 0x80, 0x98, 0xB0, 0xC8 — each block holds 24 channels (0..23).
    // Channels 24..31 of each block are writes to registers 0x08..0x1F
    // when n=0..23, so we gate on n < 24.

    uint8_t n = 0;
    uint8_t bank = 0;

    if (reg >= 0x08u && reg < 0x20u) { bank = 0; n = reg - 0x08u; }
    else if (reg >= 0x20u && reg < 0x38u) { bank = 1; n = reg - 0x20u; }
    else if (reg >= 0x38u && reg < 0x50u) { bank = 2; n = reg - 0x38u; }
    else if (reg >= 0x50u && reg < 0x68u) { bank = 3; n = reg - 0x50u; }
    else if (reg >= 0x68u && reg < 0x80u) { bank = 4; n = reg - 0x68u; }
    else if (reg >= 0x80u && reg < 0x98u) { bank = 5; n = reg - 0x80u; }
    else if (reg >= 0x98u && reg < 0xB0u) { bank = 6; n = reg - 0x98u; }
    else if (reg >= 0xB0u && reg < 0xC8u) { bank = 7; n = reg - 0xB0u; }
    else if (reg >= 0xC8u && reg < 0xE0u) { bank = 8; n = reg - 0xC8u; }
    else return;  // global or reserved

    if (n >= 24u) return;  // slot 24..31 of each block unused
    Opl4Channel& ch = s.channels[n];

    switch (bank) {
    case 0:  // 0x08+n: WAVE[7:0]
        ch.wave_num = (ch.wave_num & 0x300u) | val;
        break;
    case 1:  // 0x20+n: [3:2]=WAVE[9:8], [1:0]=FN[9:8]
        ch.wave_num = (uint16_t)((ch.wave_num & 0x0FFu) | (((uint16_t)(val >> 2) & 0x03u) << 8));
        ch.fnum     = (uint16_t)((ch.fnum     & 0x0FFu) | (((uint16_t)(val     ) & 0x03u) << 8));
        break;
    case 2:  // 0x38+n: FN[7:0]
        ch.fnum = (uint16_t)((ch.fnum & 0x300u) | val);
        break;
    case 3: {  // 0x50+n: [7]=KEYON, [6:3]=OCT
        ch.oct = (val >> 3) & 0x0Fu;
        bool new_keyon = (val >> 7) & 1u;
        if (new_keyon && !ch.keyon) {
            ch.keyon_event = true;  // rising edge → trigger attack
        } else if (!new_keyon && ch.keyon) {
            ch.env_phase = Opl4EnvPhase::RELEASE;  // falling edge → release
        }
        ch.keyon = new_keyon;
        break;
    }
    case 4:  // 0x68+n: [7:4]=AR, [3]=AM, [2]=VIB, [1:0]=LFO
        ch.ar  = (val >> 4) & 0x0Fu;
        ch.am  = (val >> 3) & 0x01u;
        ch.vib = (val >> 2) & 0x01u;
        ch.lfo = val & 0x03u;
        break;
    case 5:  // 0x80+n: [7:4]=D1R, [3:0]=DL
        ch.d1r = (val >> 4) & 0x0Fu;
        ch.dl  = val & 0x0Fu;
        break;
    case 6:  // 0x98+n: [7:4]=D2R, [3:0]=RR
        ch.d2r = (val >> 4) & 0x0Fu;
        ch.rr  = val & 0x0Fu;
        break;
    case 7:  // 0xB0+n: [7]=LD, [6:0]=TL
        ch.ld = (val >> 7) & 1u;
        ch.tl = val & 0x7Fu;
        break;
    case 8:  // 0xC8+n: [7:4]=PAN
        ch.pan = (val >> 4) & 0x0Fu;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Bus callbacks
// ---------------------------------------------------------------------------

// Port 0xF6: wave section register address latch.
static std::pair<bool, uint8_t> RAMFUNC(opl4_wave_addr_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s = *reinterpret_cast<Opl4State*>(c.ram_base);
    s.wave_addr  = (uint8_t)(bus >> GPIO_D0);
    return {false, 0};
}

// Port 0xF7 write: wave section data write.
static std::pair<bool, uint8_t> RAMFUNC(opl4_wave_data_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s  = *reinterpret_cast<Opl4State*>(c.ram_base);
    uint8_t reg   = s.wave_addr;
    uint8_t val   = (uint8_t)(bus >> GPIO_D0);
    s.wave_regs[reg] = val;

    if (reg == 0x00u) {
        // LFO speed — reset accumulator on speed change (prevents phase glitch)
        s.pcm_lfo_acc = 0u;
    } else if (reg == 0x02u) {
        s.mem_config = val & 0x03u;
    } else {
        opl4_update_channel(s, reg, val);
    }
    return {false, 0};
}

// Port 0xF7 read: wave section data read.
static std::pair<bool, uint8_t> RAMFUNC(opl4_wave_data_read)(Cartridge& c, uint32_t /*bus*/)
{
    Opl4State& s = *reinterpret_cast<Opl4State*>(c.ram_base);
    return {true, s.wave_regs[s.wave_addr]};
}

// Port 0x7E write: OPL3 primary register address.
static std::pair<bool, uint8_t> RAMFUNC(opl4_opl3_addr_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s       = *reinterpret_cast<Opl4State*>(c.ram_base);
    s.opl3_addr_primary = (uint8_t)(bus >> GPIO_D0);
    return {false, 0};
}

// Port 0x7F write: OPL3 primary data write.
static std::pair<bool, uint8_t> RAMFUNC(opl4_opl3_data_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s  = *reinterpret_cast<Opl4State*>(c.ram_base);
    uint8_t val   = (uint8_t)(bus >> GPIO_D0);
    s.opl3_regs[s.opl3_addr_primary] = val;
    opl3_write_reg(s.opl3, (uint16_t)s.opl3_addr_primary, val);
    return {false, 0};
}

// Port 0xC4 write: OPL3 secondary register address.
static std::pair<bool, uint8_t> RAMFUNC(opl4_opl3_sec_addr_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s        = *reinterpret_cast<Opl4State*>(c.ram_base);
    s.opl3_addr_secondary = (uint8_t)(bus >> GPIO_D0);
    return {false, 0};
}

// Port 0xC5 write: OPL3 secondary data write.
static std::pair<bool, uint8_t> RAMFUNC(opl4_opl3_sec_data_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s = *reinterpret_cast<Opl4State*>(c.ram_base);
    uint8_t val  = (uint8_t)(bus >> GPIO_D0);
    s.opl3_regs[0x100u + s.opl3_addr_secondary] = val;
    opl3_write_reg(s.opl3, (uint16_t)(0x100u | s.opl3_addr_secondary), val);
    return {false, 0};
}

// Port 0x7F read: OPL3 status / register readback.
// Returns STATUS byte: bit7=IRQ1, bit6=IRQ2, bit5=BUF_RDY (always 1 here).
static std::pair<bool, uint8_t> RAMFUNC(opl4_opl3_data_read)(Cartridge& c, uint32_t /*bus*/)
{
    Opl4State& s = *reinterpret_cast<Opl4State*>(c.ram_base);
    return {true, s.opl3_regs[s.opl3_addr_primary]};
}

// Port 0xF5 write: memory configuration / access port.
static std::pair<bool, uint8_t> RAMFUNC(opl4_mem_config_write)(Cartridge& c, uint32_t bus)
{
    Opl4State& s = *reinterpret_cast<Opl4State*>(c.ram_base);
    s.mem_config = (uint8_t)(bus >> GPIO_D0) & 0x03u;
    return {false, 0};
}

// ---------------------------------------------------------------------------
// opl4_reset / opl4_setup
// ---------------------------------------------------------------------------

void opl4_reset(Opl4State& state)
{
    memset(&state, 0, sizeof(state));
    // All PCM channels start silent (envelope = OFF, full attenuation).
    for (int i = 0; i < 24; ++i) {
        state.channels[i].env_phase = Opl4EnvPhase::OFF;
        state.channels[i].env_level = OPL4_ENV_MAX;
        state.channels[i].tl        = 0x7Fu;  // maximum attenuation at reset
    }
    // FM section reset (also initialises log-sin/pow2 tables on first call).
    opl3_reset(state.opl3);
}

void opl4_setup(Cartridge& c, Opl4State& state,
                const uint8_t* wave_rom, uint32_t wave_rom_size)
{
    state.wave_rom      = wave_rom;
    state.wave_rom_size = wave_rom_size;

    c.clear();
    c.name     = "opl4";
    c.ram_base = reinterpret_cast<uint8_t*>(&state);

    // OPL3 FM section ports (primary bank).
    c.io_write_callbacks[0x7Eu] = opl4_opl3_addr_write;
    c.io_write_callbacks[0x7Fu] = opl4_opl3_data_write;
    c.io_read_callbacks [0x7Fu] = opl4_opl3_data_read;

    // OPL3 FM section ports (secondary bank — Moonsound ports 0xC4/0xC5).
    c.io_write_callbacks[0xC4u] = opl4_opl3_sec_addr_write;
    c.io_write_callbacks[0xC5u] = opl4_opl3_sec_data_write;

    // Wave/PCM section ports.
    c.io_write_callbacks[0xF5u] = opl4_mem_config_write;
    c.io_write_callbacks[0xF6u] = opl4_wave_addr_write;
    c.io_write_callbacks[0xF7u] = opl4_wave_data_write;
    c.io_read_callbacks [0xF7u] = opl4_wave_data_read;
}

// ---------------------------------------------------------------------------
// opl4_compute_sample
// ---------------------------------------------------------------------------

uint8_t opl4_compute_sample(Opl4State& state)
{
    // ---- Global PCM LFO update ----
    // Speed register is wave_regs[0x00] bits [2:0].
    uint8_t lfo_speed = state.wave_regs[0x00u] & 0x07u;
    state.pcm_lfo_acc += kPcmLfoInc[lfo_speed];
    if (state.pcm_lfo_acc >= 65536u) state.pcm_lfo_acc -= 65536u;

    // Triangular LFO wave: lfo_tri goes 0→255→0 over one cycle.
    uint8_t lfo_tri = (uint8_t)(state.pcm_lfo_acc >> 8);
    if (state.pcm_lfo_acc & 0x8000u) lfo_tri = 255u - lfo_tri;

    // ---- PCM channel mix (stereo L/R separate accumulators) ----
    int32_t pcm_l = 0;
    int32_t pcm_r = 0;

    for (int i = 0; i < 24; ++i) {
        Opl4Channel& ch = state.channels[i];

        // Handle key-on event (rising edge sets keyon_event flag).
        if (ch.keyon_event) {
            ch.keyon_event = false;
            ch.phase_acc   = 0u;
            ch.env_phase   = Opl4EnvPhase::ATTACK;
            ch.env_level   = OPL4_ENV_MAX;  // start from silence, attack ramps down
            ch.adpcm_step  = 0u;
            ch.adpcm_pred  = 0;
            ch.adpcm_nibble_high = false;
        }

        if (ch.env_phase == Opl4EnvPhase::OFF) continue;

        // Parse wave descriptor from ROM.
        Opl4WaveDesc desc = {};
        if (!opl4_parse_wave_desc(state.wave_rom, state.wave_rom_size,
                                   ch.wave_num, desc)) {
            ch.env_phase = Opl4EnvPhase::OFF;
            continue;
        }

        // Base step: FN × 2^(oct_signed + 7), oct_signed = OCT - 8.
        int32_t oct_signed = (int32_t)ch.oct - 8;
        uint32_t step_fp16;
        int32_t shift = oct_signed + 7;
        if (shift >= 0) {
            step_fp16 = (uint32_t)ch.fnum << shift;
        } else {
            step_fp16 = (uint32_t)ch.fnum >> (-shift);
        }
        if (step_fp16 == 0u) step_fp16 = 1u;

        // VIB (vibrato): modulate step by LFO triangular wave.
        if (ch.vib && kVibDepth[ch.lfo] != 0) {
            // lfo_tri is 0..255 triangle. Convert to ±127 centred.
            int32_t signed_tri = (int32_t)lfo_tri - 128;
            int32_t delta = ((int32_t)step_fp16 * signed_tri * kVibDepth[ch.lfo]) >> 17;
            step_fp16 = (uint32_t)((int32_t)step_fp16 + delta);
            if (step_fp16 == 0u) step_fp16 = 1u;
        }

        // Advance phase accumulator.
        ch.phase_acc += step_fp16;

        // Compute sample index and fractional part for interpolation.
        uint32_t sample_idx = ch.phase_acc >> 16;
        uint32_t frac       = ch.phase_acc & 0xFFFFu;  // Q16 fractional part

        // Handle loop / end.
        if (desc.loop_end > 0u && sample_idx >= desc.loop_end) {
            uint32_t loop_len = desc.loop_end - desc.loop_start;
            if (loop_len == 0u) loop_len = 1u;
            sample_idx = desc.loop_start + (sample_idx - desc.loop_end) % loop_len;
            ch.phase_acc = (sample_idx << 16) | frac;
        }

        // Read wave sample with linear interpolation between adjacent samples.
        int16_t raw_sample;
        if (desc.format == 3u) {
            // ADPCM: fractional interpolation not meaningful, use direct decode.
            uint32_t byte_addr = desc.start_addr + sample_idx / 2u;
            if (state.wave_rom && byte_addr < state.wave_rom_size) {
                uint8_t byte = state.wave_rom[byte_addr];
                uint8_t nibble = ((sample_idx & 1u) == 0u)
                                 ? (byte >> 4) & 0x0Fu
                                 : byte & 0x0Fu;
                raw_sample = adpcm_decode_nibble(nibble, ch.adpcm_pred, ch.adpcm_step);
            } else {
                raw_sample = 0;
            }
        } else {
            // PCM with linear interpolation.
            uint32_t byte_addr0 = (desc.format == 2u)
                ? desc.start_addr + sample_idx * 2u
                : desc.start_addr + sample_idx;
            int16_t s0 = read_wave_sample(state.wave_rom, state.wave_rom_size,
                                           desc.format, byte_addr0);
            // Next sample index (clamped or looped).
            uint32_t next_idx = sample_idx + 1u;
            if (desc.loop_end > 0u && next_idx >= desc.loop_end) {
                next_idx = desc.loop_start;
            }
            uint32_t byte_addr1 = (desc.format == 2u)
                ? desc.start_addr + next_idx * 2u
                : desc.start_addr + next_idx;
            int16_t s1 = read_wave_sample(state.wave_rom, state.wave_rom_size,
                                           desc.format, byte_addr1);
            // Lerp: raw = s0 + (s1 - s0) * frac / 65536
            raw_sample = (int16_t)((int32_t)s0 +
                         (((int32_t)s1 - (int32_t)s0) * (int32_t)frac >> 16));
        }

        // Update envelope.
        uint32_t dl_thresh = (uint32_t)ch.dl * OPL4_ENV_SCALE;
        if (dl_thresh > OPL4_ENV_MAX) dl_thresh = OPL4_ENV_MAX;
        opl4_env_update(ch, dl_thresh);

        // AM (tremolo): add LFO-driven attenuation offset.
        uint32_t am_extra = 0u;
        if (ch.am && kAmDepth[ch.lfo] > 0u) {
            // lfo_tri 0..255 → attenuation 0..am_depth
            am_extra = ((uint32_t)lfo_tri * kAmDepth[ch.lfo]) >> 8;
        }

        // Total attenuation: env_level + TL*8 + AM, capped at OPL4_ENV_MAX.
        uint32_t attenuation = ch.ld
            ? ((uint32_t)ch.tl * 8u + am_extra)
            : (ch.env_level + (uint32_t)ch.tl * 8u + am_extra);
        if (attenuation > OPL4_ENV_MAX) attenuation = OPL4_ENV_MAX;

        // Scale sample (raw_sample ≈ ±32768; shift right 8 → ±127 scale).
        int32_t scaled = ((int32_t)(raw_sample >> 8) *
                          (int32_t)(OPL4_ENV_MAX - attenuation)) / (int32_t)OPL4_ENV_MAX;

        // Stereo pan: pan 0=hard-L, 7=centre, 8=centre, 15=hard-R.
        // For panning, derive L/R gain in [0, 256].
        // pan=0: L=256,R=0; pan=7/8: L=256,R=256; pan=15: L=0,R=256.
        uint8_t pan = ch.pan;
        int32_t l_gain = (pan <= 7u)  ? 256  : ((int32_t)(15u - pan) * 256 / 7);
        int32_t r_gain = (pan >= 8u)  ? 256  : ((int32_t)pan * 256 / 7);
        pcm_l += (scaled * l_gain) >> 8;
        pcm_r += (scaled * r_gain) >> 8;
    }

    // ---- FM synthesis (OPL3) ----
    int16_t fm_l = 0;
    int16_t fm_r = 0;
    opl3_compute_sample(state.opl3, fm_l, fm_r);

    // ---- Mix PCM + FM to mono output ----
    // PCM: 24 channels each ±127 → sum /24 → ±127.
    // FM: 18 channels each ±512 → sum /36 → ±256 max; scale down to ±128.
    //     Use /32 (≈ /36) for slightly warmer FM level.
    int32_t pcm_mono = (pcm_l + pcm_r) / (24 * 2);
    int32_t fm_mono  = ((int32_t)fm_l + (int32_t)fm_r) / (32 * 2);

    int32_t output = 128 + pcm_mono + fm_mono;
    if (output < 0)   output = 0;
    if (output > 255) output = 255;
    return (uint8_t)output;
}

// ---------------------------------------------------------------------------
// Hardware audio (firmware only)
// ---------------------------------------------------------------------------

#ifndef JLPICART_HOST_TEST

volatile uint8_t g_opl4_sample = 128u;

void opl4_audio_init(Opl4State& state) {
    // PSG already configured the PWM on GPIO64_SND.
    (void)state;
    state.audio_initialized = true;
}

void opl4_service(Opl4State& state) {
    if (!state.audio_initialized) return;
    g_opl4_sample = opl4_compute_sample(state);
}

#else

void opl4_audio_init(Opl4State& state) { (void)state; }
void opl4_service(Opl4State& state)    { (void)state; }

#endif // JLPICART_HOST_TEST
