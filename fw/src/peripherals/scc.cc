// scc.cc — Konami SCC (Sound Creative Chip) emulation.
//
// See scc.h for register map and design notes.

#include "peripherals/scc.h"
#include "platform/gpio_defs.h"
#include <cstring>

#ifndef JLPICART_HOST_TEST
#  include "hardware/pwm.h"
#  include "hardware/gpio.h"
#  include "pico/time.h"
// Shared SCC sample — written by Core 1 scc_service(), mixed by psg_service().
// Declared in psg.cc.
extern volatile uint8_t g_scc_sample;
#endif

// ---------------------------------------------------------------------------
// scc_reset
// ---------------------------------------------------------------------------

void scc_reset(SccState& state) {
    memset(&state, 0, sizeof(state));
    // All waveforms, frequencies, volumes, and enable start at zero.
    // synthesis state (phase_counter, wave_pos) also zero.
    state.scc_enabled       = false;
    state.audio_initialized = false;
}

// ---------------------------------------------------------------------------
// Bus callbacks — installed by mapper_setup_konami_scc() in mappers.cc.
//
// scc_write_register: maps an SCC offset (0-0x1FF within 0x9800 region) to
// the appropriate register in SccState.
// ---------------------------------------------------------------------------

static void scc_write_register(SccState& s, uint32_t offset, uint8_t data) {
    if (offset < 0x80u) {
        // Waveform area: 4 × 32 bytes = 0x000-0x07F.
        uint32_t ch  = offset >> 5;        // channel index 0-3
        uint32_t pos = offset & 0x1Fu;     // position 0-31
        s.wave[ch][pos] = static_cast<int8_t>(data);
    } else if (offset < 0x8Au) {
        // Frequency registers: 5 × 2 bytes at 0x080-0x089.
        uint32_t ch     = (offset - 0x80u) >> 1;  // channel 0-4
        uint32_t is_msb = (offset - 0x80u) & 1u;  // 0 = LSB, 1 = MSB
        if (!is_msb) {
            s.freq[ch] = (s.freq[ch] & 0xFF00u) | data;
        } else {
            s.freq[ch] = (s.freq[ch] & 0x00FFu) | (static_cast<uint16_t>(data & 0x0Fu) << 8);
        }
    } else if (offset < 0x8Fu) {
        // Volume registers: 5 × 1 byte at 0x08A-0x08E.
        s.vol[offset - 0x8Au] = data & 0x0Fu;
    } else if (offset == 0x8Fu) {
        // Channel enable bits at 0x08F.
        s.enable = data & 0x1Fu;
    }
    // 0x0E0-0x0FF: test register — silently accepted.
}

// Read callback: returns SCC register value when SCC enabled and address
// falls in 0x9800-0x9FFF.  Returns {false,0} for lower ROM range.
static std::pair<bool, uint8_t> RAMFUNC(scc_read_cb)(Cartridge& c, uint32_t bus) {
    const SccState* scc = reinterpret_cast<const SccState*>(c.ram_base);
    if (!scc->scc_enabled) return {false, 0};

    const uint32_t addr = (bus >> GPIO_A0) & 0xFFFFu;
    const uint32_t off  = addr & 0x1FFFu;  // offset within 8KB segment
    if (off < 0x1800u) return {false, 0};  // 0x8000-0x97FF: serve from ROM

    // 0x9800-0x9FFF: SCC register space.
    const uint32_t reg = off - 0x1800u;  // 0-0x7FF

    if (reg < 0x80u) {
        // Waveform read: channels 1-4 at 0x000-0x07F.
        return {true, static_cast<uint8_t>(scc->wave[reg >> 5][reg & 0x1Fu])};
    }
    // All other reads return 0xFF (open bus).
    return {true, 0xFFu};
}

// Write callback: handles both bank select (0x8000-0x97FF) and SCC register
// writes (0x9800-0x9FFF when enabled).
static std::pair<bool, uint8_t> RAMFUNC(scc_write_cb)(Cartridge& c, uint32_t bus) {
    SccState* scc  = reinterpret_cast<SccState*>(c.ram_base);
    const uint32_t addr = (bus >> GPIO_A0) & 0xFFFFu;
    const uint8_t  data = static_cast<uint8_t>(bus >> GPIO_D0);
    const uint32_t off  = addr & 0x1FFFu;  // offset within 8KB segment

    if (off < 0x1800u) {
        // Bank select register write (0x8000-0x97FF).
        if (data == 0x3Fu) {
            scc->scc_enabled = true;
            // Leave ROM pointer unchanged; reads to 0x8000-0x97FF still use it.
        } else {
            scc->scc_enabled = false;
            // Switch segment 4 ROM bank.
            c.memory_read_addresses[4] = &c.rom_base[data * 8192u];
        }
    } else if (scc->scc_enabled) {
        // SCC register write (0x9800-0x9FFF).
        scc_write_register(*scc, off - 0x1800u, data);
    }
    return {false, 0};
}

// ---------------------------------------------------------------------------
// scc_setup
// ---------------------------------------------------------------------------

void scc_setup(Cartridge& c, SccState& state) {
    c.ram_base = reinterpret_cast<uint8_t*>(&state);
    // Segment 4 (0x8000-0x9FFF): read from ROM by default,
    // but the read callback intercepts 0x9800-0x9FFF when SCC is enabled.
    c.memory_read_callbacks[4]  = scc_read_cb;
    c.memory_write_callbacks[4] = scc_write_cb;
}

// ---------------------------------------------------------------------------
// scc_compute_sample
// ---------------------------------------------------------------------------

uint8_t scc_compute_sample(SccState& state) {
    int32_t mixed = 0;

    for (int ch = 0; ch < 5; ++ch) {
        if (!(state.enable & (1u << ch))) continue;

        // Advance phase counter by SCC_TICKS_PER_SAMPLE_FP16.
        state.phase_counter[ch] += SCC_TICKS_PER_SAMPLE_FP16;

        const uint32_t f         = state.freq[ch] & 0xFFFu;
        const uint32_t threshold = (f + 1u) << 16;  // (freq+1) × 65536

        while (state.phase_counter[ch] >= threshold) {
            state.phase_counter[ch] -= threshold;
            state.wave_pos[ch] = (state.wave_pos[ch] + 1u) & 31u;
        }

        // Ch 5 (index 4) shares ch 4's waveform (index 3).
        const int8_t wave_val = state.wave[ch < 4 ? ch : 3][state.wave_pos[ch]];
        mixed += (int32_t)wave_val * (int32_t)state.vol[ch];
    }

    // Scale: wave_val × vol ranges ±127×15=±1905 per channel; 5 channels ±9525.
    // Map to [0,255]: center at 128, scale so ±9525 fits in ±127.
    // output = 128 + mixed × 127 / 9525 ≈ 128 + mixed / 75
    int32_t output = 128 + (mixed / 75);
    if (output < 0)   output = 0;
    if (output > 255) output = 255;
    return static_cast<uint8_t>(output);
}

// ---------------------------------------------------------------------------
// Hardware audio (firmware only)
// ---------------------------------------------------------------------------

#ifndef JLPICART_HOST_TEST

// Shared SCC output sample — written here, mixed by psg_service() in psg.cc.
volatile uint8_t g_scc_sample = 128u;

void scc_audio_init(SccState& state) {
    // PSG audio init already configured the PWM on GPIO64_SND.
    // We only need to mark ourselves initialised.
    (void)state;
    state.audio_initialized = true;
}

void scc_service(SccState& state) {
    if (!state.audio_initialized) return;

    // Rate-limit to 44100 Hz using the same timer as psg_service().
    // The rate-limiting happens in psg_service; here we just compute the sample.
    g_scc_sample = scc_compute_sample(state);
}

#else

void scc_audio_init(SccState& state) { (void)state; }
void scc_service(SccState& state)    { (void)state; }

#endif // JLPICART_HOST_TEST
