// psg.cc — AY-3-8910 / YM2149 PSG emulation.
//
// See psg.h for design notes and register map.
//
// Audio model (per audio sample, ~44100 Hz):
//   1. Advance tone counters (Q16 fixed-point, threshold = TP * 16 * 65536).
//   2. Advance noise counter (same model; clock LFSR on toggle).
//   3. Advance envelope counter (uint64_t; step threshold = EP * 16 * 65536).
//   4. Mix channels: sample = sum(amplitude[ch]) for active channels, clamped.
//
// PWM (hardware only):
//   GPIO64_SND = GPIO 33 → RP2350 PWM slice 4, channel B.
//   Carrier: 125 MHz / 256 = 488 kHz.  Duty updated at ~44100 Hz from Core 1.

#include "peripherals/psg.h"
#include "peripherals/scc.h"
#include "peripherals/opl4.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#  include "boards/gpio_defs.h"
#  include "hardware/gpio.h"
#  include "hardware/pwm.h"
#  include "pico/time.h"

// Shared audio outputs from SCC and OPL4 service ticks (written by their
// respective service functions on Core 1, read here for mixing).
extern volatile uint8_t g_scc_sample;
extern volatile uint8_t g_opl4_sample;
#endif

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// PSG master clock / audio sample rate, in Q16 fixed-point.
// = (1789773 / 44100) * 65536 ≈ 40.585 * 65536 = 2660413.
static constexpr uint32_t PSG_TICKS_PER_SAMPLE_FP16 = 2660413u;

// Interval between audio samples in microseconds (~44100 Hz).
static constexpr uint32_t PSG_SAMPLE_INTERVAL_US = 22u;

// Logarithmic amplitude table for 16 PSG volume levels (0–15).
// Max contribution per channel = 85; 3 × 85 = 255 (fits uint8_t).
static const uint8_t kAmplitude[16] = {
    0, 1, 1, 2, 2, 3, 5, 6, 9, 13, 18, 26, 36, 51, 73, 85
};

// ---------------------------------------------------------------------------
// Envelope shape helper
// ---------------------------------------------------------------------------
//
// AY-3-8910 envelope shape table (R13 bits CONT=3, ATT=2, ALT=1, HOLD=0):
//
//  Shape  CONT ATT ALT HOLD  Pattern
//   0-3     0   0   x   x    \_____ (decay, hold 0)
//   4-7     0   1   x   x    /|____ (attack, hold 0)
//   8       1   0   0   0    \\\\   (continuous decay sawtooth)
//   9       1   0   0   1    \_____  (decay, hold 0)
//   10      1   0   1   0    \/\/   (alternating decay-attack)
//   11      1   0   1   1    \|^^^^ (decay, hold 15)
//   12      1   1   0   0    /////  (continuous attack sawtooth)
//   13      1   1   0   1    /|^^^^ (attack, hold 15)
//   14      1   1   1   0    /\/\   (alternating attack-decay)
//   15      1   1   1   1    /|____ (attack, hold 0)

static uint8_t psg_env_level(uint8_t shape, uint32_t pos)
{
    bool cont = (shape >> 3) & 1u;
    bool att  = (shape >> 2) & 1u;
    bool alt  = (shape >> 1) & 1u;
    bool hold = (shape >> 0) & 1u;

    if (!cont) {
        // Shapes 0–7: single ramp, then hold at 0.
        if (pos < 16u)
            return att ? (uint8_t)pos : (uint8_t)(15u - pos);
        return 0u;
    }

    uint32_t step  = pos & 15u;
    uint32_t cycle = pos >> 4u;

    if (hold) {
        // Shapes 9, 11, 13, 15: one ramp, then hold.
        // Hold level: att ^ alt selects 15 or 0.
        if (cycle == 0u)
            return att ? (uint8_t)step : (uint8_t)(15u - step);
        return (att ^ alt) ? 15u : 0u;
    }

    if (!alt) {
        // Shapes 8, 12: continuous sawtooth (same direction every cycle).
        return att ? (uint8_t)step : (uint8_t)(15u - step);
    }

    // Shapes 10, 14: alternating direction each cycle.
    // Even cycles start in the ATT-determined direction; odd cycles reverse.
    bool forward = att ^ ((cycle & 1u) != 0u);
    return forward ? (uint8_t)step : (uint8_t)(15u - step);
}

// ---------------------------------------------------------------------------
// Bus IO callbacks — run from SRAM on hardware (RAMFUNC).
// ---------------------------------------------------------------------------
//
// Bus word: address in GPIO_A0..A7 (bits 7:0), data in GPIO_D0..D7 (bits 23:16).

// Port 0xA0 write: latch register index (4-bit).
static std::pair<bool, uint8_t> RAMFUNC(psg_write_regselect)(Cartridge& c, uint32_t bus)
{
    uint8_t data = (uint8_t)(bus >> 16u);
    PsgState& s = *reinterpret_cast<PsgState*>(c.ram_base);
    s.reg_select = data & 0x0Fu;
    return {false, 0};
}

// Port 0xA1 write: write data byte to the selected register.
static std::pair<bool, uint8_t> RAMFUNC(psg_write_data)(Cartridge& c, uint32_t bus)
{
    uint8_t data = (uint8_t)(bus >> 16u);
    PsgState& s = *reinterpret_cast<PsgState*>(c.ram_base);
    uint8_t reg = s.reg_select;
    if (reg > 15u) return {false, 0};

    switch (reg) {
        // Tone period low (8-bit)
        case 0: case 2: case 4:
            s.regs[reg] = data;
            break;
        // Tone period high (4-bit)
        case 1: case 3: case 5:
            s.regs[reg] = data & 0x0Fu;
            break;
        // Noise period (5-bit)
        case 6:
            s.regs[reg] = data & 0x1Fu;
            break;
        // Mixer control (8-bit)
        case 7:
            s.regs[reg] = data;
            break;
        // Amplitude (5-bit: bit4=env_enable, bits3:0=level)
        case 8: case 9: case 10:
            s.regs[reg] = data & 0x1Fu;
            break;
        // Envelope period (8-bit each)
        case 11: case 12:
            s.regs[reg] = data;
            break;
        // Envelope shape (4-bit): writing resets the envelope.
        case 13:
            s.regs[reg] = data & 0x0Fu;
            s.env_counter = 0u;
            s.env_pos     = 0u;
            s.env_level   = psg_env_level(s.regs[13], 0u);
            break;
        // IO ports (8-bit, read-back as written)
        case 14: case 15:
            s.regs[reg] = data;
            break;
        default:
            break;
    }
    return {false, 0};
}

// Port 0xA2 read: return the value of the selected register.
static std::pair<bool, uint8_t> RAMFUNC(psg_read_data)(Cartridge& c, uint32_t /*bus*/)
{
    PsgState& s = *reinterpret_cast<PsgState*>(c.ram_base);
    uint8_t reg = s.reg_select;
    if (reg > 15u) return {true, 0xFFu};
    return {true, s.regs[reg]};
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void psg_reset(PsgState& state)
{
    // Zero synthesis state.
    state.tone_counter[0] = 0u;
    state.tone_counter[1] = 0u;
    state.tone_counter[2] = 0u;
    state.tone_output[0]  = 0u;
    state.tone_output[1]  = 0u;
    state.tone_output[2]  = 0u;
    state.noise_counter   = 0u;
    state.noise_lfsr      = 0x00001u;  // must be non-zero
    state.noise_output    = 0u;
    state.env_counter     = 0u;
    state.env_pos         = 0u;
    state.env_level       = 0u;
    state.audio_initialized = false;

    // Register power-on defaults.
    for (uint8_t i = 0; i < 16u; ++i) state.regs[i] = 0u;
    state.reg_select = 0u;
    state.regs[7]  = 0xFFu;   // all tone/noise muted; IO as inputs
    state.regs[14] = 0xFFu;   // no joystick connected (port A)
    state.regs[15] = 0xFFu;   // no joystick connected (port B)
}

void psg_setup(Cartridge& c, PsgState& state)
{
    c.clear();
    c.name     = "psg";
    c.ram_base = reinterpret_cast<uint8_t*>(&state);

    c.io_write_callbacks[0xA0] = psg_write_regselect;
    c.io_write_callbacks[0xA1] = psg_write_data;
    c.io_read_callbacks[0xA2]  = psg_read_data;
}

void psg_audio_init(PsgState& state)
{
#ifndef JLPICART_HOST_TEST
    if (state.audio_initialized) return;

    uint slice   = pwm_gpio_to_slice_num(GPIO64_SND);
    uint channel = pwm_gpio_to_channel(GPIO64_SND);

    gpio_set_function(GPIO64_SND, GPIO_FUNC_PWM);
    pwm_set_wrap(slice, 255u);
    pwm_set_clkdiv_int_frac4(slice, 1u, 0u);   // no prescale → 488 kHz carrier
    pwm_set_chan_level(slice, channel, 128u);    // 50% duty = silence
    pwm_set_enabled(slice, true);

    state.audio_initialized = true;
#else
    (void)state;
#endif
}

void psg_service(PsgState& state)
{
#ifndef JLPICART_HOST_TEST
    static uint64_t last_us = 0u;
    uint64_t now = time_us_64();
    if (now - last_us < PSG_SAMPLE_INTERVAL_US) return;
    last_us = now;

    // Mix PSG + SCC + OPL4. Each source is 0–255 (128 = silence).
    // Combine as signed offsets from 128, average, re-centre.
    int32_t mixed = (int32_t)psg_compute_sample(state) - 128
                  + (int32_t)g_scc_sample              - 128
                  + (int32_t)g_opl4_sample             - 128;
    mixed = 128 + mixed / 3;
    if (mixed < 0)   mixed = 0;
    if (mixed > 255) mixed = 255;

    uint slice   = pwm_gpio_to_slice_num(GPIO64_SND);
    uint channel = pwm_gpio_to_channel(GPIO64_SND);
    pwm_set_chan_level(slice, channel, (uint32_t)mixed);
#else
    (void)state;
#endif
}

uint8_t psg_compute_sample(PsgState& state)
{
    // ------------------------------------------------------------------
    // 1. Advance tone generators.
    //    Toggle threshold (Q16) = TP * 16 * 65536 = TP * 1048576.
    //    Use uint64_t intermediate to avoid overflow (TP_max=4095 →
    //    threshold ≈ 4.29 G, which barely fits uint32_t but
    //    threshold + advance may not).
    // ------------------------------------------------------------------
    for (uint8_t ch = 0u; ch < 3u; ++ch) {
        uint16_t tp = ((uint16_t)(state.regs[2u * ch + 1u] & 0x0Fu) << 8)
                    | state.regs[2u * ch];
        if (tp == 0u) continue;  // period=0 → DC, no oscillation

        uint64_t threshold = (uint64_t)tp * 1048576ull;
        uint64_t cnt = (uint64_t)state.tone_counter[ch] + PSG_TICKS_PER_SAMPLE_FP16;
        if (cnt >= threshold) {
            cnt -= threshold;
            state.tone_output[ch] ^= 1u;
        }
        state.tone_counter[ch] = (uint32_t)cnt;
    }

    // ------------------------------------------------------------------
    // 2. Advance noise generator.
    //    NP is 5-bit (R6[4:0]); NP=0 treated as 1.
    //    Clock the 17-bit LFSR: feedback = bit0 XOR bit3.
    // ------------------------------------------------------------------
    {
        uint8_t np = state.regs[6] & 0x1Fu;
        if (np == 0u) np = 1u;

        uint64_t threshold = (uint64_t)np * 1048576ull;
        uint64_t cnt = (uint64_t)state.noise_counter + PSG_TICKS_PER_SAMPLE_FP16;
        if (cnt >= threshold) {
            cnt -= threshold;
            uint32_t lfsr = state.noise_lfsr;
            if (lfsr == 0u) lfsr = 1u;
            uint32_t bit = (lfsr ^ (lfsr >> 3u)) & 1u;
            lfsr = ((lfsr >> 1u) | (bit << 16u)) & 0x1FFFFu;
            state.noise_lfsr   = lfsr;
            state.noise_output = (uint8_t)bit;
        }
        state.noise_counter = (uint32_t)cnt;
    }

    // ------------------------------------------------------------------
    // 3. Advance envelope generator.
    //    EP is 16-bit (R12:R11); EP=0 treated as 1.
    //    One envelope step = EP * 16 * 65536 PSG clocks (Q16).
    // ------------------------------------------------------------------
    {
        uint16_t ep = ((uint16_t)state.regs[12] << 8) | state.regs[11];
        if (ep == 0u) ep = 1u;

        uint64_t threshold = (uint64_t)ep * 1048576ull;
        state.env_counter += PSG_TICKS_PER_SAMPLE_FP16;
        if (state.env_counter >= threshold) {
            state.env_counter -= threshold;
            state.env_pos++;
            state.env_level = psg_env_level(state.regs[13], state.env_pos);
        }
    }

    // ------------------------------------------------------------------
    // 4. Mix channels.
    //    R7 mixer: bits 0-2 = tone enables (active-low), bits 3-5 = noise enables.
    //    R8/9/10: bit4 = envelope mode, bits3:0 = static level.
    // ------------------------------------------------------------------
    uint8_t mixer = state.regs[7];
    uint16_t mix  = 0u;

    for (uint8_t ch = 0u; ch < 3u; ++ch) {
        bool tone_en  = !(mixer & (1u << ch));
        bool noise_en = !(mixer & (1u << (ch + 3u)));

        // Channel output: start high; AND with each enabled source.
        // When both disabled, output is 1 (DC level → amplitude applies).
        uint8_t out = 1u;
        if (tone_en)  out &= state.tone_output[ch];
        if (noise_en) out &= state.noise_output;

        if (out) {
            uint8_t amp_reg = state.regs[8u + ch];
            uint8_t level;
            if (amp_reg & 0x10u) {
                level = state.env_level;          // envelope mode
            } else {
                level = amp_reg & 0x0Fu;          // static level
            }
            mix += kAmplitude[level];
        }
    }

    if (mix > 255u) mix = 255u;
    return (uint8_t)mix;
}
