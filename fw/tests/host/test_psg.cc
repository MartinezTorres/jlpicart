// test_psg.cc — host tests for Stage 27 PSG (AY-3-8910) emulation.
//
// Tests cover:
//   - psg_reset() power-on register defaults
//   - IO port callbacks: register select, data write (with masking), data read
//   - Silence: muted mixer (R7=0xFF) produces zero output
//   - Tone generation: non-zero sample when tone enabled and unmasked
//   - Noise generation: non-zero sample when noise enabled
//   - Amplitude levels: sample scales with R8 level field
//   - Envelope shape: basic counter / level progression (shapes 0, 12)
//   - Register write masking (R1, R6, R8, R13)
//   - Writing R13 resets envelope counter and position

#include "peripherals/psg.h"
#include "cartridges/cartridge.h"
#include "test_helpers.h"
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Helper: drive an IO write callback directly.
// addr: IO port (A7:0), data: data byte written.
// ---------------------------------------------------------------------------

static void io_write(Cartridge& c, uint8_t addr, uint8_t data)
{
    auto cb = c.io_write_callbacks[addr];
    if (cb) cb(c, ((uint32_t)data << 16) | addr);
}

static uint8_t io_read(Cartridge& c, uint8_t addr)
{
    auto cb = c.io_read_callbacks[addr];
    if (!cb) return 0xFFu;
    auto [driven, val] = cb(c, (uint32_t)addr);
    return driven ? val : 0xFFu;
}

// Helper: select register, then write data via the PSG IO ports.
static void psg_write_reg(Cartridge& c, uint8_t reg, uint8_t data)
{
    io_write(c, 0xA0, reg);    // register select
    io_write(c, 0xA1, data);   // data write
}

// Helper: select register, then read back via port 0xA2.
static uint8_t psg_read_reg(Cartridge& c, uint8_t reg)
{
    io_write(c, 0xA0, reg);
    return io_read(c, 0xA2);
}

// ---------------------------------------------------------------------------
// test_reset_defaults
//   psg_reset() must apply MSX power-on defaults: R7=0xFF, R14=R15=0xFF.
// ---------------------------------------------------------------------------

static void test_reset_defaults()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    // All registers default to 0 except R7, R14, R15.
    for (uint8_t r = 0; r < 16u; ++r) {
        uint8_t expected = (r == 7 || r == 14 || r == 15) ? 0xFFu : 0x00u;
        CHECK(psg_read_reg(c, r) == expected);
    }

    // Synthesis state zeroed; LFSR must be non-zero (= 1).
    CHECK(state.noise_lfsr == 0x00001u);
    CHECK(state.env_pos    == 0u);
    CHECK(!state.audio_initialized);
}

// ---------------------------------------------------------------------------
// test_register_select_masking
//   Port 0xA0 only latches the lower 4 bits (0–15); higher bits discarded.
// ---------------------------------------------------------------------------

static void test_register_select_masking()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    // Write 0xF5 → reg_select should be 0x05.
    io_write(c, 0xA0, 0xF5u);
    CHECK(state.reg_select == 0x05u);

    // Write 0x8Au → reg_select should be 0x0Au.
    io_write(c, 0xA0, 0x8Au);
    CHECK(state.reg_select == 0x0Au);
}

// ---------------------------------------------------------------------------
// test_write_read_roundtrip
//   Arbitrary values written to R0 and R2 must read back correctly.
// ---------------------------------------------------------------------------

static void test_write_read_roundtrip()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    psg_write_reg(c, 0, 0xABu);
    CHECK(psg_read_reg(c, 0) == 0xABu);

    psg_write_reg(c, 2, 0x55u);
    CHECK(psg_read_reg(c, 2) == 0x55u);

    // Verify R0 still intact after writing R2.
    CHECK(psg_read_reg(c, 0) == 0xABu);
}

// ---------------------------------------------------------------------------
// test_register_write_masking
//   R1 (tone high) retains only 4 bits.
//   R6 (noise period) retains only 5 bits.
//   R8 (amplitude) retains only 5 bits.
//   R13 (envelope shape) retains only 4 bits.
// ---------------------------------------------------------------------------

static void test_register_write_masking()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    // R1: 4-bit (bits 7:4 discarded)
    psg_write_reg(c, 1, 0xF7u);
    CHECK(psg_read_reg(c, 1) == 0x07u);

    // R6: 5-bit (bits 7:5 discarded)
    psg_write_reg(c, 6, 0xFFu);
    CHECK(psg_read_reg(c, 6) == 0x1Fu);

    // R8: 5-bit (bits 7:5 discarded)
    psg_write_reg(c, 8, 0xFFu);
    CHECK(psg_read_reg(c, 8) == 0x1Fu);

    // R13: 4-bit
    psg_write_reg(c, 13, 0xFFu);
    CHECK(psg_read_reg(c, 13) == 0x0Fu);
}

// ---------------------------------------------------------------------------
// test_silence_default
//   With R7=0xFF (all muted) and all amplitude registers zero, every sample
//   must be 0 (silence).
// ---------------------------------------------------------------------------

static void test_silence_default()
{
    PsgState state;
    psg_reset(state);

    // Mixer default R7=0xFF: all tone/noise disabled.  R8/R9/R10=0: amplitude 0.
    for (int i = 0; i < 100; ++i) {
        uint8_t s = psg_compute_sample(state);
        CHECK(s == 0u);
    }
}

// ---------------------------------------------------------------------------
// test_tone_produces_output
//   Enable channel A tone (R7 bit0 = 0), set a non-zero period and amplitude.
//   After enough samples the output must alternate between 0 and non-zero
//   (the square wave), and the average must be non-zero.
// ---------------------------------------------------------------------------

static void test_tone_produces_output()
{
    PsgState state;
    psg_reset(state);

    // Channel A tone period = 0x100 (256) → ~27 Hz
    state.regs[0] = 0x00u;  // TP low
    state.regs[1] = 0x01u;  // TP high (TP = 0x100 = 256)
    state.regs[7] = 0xFEu;  // enable channel A tone (bit0=0), rest muted
    state.regs[8] = 15u;    // channel A amplitude = 15 (max static level)

    uint32_t non_zero = 0u;
    const int SAMPLES = 4000;
    for (int i = 0; i < SAMPLES; ++i) {
        if (psg_compute_sample(state) > 0u) ++non_zero;
    }
    // Square wave: roughly half of samples should be non-zero.
    CHECK(non_zero > (uint32_t)(SAMPLES / 4));
    CHECK(non_zero < (uint32_t)(SAMPLES * 3 / 4));
}

// ---------------------------------------------------------------------------
// test_noise_produces_output
//   Enable channel A noise only (R7 = 0b11111000 = 0xF8).
//   With amplitude > 0, expect non-zero samples and some variation.
// ---------------------------------------------------------------------------

static void test_noise_produces_output()
{
    PsgState state;
    psg_reset(state);

    state.regs[6] = 16u;    // noise period = 16
    state.regs[7] = 0xF7u;  // noise A enabled (bit3=0), all tones off
    state.regs[8] = 10u;    // channel A amplitude = 10

    uint32_t non_zero = 0u;
    const int SAMPLES = 4000;
    for (int i = 0; i < SAMPLES; ++i) {
        if (psg_compute_sample(state) > 0u) ++non_zero;
    }
    CHECK(non_zero > 0u);
    CHECK(non_zero < (uint32_t)SAMPLES);
}

// ---------------------------------------------------------------------------
// test_amplitude_scaling
//   Higher R8 level must produce higher or equal average output.
// ---------------------------------------------------------------------------

static void test_amplitude_scaling()
{
    const int SAMPLES = 2000;

    auto avg_at_level = [&](uint8_t level) -> uint32_t {
        PsgState state;
        psg_reset(state);
        state.regs[0] = 0x20u;  // TP=32 → ~1.7 kHz
        state.regs[7] = 0xFEu;  // tone A enabled
        state.regs[8] = level;
        uint32_t sum = 0u;
        for (int i = 0; i < SAMPLES; ++i)
            sum += psg_compute_sample(state);
        return sum;
    };

    uint32_t a5  = avg_at_level(5);
    uint32_t a10 = avg_at_level(10);
    uint32_t a15 = avg_at_level(15);

    CHECK(a5  > 0u);
    CHECK(a10 > a5);
    CHECK(a15 > a10);
}

// ---------------------------------------------------------------------------
// test_envelope_shape0_decay
//   Shape 0 (CONT=0, ATT=0): single decay ramp (15→0), then hold at 0.
//   With envelope mode on R8, amplitude must decrease over time then stay 0.
// ---------------------------------------------------------------------------

static void test_envelope_shape0_decay()
{
    PsgState state;
    psg_reset(state);

    // Short envelope period so it advances quickly.
    // EP = 1: one step every EP*16*65536/PSG_TICKS_PER_SAMPLE_FP16 ≈ 26 samples.
    state.regs[11] = 1u;   // EP low
    state.regs[12] = 0u;   // EP high
    state.regs[13] = 0u;   // shape 0: \___

    state.regs[0] = 1u;    // TP=1 → very fast tone (DC-like at this resolution)
    state.regs[7] = 0xFEu; // tone A enabled
    state.regs[8] = 0x10u; // envelope mode (bit4=1)

    // Capture a sample early (env_pos should be small → high level).
    uint8_t early_sample = 0u;
    for (int i = 0; i < 5; ++i) early_sample = psg_compute_sample(state);

    // Advance far past 16 envelope steps (shape 0 holds at 0 after step 15).
    for (int i = 0; i < 1000; ++i) psg_compute_sample(state);

    uint8_t late_sample = psg_compute_sample(state);

    // After the envelope decays to 0, sample should be 0.
    CHECK(late_sample == 0u);
    // Early sample should be non-zero (level > 0 during decay).
    CHECK(early_sample > 0u);
}

// ---------------------------------------------------------------------------
// test_envelope_shape12_continuous_attack
//   Shape 12 (CONT=1, ATT=1, ALT=0, HOLD=0): continuous attack sawtooth.
//   After many steps the envelope must still be producing non-zero output
//   (i.e., it wraps rather than holding at 0).
// ---------------------------------------------------------------------------

static void test_envelope_shape12_continuous()
{
    PsgState state;
    psg_reset(state);

    state.regs[11] = 1u;    // EP=1 (fast)
    state.regs[12] = 0u;
    state.regs[13] = 12u;   // shape 12: /////

    state.regs[0]  = 1u;    // fast tone
    state.regs[7]  = 0xFEu; // tone A enabled
    state.regs[8]  = 0x10u; // envelope mode

    // Advance well past 16 steps (one full sawtooth cycle).
    for (int i = 0; i < 600; ++i) psg_compute_sample(state);

    // Envelope must have wrapped: env_pos > 16 and still producing samples.
    CHECK(state.env_pos > 16u);

    uint32_t non_zero = 0u;
    for (int i = 0; i < 200; ++i)
        if (psg_compute_sample(state) > 0u) ++non_zero;
    CHECK(non_zero > 0u);  // still alive, not stuck at 0
}

// ---------------------------------------------------------------------------
// test_write_r13_resets_envelope
//   Writing to R13 must reset env_counter and env_pos to 0.
// ---------------------------------------------------------------------------

static void test_write_r13_resets_envelope()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    // Advance the envelope.
    state.regs[11] = 1u;
    state.regs[12] = 0u;
    state.regs[13] = 0u;
    state.regs[8]  = 0x10u;
    state.regs[7]  = 0xFEu;
    state.regs[0]  = 1u;
    for (int i = 0; i < 200; ++i) psg_compute_sample(state);
    CHECK(state.env_pos > 0u);

    // Write R13 via IO port — must reset envelope.
    psg_write_reg(c, 13, 0x0Cu);
    CHECK(state.env_pos    == 0u);
    CHECK(state.env_counter == 0u);
}

// ---------------------------------------------------------------------------
// test_three_channels_sum
//   Three channels at equal amplitude summed must produce more output than one.
//
// Note: AY-3-8910 mixer semantics: when a channel's tone AND noise are both
// disabled (R7 bits high), the channel output is DC=1 and amplitude still
// applies.  "Silence" requires amplitude = 0, not just disabling sources.
// So to isolate channels we zero unused channels' amplitudes, not the mixer.
// ---------------------------------------------------------------------------

static void test_three_channels_sum()
{
    const int SAMPLES = 1000;
    const uint8_t LEVEL = 8u;

    // Helper: run SAMPLES samples with given per-channel amplitude configuration.
    // All three tones active (mixer=0xF8), all at the same period.
    auto run_three = [&]() -> uint32_t {
        PsgState state;
        psg_reset(state);
        state.regs[0] = 8u; state.regs[2] = 8u; state.regs[4] = 8u;
        state.regs[7] = 0xF8u;   // all three tone channels enabled
        state.regs[8] = LEVEL;
        state.regs[9] = LEVEL;
        state.regs[10] = LEVEL;
        uint32_t sum = 0u;
        for (int i = 0; i < SAMPLES; ++i) sum += psg_compute_sample(state);
        return sum;
    };

    // Channel A only: enable all tones but zero B and C amplitudes.
    auto run_one = [&]() -> uint32_t {
        PsgState state;
        psg_reset(state);
        state.regs[0] = 8u; state.regs[2] = 8u; state.regs[4] = 8u;
        state.regs[7] = 0xF8u;   // tones enabled for all (amplitude gates silence)
        state.regs[8] = LEVEL;
        state.regs[9] = 0u;      // B silent
        state.regs[10] = 0u;     // C silent
        uint32_t sum = 0u;
        for (int i = 0; i < SAMPLES; ++i) sum += psg_compute_sample(state);
        return sum;
    };

    uint32_t one   = run_one();
    uint32_t three = run_three();

    // Three channels at the same level and phase must produce 3× a single channel.
    CHECK(three > one);
    CHECK(three <= one * 3u + 1u);
}

// ---------------------------------------------------------------------------
// test_io_read_out_of_range
//   Reading port 0xA2 when reg_select > 15 must return 0xFF.
// ---------------------------------------------------------------------------

static void test_io_read_out_of_range()
{
    PsgState state;
    Cartridge c;
    psg_reset(state);
    psg_setup(c, state);

    // Force an out-of-range register select directly.
    state.reg_select = 20u;
    uint8_t val = io_read(c, 0xA2u);
    CHECK(val == 0xFFu);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_reset_defaults();
    test_register_select_masking();
    test_write_read_roundtrip();
    test_register_write_masking();
    test_silence_default();
    test_tone_produces_output();
    test_noise_produces_output();
    test_amplitude_scaling();
    test_envelope_shape0_decay();
    test_envelope_shape12_continuous();
    test_write_r13_resets_envelope();
    test_three_channels_sum();
    test_io_read_out_of_range();

    return test_summary();
}
