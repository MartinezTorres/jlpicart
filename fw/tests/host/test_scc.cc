// test_scc.cc — Stage 29: SCC (Sound Creative Chip) emulation.
//
// Covers:
//   1. scc_reset() zeroes all state.
//   2. scc_write_register() correctly routes waveform / freq / vol / enable.
//   3. scc_compute_sample() produces silence (0 output = 128) when no channel
//      is enabled and advances phase counters.
//   4. scc_compute_sample() produces correct output for a single enabled
//      channel with a known waveform.
//   5. Channel 5 shares channel 4's waveform.
//   6. mapper_type_from_string("konami_scc") → MapperType::KONAMI_SCC.
//   7. mapper_type_to_string(MapperType::KONAMI_SCC) → "konami_scc".
//   8. scc_setup() + scc_write_cb: bank-select 0x3F enables SCC; other values
//      disable it.
//   9. scc_read_cb: waveform bytes readable when SCC is enabled; 0xFF for
//      non-waveform registers.

#include "peripherals/scc.h"
#include "bus/mappers.h"
#include "bus/mapping_plan.h"
#include "bus/cartridge.h"
#include "platform/gpio_defs.h"

#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Bus word helpers (mirrors psg test pattern)
// ---------------------------------------------------------------------------

// Build a GPIO bus word for a memory write: address + data + /WR low.
static uint32_t make_write(uint32_t addr, uint8_t data) {
    return (addr << GPIO_A0) | (static_cast<uint32_t>(data) << GPIO_D0);
    // /WR state is decoded by the bus loop, not by callbacks; callbacks
    // receive the raw bus word — address and data already decoded by the
    // Cartridge dispatch layer.
}

// Build a GPIO bus word for a memory read.
static uint32_t make_read(uint32_t addr) {
    return (addr << GPIO_A0);
}

// ---------------------------------------------------------------------------
// test_scc_reset
// ---------------------------------------------------------------------------

static void test_scc_reset() {
    SccState s;
    memset(&s, 0xAB, sizeof(s));  // fill with garbage

    scc_reset(s);

    // All waveform bytes must be zero.
    for (int ch = 0; ch < 4; ++ch)
        for (int i = 0; i < 32; ++i)
            CHECK(s.wave[ch][i] == 0);

    // Frequency, volume, enable all zero.
    for (int ch = 0; ch < 5; ++ch) {
        CHECK(s.freq[ch]          == 0u);
        CHECK(s.vol[ch]           == 0u);
        CHECK(s.phase_counter[ch] == 0u);
        CHECK(s.wave_pos[ch]      == 0u);
    }
    CHECK(s.enable          == 0u);
    CHECK(s.scc_enabled     == false);
    CHECK(s.audio_initialized == false);
}

// ---------------------------------------------------------------------------
// test_scc_write_register
// ---------------------------------------------------------------------------

static void test_scc_write_register() {
    SccState s;
    scc_reset(s);

    // --- Waveform writes ---

    // Write byte 0x42 to channel 2 position 5 (offset 0x045).
    // Channel 2 waveform starts at offset 0x040.
    {
        Cartridge c; c.clear();
        scc_setup(c, s);
        s.scc_enabled = true;  // enable SCC so register writes are accepted

        // Simulate SCC write at MSX addr 0x9845 (seg4 offset 0x1845; reg = 0x0045).
        auto [handled, val] = c.memory_write_callbacks[4](c, make_write(0x9845u, 0x42u));
        (void)handled; (void)val;
        CHECK(s.wave[2][5] == static_cast<int8_t>(0x42u));
    }

    // --- Frequency writes ---
    // Channel 3 freq LSB at offset 0x086, MSB at offset 0x087.
    {
        Cartridge c; c.clear();
        scc_setup(c, s);
        s.scc_enabled = true;

        // LSB of channel 3 freq: MSX addr 0x9886.
        c.memory_write_callbacks[4](c, make_write(0x9886u, 0xABu));
        // MSB (only 4 bits used) of channel 3 freq: MSX addr 0x9887.
        c.memory_write_callbacks[4](c, make_write(0x9887u, 0x0Cu));

        CHECK(s.freq[3] == 0x0CABu);
    }

    // --- Volume writes ---
    // Channel 1 volume at offset 0x08A.
    {
        Cartridge c; c.clear();
        scc_setup(c, s);
        s.scc_enabled = true;

        c.memory_write_callbacks[4](c, make_write(0x988Au, 0x1Fu));  // 0x1F → masked to 0x0F
        CHECK(s.vol[0] == 0x0Fu);
    }

    // --- Enable register ---
    {
        Cartridge c; c.clear();
        scc_setup(c, s);
        s.scc_enabled = true;

        c.memory_write_callbacks[4](c, make_write(0x988Fu, 0x15u));  // bits 0,2,4
        CHECK(s.enable == 0x15u);
    }
}

// ---------------------------------------------------------------------------
// test_scc_silence
// ---------------------------------------------------------------------------

static void test_scc_silence() {
    SccState s;
    scc_reset(s);
    // All channels disabled (enable == 0).
    // Even a non-zero waveform should produce no output.
    for (int i = 0; i < 32; ++i)
        s.wave[0][i] = static_cast<int8_t>(127);
    s.freq[0] = 100u;
    s.vol[0]  = 15u;
    // enable stays 0 → channel 0 not running.

    uint8_t sample = scc_compute_sample(s);
    CHECK(sample == 128u);  // centre (silence)
}

// ---------------------------------------------------------------------------
// test_scc_single_channel
// ---------------------------------------------------------------------------

static void test_scc_single_channel() {
    SccState s;
    scc_reset(s);

    // Channel 0: square wave (first 16 bytes = +127, next 16 = -128).
    for (int i = 0;  i < 16; ++i) s.wave[0][i] =  127;
    for (int i = 16; i < 32; ++i) s.wave[0][i] = -128;

    s.freq[0] = 0u;    // freq register 0 → threshold = 1 × 65536
    s.vol[0]  = 15u;   // max volume
    s.enable  = 0x01u; // channel 0 only

    // At freq=0 each sample advances the phase counter by SCC_TICKS_PER_SAMPLE_FP16
    // = 5319483. Threshold = (0+1)×65536 = 65536.
    // After one call: counter = 5319483, which wraps 5319483/65536 = 81.15...
    // times, advancing wave_pos by 81. wave_pos = 81 % 32 = 17.
    // wave[0][17] = -128; vol=15; mixed = -128 × 15 = -1920.
    // output = 128 + (-1920 / 75) = 128 - 25 = 103.

    uint8_t sample = scc_compute_sample(s);
    CHECK(sample == 103u);

    // wave_pos after wrap: 81 % 32 = 17.
    CHECK(s.wave_pos[0] == 17u);
}

// ---------------------------------------------------------------------------
// test_scc_ch5_shares_ch4_waveform
// ---------------------------------------------------------------------------

static void test_scc_ch5_shares_ch4_waveform() {
    SccState s;
    scc_reset(s);

    // Write a distinctive pattern to channel 4 waveform (index 3).
    for (int i = 0; i < 32; ++i)
        s.wave[3][i] = static_cast<int8_t>(i * 4);  // 0, 4, 8, ...

    // Enable only channel 5 (bit 4).
    s.freq[4] = 0u;
    s.vol[4]  = 15u;
    s.enable  = 0x10u;  // bit 4 = channel 5

    // Channel 5 (index 4 in arrays) must read from wave[3] (same as ch4).
    // wave_pos[4] starts at 0; wave[3][0] = 0.
    // After advance: same arithmetic as single-channel test but reading wave[3].
    // wave_pos = 81 % 32 = 17; wave[3][17] = 17*4 = 68.
    // mixed = 68 × 15 = 1020; output = 128 + (1020/75) = 128 + 13 = 141.

    uint8_t sample = scc_compute_sample(s);
    CHECK(sample == 141u);

    // Confirm wave_pos[4] advanced (ch5 index).
    CHECK(s.wave_pos[4] == 17u);
}

// ---------------------------------------------------------------------------
// test_mapper_type_roundtrip
// ---------------------------------------------------------------------------

static void test_mapper_type_roundtrip() {
    CHECK(mapper_type_from_string("konami_scc") == MapperType::KONAMI_SCC);
    CHECK(strcmp(mapper_type_to_string(MapperType::KONAMI_SCC), "konami_scc") == 0);
    // Verify existing types unaffected.
    CHECK(mapper_type_from_string("konami")   == MapperType::KONAMI);
    CHECK(mapper_type_from_string("konami_z") == MapperType::KONAMI_Z);
}

// ---------------------------------------------------------------------------
// test_scc_bank_select
// ---------------------------------------------------------------------------

static void test_scc_bank_select() {
    SccState s;
    scc_reset(s);

    static const uint8_t fake_rom[8192u * 8u] = {};

    Cartridge c; c.clear();
    mapper_setup_konami_scc(c, fake_rom, s);

    // SCC starts disabled.
    CHECK(s.scc_enabled == false);

    // Write 0x3F to any address in segment 4 (0x8000–0x97FF).
    c.memory_write_callbacks[4](c, make_write(0x8000u, 0x3Fu));
    CHECK(s.scc_enabled == true);

    // Write any other value — disables SCC.
    c.memory_write_callbacks[4](c, make_write(0x8000u, 0x02u));
    CHECK(s.scc_enabled == false);
}

// ---------------------------------------------------------------------------
// test_scc_read_cb
// ---------------------------------------------------------------------------

static void test_scc_read_cb() {
    SccState s;
    scc_reset(s);

    static const uint8_t fake_rom[8192u * 8u] = {};

    Cartridge c; c.clear();
    mapper_setup_konami_scc(c, fake_rom, s);

    // With SCC disabled, reads to 0x9800 area should return {false,0}.
    {
        auto [handled, val] = c.memory_read_callbacks[4](c, make_read(0x9800u));
        CHECK(handled == false);
        CHECK(val     == 0u);
    }

    // Enable SCC.
    c.memory_write_callbacks[4](c, make_write(0x8000u, 0x3Fu));
    CHECK(s.scc_enabled == true);

    // Write a known value to ch1 waveform byte 3 (offset 0x003 → MSX 0x9803).
    s.wave[0][3] = static_cast<int8_t>(0x55);

    // Read it back via the read callback.
    {
        auto [handled, val] = c.memory_read_callbacks[4](c, make_read(0x9803u));
        CHECK(handled == true);
        CHECK(val     == 0x55u);
    }

    // Reads to non-waveform SCC space (e.g. 0x9880) return 0xFF.
    {
        auto [handled, val] = c.memory_read_callbacks[4](c, make_read(0x9880u));
        CHECK(handled == true);
        CHECK(val     == 0xFFu);
    }

    // Reads to lower ROM range (0x8000–0x97FF) return {false, 0} even when
    // SCC is enabled (ROM pointer serves that region).
    {
        auto [handled, val] = c.memory_read_callbacks[4](c, make_read(0x8000u));
        CHECK(handled == false);
        CHECK(val     == 0u);
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_scc_reset();
    test_scc_write_register();
    test_scc_silence();
    test_scc_single_channel();
    test_scc_ch5_shares_ch4_waveform();
    test_mapper_type_roundtrip();
    test_scc_bank_select();
    test_scc_read_cb();

    return test_summary();
}
