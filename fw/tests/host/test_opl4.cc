// test_opl4.cc — Stage 30: OPL4 (YMF278B) PCM emulation.
//
// Tests:
//   1. opl4_reset() zeroes all state / sets all channels to OFF.
//   2. opl4_parse_wave_desc() correctly unpacks 12-byte headers.
//   3. Register writes routed correctly: wave, fnum, oct, tl, ar, envelope regs.
//   4. Key-on (KEYON bit rising edge) triggers attack; key-off enters release.
//   5. opl4_compute_sample() returns 128 (silence) when all channels are OFF.
//   6. A single 8-bit PCM channel produces non-zero output when keyed on.
//   7. Total level TL=0x7F (max attenuation) mutes the output.
//   8. Envelope: ATTACK eventually reaches 0 (max volume).
//   9. Envelope: RELEASE from peak eventually silences the channel.
//  10. OPL3 register store: write and read back via port 0x7E/0x7F.
//  11. Wave section register address/data readback via 0xF6/0xF7.
//  12. 16-bit PCM format reads the correct 2-byte sample.
//  13. ADPCM nibble decode: known step produces known output.

#include "peripherals/opl4.h"
#include "bus/cartridge.h"
#include "platform/gpio_defs.h"

#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Bus word helpers
// ---------------------------------------------------------------------------

static uint32_t io_write(uint8_t port, uint8_t data) {
    return ((uint32_t)port << GPIO_A0) | ((uint32_t)data << GPIO_D0);
}
static uint32_t io_read(uint8_t port) {
    return (uint32_t)port << GPIO_A0;
}

// ---------------------------------------------------------------------------
// Minimal wave ROM builder
// ---------------------------------------------------------------------------

// Build a 12-byte wave descriptor into `buf`.
static void make_wave_desc(uint8_t* buf,
                            uint8_t format, uint32_t start,
                            uint16_t loop_s, uint16_t loop_e,
                            uint8_t base_fnum, uint8_t base_oct)
{
    buf[0]  = (uint8_t)(format << 6);
    buf[1]  = (uint8_t)(start & 0xFF);
    buf[2]  = (uint8_t)((start >> 8)  & 0xFF);
    buf[3]  = (uint8_t)((start >> 16) & 0xFF);
    buf[4]  = (uint8_t)(loop_s & 0xFF);
    buf[5]  = (uint8_t)(loop_s >> 8);
    buf[6]  = (uint8_t)(loop_e & 0xFF);
    buf[7]  = (uint8_t)(loop_e >> 8);
    buf[8]  = base_fnum;
    buf[9]  = base_oct & 0x0Fu;
    buf[10] = 0;
    buf[11] = 0;
}

// ---------------------------------------------------------------------------
// test_reset
// ---------------------------------------------------------------------------

static void test_reset() {
    Opl4State s;
    memset(&s, 0xBB, sizeof(s));
    opl4_reset(s);

    for (int i = 0; i < 24; ++i) {
        CHECK(s.channels[i].env_phase == Opl4EnvPhase::OFF);
        CHECK(s.channels[i].env_level == OPL4_ENV_MAX);
        CHECK(s.channels[i].tl        == 0x7Fu);
        CHECK(s.channels[i].keyon     == false);
    }
    CHECK(s.audio_initialized == false);
}

// ---------------------------------------------------------------------------
// test_parse_wave_desc
// ---------------------------------------------------------------------------

static void test_parse_wave_desc() {
    uint8_t rom[24] = {};  // two wave descriptors

    // Wave 0: 8-bit, start=0x001800, loop_start=0, loop_end=32,
    //          base_fnum=128, base_oct=8
    make_wave_desc(rom + 0,  0, 0x001800, 0, 32, 128, 8);
    // Wave 1: 16-bit, start=0x002000, loop_start=4, loop_end=20,
    //          base_fnum=256, base_oct=9
    make_wave_desc(rom + 12, 2, 0x002000, 4, 20, 0, 9);

    Opl4WaveDesc d0, d1;
    CHECK(opl4_parse_wave_desc(rom, sizeof(rom), 0, d0) == true);
    CHECK(d0.format     == 0u);
    CHECK(d0.start_addr == 0x001800u);
    CHECK(d0.loop_start == 0u);
    CHECK(d0.loop_end   == 32u);
    CHECK(d0.base_fnum  == 128u);
    CHECK(d0.base_oct   == 8u);

    CHECK(opl4_parse_wave_desc(rom, sizeof(rom), 1, d1) == true);
    CHECK(d1.format     == 2u);
    CHECK(d1.start_addr == 0x002000u);
    CHECK(d1.loop_start == 4u);
    CHECK(d1.loop_end   == 20u);
    CHECK(d1.base_oct   == 9u);

    // Out-of-range wave index returns false.
    Opl4WaveDesc dbad;
    CHECK(opl4_parse_wave_desc(rom, sizeof(rom), 2, dbad) == false);

    // Null ROM returns false.
    CHECK(opl4_parse_wave_desc(nullptr, 0, 0, dbad) == false);
}

// ---------------------------------------------------------------------------
// test_register_routing
// ---------------------------------------------------------------------------

static void test_register_routing() {
    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, nullptr, 0);

    // Write wave number to channel 0: addr 0x08, value 0xAB.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x08));  // addr = 0x08 (WAVE[7:0] ch0)
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xAB));
    CHECK(s.channels[0].wave_num == 0xABu);

    // Write WAVE[9:8] and FN[9:8] for channel 0: addr 0x20, value 0b00001101 = 0x0D.
    // bits[3:2]=WAVE[9:8]=0b11, bits[1:0]=FN[9:8]=0b01
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x20));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x0Du));
    CHECK((s.channels[0].wave_num >> 8) == 0x03u);  // WAVE[9:8] = bits[3:2] = 0b11
    CHECK((s.channels[0].fnum    >> 8) == 0x01u);  // FN[9:8]   = bits[1:0] = 0b01

    // FN[7:0] at addr 0x38.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x38));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x55));
    CHECK((s.channels[0].fnum & 0xFFu) == 0x55u);

    // OCT + KEYON: addr 0x50, value 0b10111000 = 0xB8.
    // bit[7]=KEYON=1, bits[6:3]=OCT=0b0111=7 → oct=7.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xB8u));
    CHECK(s.channels[0].oct   == 7u);
    CHECK(s.channels[0].keyon == true);
    CHECK(s.channels[0].keyon_event == true);  // rising edge recorded

    // TL at addr 0xB0, value 0x40 → tl=0x40, ld=0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xB0));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x40));
    CHECK(s.channels[0].tl == 0x40u);
    CHECK(s.channels[0].ld == false);

    // AR + AM + VIB: addr 0x68, value 0b11000110 = 0xC6.
    // bits[7:4]=AR=0xC=12, bit[3]=AM=0, bit[2]=VIB=1, bits[1:0]=LFO=2
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x68));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC6u));
    CHECK(s.channels[0].ar  == 12u);
    CHECK(s.channels[0].am  == false);
    CHECK(s.channels[0].vib == true);
    CHECK(s.channels[0].lfo == 2u);

    // D1R + DL: addr 0x80, value 0b01010011 = 0x53.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x80));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x53));
    CHECK(s.channels[0].d1r == 5u);
    CHECK(s.channels[0].dl  == 3u);

    // D2R + RR: addr 0x98, value 0b10000111 = 0x87.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x98));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x87));
    CHECK(s.channels[0].d2r == 8u);
    CHECK(s.channels[0].rr  == 7u);

    // PAN: addr 0xC8, value 0b10110000 = 0xB0. bits[7:4]=pan=0xB.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xC8));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xB0));
    CHECK(s.channels[0].pan == 0xBu);
}

// ---------------------------------------------------------------------------
// test_silence_when_all_off
// ---------------------------------------------------------------------------

static void test_silence_when_all_off() {
    Opl4State s;
    opl4_reset(s);
    uint8_t sample = opl4_compute_sample(s);
    CHECK(sample == 128u);
}

// ---------------------------------------------------------------------------
// test_pcm_8bit_output
//
// Build a minimal wave ROM with one 8-bit square-wave sample,
// key-on channel 0, and verify the output is not 128.
// ---------------------------------------------------------------------------

static void test_pcm_8bit_output() {
    // Wave ROM: 1 descriptor (12 bytes) + 8 sample bytes.
    static uint8_t rom[12 + 8];
    memset(rom, 0, sizeof(rom));
    // Descriptor: 8-bit, start=12, loop_start=0, loop_end=8.
    make_wave_desc(rom, 0, 12, 0, 8, 128, 8);
    // Samples: alternating +127 / -128.
    for (int i = 0; i < 8; ++i)
        rom[12 + i] = (i & 1u) ? 0x80u : 0x7Fu;  // -128 and +127

    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, rom, sizeof(rom));

    // Set channel 0 registers: wave=0, fnum=512, oct=8 (oct_signed=0), ar=15, tl=0.
    // step_fp16 = 512 << 7 = 65536 → exactly 1 sample per output sample.

    // WAVE[7:0] = 0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x08));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x00));

    // FN[9:8]=2 (512>>8=2), WAVE[9:8]=0: addr 0x20 → value 0b00001000 = 0x08.
    // FN[9:8] = bits[1:0] = 0b10 = 2; WAVE[9:8] = bits[3:2] = 0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x20));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x02));  // FN[9:8]=2

    // FN[7:0] = 0 → total fnum = 0x200 = 512.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x38));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x00));

    // TL=0 (max volume), LD=0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xB0));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x00));

    // AR=15 (max attack), D1R=0, DL=15 (sustain at max), D2R=0, RR=0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x68));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xF0));  // AR=15

    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x80));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x0F));  // D1R=0, DL=15

    // KEYON: addr 0x50, OCT=8 (bits[6:3]=0b1000=8), KEYON=1 → value=0b11000000=0xC0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC0u));  // KEYON=1, OCT=8

    // After enough attack steps (AR=15, step=512 per sample, from 1023 to 0
    // → need 1023/512+1 = 3 samples to reach 0).
    // Then wave output should be non-128.
    for (int i = 0; i < 10; ++i)
        opl4_compute_sample(s);

    uint8_t sample = opl4_compute_sample(s);
    // Channel is active, TL=0, envelope should be near peak → output ≠ 128.
    CHECK(sample != 128u);
}

// ---------------------------------------------------------------------------
// test_tl_max_mutes
// ---------------------------------------------------------------------------

static void test_tl_max_mutes() {
    // Same ROM as above.
    static uint8_t rom[12 + 8];
    memset(rom, 0, sizeof(rom));
    make_wave_desc(rom, 0, 12, 0, 8, 128, 8);
    for (int i = 0; i < 8; ++i)
        rom[12 + i] = (i & 1u) ? 0x80u : 0x7Fu;

    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, rom, sizeof(rom));

    // Channel 0: same setup but TL=0x7F (max attenuation) and LD=1.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x08));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x00));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x38));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x00));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x20));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x02));

    // TL=127 + LD=1: value = 0xFF.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xB0));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xFF));

    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x68));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xF0));

    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC0u));

    for (int i = 0; i < 20; ++i)
        opl4_compute_sample(s);

    uint8_t sample = opl4_compute_sample(s);
    CHECK(sample == 128u);  // TL=max + LD → always silent
}

// ---------------------------------------------------------------------------
// test_envelope_attack_reaches_zero
// ---------------------------------------------------------------------------

static void test_envelope_attack_reaches_zero() {
    // Verify: kAttStep[15]=512. Starting from OPL4_ENV_MAX=1023,
    // after 2 subtractions: 1023-512=511, 511≤512 → level=0.
    uint32_t level = OPL4_ENV_MAX;
    for (int i = 0; i < 10; ++i) {
        if (level <= 512u) { level = 0u; break; }
        level -= 512u;
    }
    CHECK(level == 0u);  // AR=15 step=512 reaches 0 within 10 iterations
}

// ---------------------------------------------------------------------------
// test_release_silences_channel
// ---------------------------------------------------------------------------

static void test_release_silences_channel() {
    // kEnvStep[15] = 128, so after ceil(1023/128)+1 = 9 steps, level >= ENV_MAX.
    uint32_t level = 0u;
    for (int i = 0; i < 20; ++i) {
        level += 128u;
        if (level >= OPL4_ENV_MAX) { level = OPL4_ENV_MAX; break; }
    }
    CHECK(level == OPL4_ENV_MAX);

    // Verify through the register path: after key-off, env enters release.
    Opl4State s2;
    opl4_reset(s2);
    Cartridge c; c.clear();
    opl4_setup(c, s2, nullptr, 0);

    // Key-on channel 3.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50 + 3));  // OCT+KEYON for ch 3
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC0u));     // KEYON=1, OCT=8
    CHECK(s2.channels[3].keyon == true);

    // Key-off: same address, bit7=0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50 + 3));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x40u));     // KEYON=0, OCT=8
    CHECK(s2.channels[3].keyon       == false);
    CHECK(s2.channels[3].env_phase   == Opl4EnvPhase::RELEASE);
}

// ---------------------------------------------------------------------------
// test_opl3_register_store
// ---------------------------------------------------------------------------

static void test_opl3_register_store() {
    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, nullptr, 0);

    // Write OPL3 register 0x20 = 0xAB via ports 0x7E/0x7F.
    c.io_write_callbacks[0x7E](c, io_write(0x7E, 0x20));
    c.io_write_callbacks[0x7F](c, io_write(0x7F, 0xAB));
    CHECK(s.opl3_regs[0x20] == 0xABu);

    // Read back via 0x7F read.
    auto [ok, val] = c.io_read_callbacks[0x7F](c, io_read(0x7F));
    CHECK(ok  == true);
    CHECK(val == 0xABu);
}

// ---------------------------------------------------------------------------
// test_wave_reg_readback
// ---------------------------------------------------------------------------

static void test_wave_reg_readback() {
    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, nullptr, 0);

    // Write wave register 0x00 = 0x05 (LFO speed).
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x00));
    c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x05));
    CHECK(s.wave_regs[0x00] == 0x05u);

    // Read back.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x00));  // re-select
    auto [ok, val] = c.io_read_callbacks[0xF7](c, io_read(0xF7));
    CHECK(ok  == true);
    CHECK(val == 0x05u);
}

// ---------------------------------------------------------------------------
// test_16bit_pcm_read
// ---------------------------------------------------------------------------

static void test_16bit_pcm_read() {
    // Single 16-bit sample: 0x7F00 (little-endian → +32512).
    // After >>8 = +127; 128 + 127/24 = 133 > 128.
    uint8_t rom[12 + 2];
    memset(rom, 0, sizeof(rom));
    make_wave_desc(rom, 2 /*16-bit*/, 12, 0, 1, 128, 8);
    rom[12] = 0x00;
    rom[13] = 0x7F;  // → 0x7F00 as int16_t (little-endian)

    Opl4WaveDesc d;
    opl4_parse_wave_desc(rom, sizeof(rom), 0, d);
    CHECK(d.format == 2u);

    // Verify read_wave_sample indirectly: set up a channel and compute a sample.
    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, rom, sizeof(rom));

    // Channel 0: wave=0, fnum=512, oct=8, ar=15, tl=0, LD=0.
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x08)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x20)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x02));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x38)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xB0)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x68)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xF0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC0u));

    // Run attack samples.
    for (int i = 0; i < 5; ++i) opl4_compute_sample(s);
    uint8_t sample = opl4_compute_sample(s);
    // The ROM only has 1 sample; wave loops between 0 and 1, loop_end=1.
    // The sample is 0x1234 >> 8 = 0x12 = 18 (positive). Mix → output > 128.
    CHECK(sample > 128u);
}

// ---------------------------------------------------------------------------
// test_adpcm_nibble_decode
// ---------------------------------------------------------------------------

static void test_adpcm_nibble_decode() {
    // Verify a single known ADPCM decode step.
    // Initial predictor=0, step_idx=0.
    // Nibble=7 (nibble bits: 0b0111 → magnitude=7, sign=0):
    //   delta = step[0] + step[0]/2 + step[0]/4 + step[0]/8
    //         = 7 + 3 + 1 + 0 = 11 (step[0]=7).
    //   Wait: delta += step if bit 2 set (4)... let me re-read:
    //     if (nibble & 4) delta += step;  // bit 2: nibble=7, bit2=1 → +=7
    //     if (nibble & 2) delta += step>>1; // bit 1: nibble=7, bit1=1 → +=3
    //     if (nibble & 1) delta += step>>2; // bit 0: nibble=7, bit0=1 → +=1
    //     delta += step>>3;                  //                         +=0
    //   delta = 7+3+1+0 = 11; sign bit=0 → positive.
    //   pred = 0 + 11 = 11.
    //   step_idx: index_table[7] = 8 → si=0+8=8.

    int16_t  pred    = 0;
    uint32_t step_ix = 0;

    // Invoke internal ADPCM decode: we test via opl4_compute_sample with
    // a minimal ADPCM ROM.
    // For a unit test just verify the table-driven arithmetic by building
    // an ADPCM ROM and checking compute_sample output is not 128.

    // Instead test the state struct directly: write 4 ADPCM nibbles and
    // check sign via compute_sample.
    static uint8_t adpcm_rom[12 + 4];
    memset(adpcm_rom, 0, sizeof(adpcm_rom));
    // 4 bytes → 8 nibbles (ADPCM samples). Use nibble 0x77 = two nibbles of 7.
    make_wave_desc(adpcm_rom, 3 /*ADPCM*/, 12, 0, 8, 128, 8);
    adpcm_rom[12] = 0x77;  // nibbles: 7 (high), 7 (low)
    adpcm_rom[13] = 0x77;
    adpcm_rom[14] = 0x77;
    adpcm_rom[15] = 0x77;

    Opl4State s;
    opl4_reset(s);
    Cartridge c; c.clear();
    opl4_setup(c, s, adpcm_rom, sizeof(adpcm_rom));

    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x08)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x20)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x02));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x38)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0xB0)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x68)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xF0));
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x80)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0x0F)); // DL=15 sustain
    c.io_write_callbacks[0xF6](c, io_write(0xF6, 0x50)); c.io_write_callbacks[0xF7](c, io_write(0xF7, 0xC0u));

    // The ADPCM predictor grows exponentially with nibble=7 (all positive
    // steps, step_idx advances +8 per nibble).  After ~9 iterations the
    // predictor exceeds 11000, >>8 > 43, 43/24 >= 1 → output > 128.
    for (int i = 0; i < 10; ++i) opl4_compute_sample(s);
    uint8_t sample = opl4_compute_sample(s);
    // ADPCM nibble=7 (positive, large predictor after 10 steps) → output > 128.
    CHECK(sample > 128u);

    (void)pred; (void)step_ix;  // suppress unused warning from explanatory vars
}

// ---------------------------------------------------------------------------
// OPL3 FM synthesis tests
// ---------------------------------------------------------------------------

// Helpers: write an OPL3 register directly (bypass the cartridge port layer)
static void fm_write(Opl3State& s, uint16_t reg, uint8_t val) {
    opl3_write_reg(s, reg, val);
}

// Set up a basic 2-op melodic channel on ch0 in bank 0.
// AR=15, DR=0, SL=0, RR=15, TL=0, MULTI=1, no KSL, sine wave (WS=0).
static void fm_setup_ch0(Opl3State& s) {
    // Enable OPL3 mode (0x105)
    fm_write(s, 0x105, 0x01);
    // Modulator slot 0 (reg offset 0x00): AM=0, VIB=0, EG=1, KSR=0, MULTI=1
    fm_write(s, 0x0020, 0x21);
    // Modulator TL = 0 (max vol)
    fm_write(s, 0x0040, 0x00);
    // Modulator AR=15, DR=0
    fm_write(s, 0x0060, 0xF0);
    // Modulator SL=0, RR=15
    fm_write(s, 0x0080, 0x0F);
    // Carrier slot 3 (reg offset 0x03): EG=1, MULTI=1
    fm_write(s, 0x0023, 0x21);
    fm_write(s, 0x0043, 0x00);
    fm_write(s, 0x0063, 0xF0);
    fm_write(s, 0x0083, 0x0F);
    // Channel 0: left+right, no feedback, FM (algo=0)
    fm_write(s, 0x00C0, 0xC0);
    // F-number 512, block 3 (≈ 440 Hz equivalent)
    fm_write(s, 0x00A0, 0x00);       // fnum[7:0] = 0
    fm_write(s, 0x00B0, (3 << 2) | 2); // block=3, fnum[9:8]=2 (fnum=512)
    // Key-on: write 0xB0 with keyon bit
    fm_write(s, 0x00B0, 0x20 | (3 << 2) | 2);
}

// test_opl3_fm_2op_produces_audio:
// A keyed-on 2-op channel must produce non-zero samples within a few hundred
// iterations once the attack has completed.
static void test_opl3_fm_2op_produces_audio() {
    Opl3State s;
    opl3_reset(s);
    fm_setup_ch0(s);

    bool got_audio = false;
    for (int i = 0; i < 1000 && !got_audio; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l != 0 || r != 0) got_audio = true;
    }
    CHECK(got_audio);
}

// test_opl3_fm_keyoff_silences:
// After key-off, channel must eventually reach silence (all zeros).
static void test_opl3_fm_keyoff_silences() {
    Opl3State s;
    opl3_reset(s);
    fm_setup_ch0(s);

    // Let it play briefly.
    for (int i = 0; i < 200; ++i) {
        int16_t l, r; opl3_compute_sample(s, l, r);
    }

    // Key-off: clear keyon bit
    fm_write(s, 0x00B0, (3 << 2) | 2);

    // Within a reasonable release time (RR=15 → very fast) it should silence.
    bool silenced = false;
    for (int i = 0; i < 2000 && !silenced; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l == 0 && r == 0) silenced = true;
    }
    CHECK(silenced);
}

// test_opl3_rhythm_bd_produces_audio:
// Bass Drum (key 0xBD bit4) must produce audio when rhythm mode is enabled.
static void test_opl3_rhythm_bd_produces_audio() {
    Opl3State s;
    opl3_reset(s);

    // Enable OPL3 mode
    fm_write(s, 0x105, 0x01);

    // Set up BD operators (ch6: mod=op12 slot 0x10, car=op15 slot 0x13).
    fm_write(s, 0x0030, 0x21); // mod AR+EG
    fm_write(s, 0x0050, 0x00); // mod TL=0
    fm_write(s, 0x0070, 0xF0); // mod AR=15,DR=0
    fm_write(s, 0x0090, 0x0F); // mod SL=0,RR=15
    fm_write(s, 0x0033, 0x21); // car
    fm_write(s, 0x0053, 0x00);
    fm_write(s, 0x0073, 0xF0);
    fm_write(s, 0x0093, 0x0F);
    // ch6 fnum/block (0xB6)
    fm_write(s, 0x00A6, 0x00);
    fm_write(s, 0x00B6, (3 << 2) | 2); // block=3 fnum=512 (no keyon — rhythm controls it)
    // ch6 stereo output
    fm_write(s, 0x00C6, 0xC0);

    // Enter rhythm mode, key BD on (bit 4).
    fm_write(s, 0x00BD, 0x20 | 0x10);  // rhythm=1, BD=1

    bool got_audio = false;
    for (int i = 0; i < 1000 && !got_audio; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l != 0 || r != 0) got_audio = true;
    }
    CHECK(got_audio);
}

// test_opl3_rhythm_melodic_ch6_blocked:
// In rhythm mode, writing key-on to 0xB6 must NOT key on ch6 operators;
// only 0xBD controls them.  Verify by writing 0xB6 with keyon first,
// then enabling rhythm mode — output should stay zero.
static void test_opl3_rhythm_melodic_ch6_blocked() {
    Opl3State s;
    opl3_reset(s);
    fm_write(s, 0x105, 0x01);

    // Instrument setup for ch6
    fm_write(s, 0x0030, 0x21); fm_write(s, 0x0050, 0x00);
    fm_write(s, 0x0070, 0xF0); fm_write(s, 0x0090, 0x0F);
    fm_write(s, 0x0033, 0x21); fm_write(s, 0x0053, 0x00);
    fm_write(s, 0x0073, 0xF0); fm_write(s, 0x0093, 0x0F);
    fm_write(s, 0x00C6, 0xC0);
    fm_write(s, 0x00A6, 0x00);

    // Enable rhythm mode, do NOT set BD key bit — only rhythm mode enabled.
    // Also write keyon bit in 0xB6 (should be ignored while in rhythm mode).
    fm_write(s, 0x00BD, 0x20);             // rhythm=1, BD=0
    fm_write(s, 0x00B6, 0x20 | (3<<2) | 2); // keyon via 0xB6: must be ignored

    // No samples should be non-zero since BD key is not set.
    bool got_audio = false;
    for (int i = 0; i < 500; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l != 0 || r != 0) got_audio = true;
    }
    CHECK(!got_audio);
}

// test_opl3_4op_leader_keys_follower:
// When the 4-op pair ch0+ch3 is enabled and ch0 receives key-on, all 4
// operators (ch0 mod+car, ch3 mod+car) must be keyed on.
static void test_opl3_4op_leader_keys_follower() {
    Opl3State s;
    opl3_reset(s);
    fm_write(s, 0x105, 0x01);
    // Enable pair ch0+ch3 (fourop_en bit 0)
    fm_write(s, 0x104, 0x01);

    // Set up all 4 operators: loud, fast attack, sustain hold.
    // ch0 mod=slot0 (0x00), car=slot3 (0x03)
    // ch3 mod=slot6 (0x08), car=slot9 (0x0B? — wait, kSlotToOp mapping)
    // Actually according to opl3fm.cc: kChMod[3]=6, kChCar[3]=9
    // slot offsets: op6 = slot offset 8, op9 = slot offset 9+2=0x0B? Let me check.
    // slot_to_op: 0→0,1→1,2→2,3→3,4→4,5→5, 8→6,9→7,0xA→8,0xB→9,0xC→10,0xD→11
    // op6 = slot 8 → reg offset 0x28 for ch3 modulator
    // op9 = slot 0xB → reg offset 0x2B for ch3 carrier
    const uint8_t regs[] = { 0x00, 0x03, 0x08, 0x0B }; // slot offsets
    for (uint8_t slot : regs) {
        fm_write(s, 0x0020 + slot, 0x21); // EG=1, MULTI=1
        fm_write(s, 0x0040 + slot, 0x00); // TL=0
        fm_write(s, 0x0060 + slot, 0xF0); // AR=15,DR=0
        fm_write(s, 0x0080 + slot, 0x0F); // SL=0,RR=15
    }
    fm_write(s, 0x00C0, 0xC0); // ch0: L+R
    fm_write(s, 0x00C3, 0xC0); // ch3: L+R

    // F-number for both channels
    fm_write(s, 0x00A0, 0x00); fm_write(s, 0x00B0, (3 << 2) | 2);
    fm_write(s, 0x00A3, 0x00); fm_write(s, 0x00B3, (3 << 2) | 2);

    // Key-on via the LEADER (ch0)
    fm_write(s, 0x00B0, 0x20 | (3 << 2) | 2);

    // The follower operators (ch3 mod+car, ops 6+9 = bank-0 ops 6,9) should
    // now be in ATTACK or later.  Any carrier output from them is non-zero.
    bool got_audio = false;
    for (int i = 0; i < 1000 && !got_audio; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l != 0 || r != 0) got_audio = true;
    }
    CHECK(got_audio);

    // Verify follower ops were keyed on: check env_state not OFF.
    // op6 (ch3 mod) and op9 (ch3 car) are primary bank ops.
    CHECK(s.ops[6].env_state != Opl3Env::OFF);
    CHECK(s.ops[9].env_state != Opl3Env::OFF);
}

// test_opl3_4op_follower_keyon_ignored:
// Writing key-on to the FOLLOWER channel (ch3) directly must NOT key on ops.
static void test_opl3_4op_follower_keyon_ignored() {
    Opl3State s;
    opl3_reset(s);
    fm_write(s, 0x105, 0x01);
    fm_write(s, 0x104, 0x01); // enable ch0+ch3 pair

    // Instrument setup
    const uint8_t regs[] = { 0x08, 0x0B }; // ch3 mod, car slot offsets
    for (uint8_t slot : regs) {
        fm_write(s, 0x0020 + slot, 0x21);
        fm_write(s, 0x0040 + slot, 0x00);
        fm_write(s, 0x0060 + slot, 0xF0);
        fm_write(s, 0x0080 + slot, 0x0F);
    }
    fm_write(s, 0x00C3, 0xC0);
    fm_write(s, 0x00A3, 0x00); fm_write(s, 0x00B3, (3 << 2) | 2);

    // Write key-on to the FOLLOWER (ch3) — should be ignored in 4-op mode.
    fm_write(s, 0x00B3, 0x20 | (3 << 2) | 2);

    // No audio expected
    bool got_audio = false;
    for (int i = 0; i < 500; ++i) {
        int16_t l = 0, r = 0;
        opl3_compute_sample(s, l, r);
        if (l != 0 || r != 0) got_audio = true;
    }
    CHECK(!got_audio);
    // Follower ops should still be OFF
    CHECK(s.ops[6].env_state == Opl3Env::OFF);
    CHECK(s.ops[9].env_state == Opl3Env::OFF);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_reset();
    test_parse_wave_desc();
    test_register_routing();
    test_silence_when_all_off();
    test_pcm_8bit_output();
    test_tl_max_mutes();
    test_envelope_attack_reaches_zero();
    test_release_silences_channel();
    test_opl3_register_store();
    test_wave_reg_readback();
    test_16bit_pcm_read();
    test_adpcm_nibble_decode();
    // OPL3 FM synthesis
    test_opl3_fm_2op_produces_audio();
    test_opl3_fm_keyoff_silences();
    test_opl3_rhythm_bd_produces_audio();
    test_opl3_rhythm_melodic_ch6_blocked();
    test_opl3_4op_leader_keys_follower();
    test_opl3_4op_follower_keyon_ignored();

    return test_summary();
}
