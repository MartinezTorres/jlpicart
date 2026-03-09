/* main.c — JLPiCart Menu stub ROM for Z80 MSX.
 *
 * Implements the Z80 side of the Menu Host ABI (spec.md §8).
 * Polls the MenuMailboxRegs; when cmd_seq != resp_seq, executes the command
 * and writes resp_seq last (signalling the RP2350 that the command is done).
 *
 * Compiled with SDCC 4.5.0 targeting MSX Z80.
 *
 * Self-test mode: compile with -DMENU_STUB_SELF_TEST to have the stub write
 * a known signature into the mailbox instead of entering the poll loop.
 * The RP2350 can then verify the stub loaded and ran correctly.
 *
 * Conformance per spec.md §8:
 *   MUST implement: GET_HOST_INFO, SET_MODE(TEXT_40), CLEAR, PUT_TEXT,
 *                   READ_INPUT, IDLE, NOP.
 *
 * Assumptions:
 *   - Menu Page is at 0x4000–0x7FFF (Z80 page 1, subslot 1).
 *   - MSX BIOS is accessible at page 0 via direct calls.
 *   - No interrupts assumed; pure polling.
 */

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Menu Page addresses (absolute)
 * ---------------------------------------------------------------------------*/

#define PAGE_BASE       ((volatile uint8_t *)0x4000u)
#define HEADER_BASE     ((volatile uint8_t *)0x4000u)
#define MAILBOX_BASE    ((volatile uint8_t *)0x4040u)
#define DATA_BASE       ((volatile uint8_t *)0x4100u)
#define DATA_LEN        0x3F00u

/* MenuStubHeader field offsets (from page base). */
#define HDR_HOST_CAPS   16u  /* uint32_t host_caps */
#define HDR_VDP_CAPS    20u  /* uint32_t vdp_caps */

/* MenuMailboxRegs field offsets (from mailbox base). */
#define MBX_CMD_SEQ     0u
#define MBX_RESP_SEQ    2u
#define MBX_CMD_ID      4u
#define MBX_STATUS      6u
#define MBX_ARG0        8u
#define MBX_ARG1        12u
#define MBX_ARG2        16u
#define MBX_ARG3        20u
#define MBX_IN_LEN      24u
#define MBX_OUT_LEN     26u

/* ---------------------------------------------------------------------------
 * Command IDs and status codes
 * ---------------------------------------------------------------------------*/

#define CMD_NOP           0x0000u
#define CMD_GET_HOST_INFO 0x0001u
#define CMD_SET_MODE      0x0002u
#define CMD_CLEAR         0x0003u
#define CMD_PUT_TEXT      0x0004u
#define CMD_READ_INPUT    0x0005u
#define CMD_VDP_WRITE_REG 0x0006u
#define CMD_VRAM_WRITE    0x0007u
#define CMD_VRAM_FILL     0x0008u
#define CMD_BEEP          0x0009u
#define CMD_IDLE          0x000Au

#define MODE_TEXT_40    0u
#define MODE_TEXT_80    1u
#define MODE_BITMAP     2u

#define MENU_OK             0x0000u
#define MENU_E_UNSUPPORTED  0x0001u
#define MENU_E_BAD_ARG      0x0002u
#define MENU_E_BAD_STATE    0x0003u
#define MENU_E_OVERFLOW     0x0004u
#define MENU_E_HW           0x0005u

/* ---------------------------------------------------------------------------
 * Little-endian helpers
 * ---------------------------------------------------------------------------*/

static uint16_t rd16(volatile const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr16(volatile uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t rd32(volatile const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(volatile uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8)  & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/* ---------------------------------------------------------------------------
 * MSX BIOS helpers (SDCC Z80 __naked calling convention)
 * ---------------------------------------------------------------------------*/

/* CHPUT (0x00A2): A = character to output. */
void bios_chput(uint8_t c) __naked
{
    c;
    __asm
        ld   a, l
        call 0x00A2
        ret
    __endasm;
}

/* POSIT (0x00C6): H = row (1-based), L = col (1-based). */
void bios_posit(uint8_t row, uint8_t col) __naked
{
    row; col;
    __asm
        ld   h, 4(ix)    ; row
        ld   l, 5(ix)    ; col
        call 0x00C6
        ret
    __endasm;
}

/* CLS (0x009F): clear screen in current mode. */
void bios_cls(void) __naked
{
    __asm
        call 0x009F
        ret
    __endasm;
}

/* SNSMAT (0x0141): E = row number, returns A = row byte (0=pressed). */
uint8_t bios_snsmat(uint8_t row) __naked
{
    row;
    __asm
        ld   e, l        ; row argument
        call 0x0141
        ld   l, a        ; return value in L
        ret
    __endasm;
}

/* GTSTCK (0x00D5): A = stick number (0=keyboard, 1=joy1, 2=joy2).
 * Returns A = direction (0=none, 1–8 clockwise from North). */
uint8_t bios_gtstck(uint8_t stick) __naked
{
    stick;
    __asm
        ld   a, l
        call 0x00D5
        ld   l, a
        ret
    __endasm;
}

/* GTTRIG (0x00D8): A = trigger id (0=space/joy1A, 1=joy1B, 2=joy2A, 3=joy2B).
 * Returns A = 0xFF if pressed, 0x00 if not. */
uint8_t bios_gttrig(uint8_t trigger) __naked
{
    trigger;
    __asm
        ld   a, l
        call 0x00D8
        ld   l, a
        ret
    __endasm;
}

/* ---------------------------------------------------------------------------
 * MSX generation detection (best-effort)
 * ---------------------------------------------------------------------------*/

/* Read byte at absolute Z80 address via indirect. */
static uint8_t peek(uint16_t addr) __naked
{
    addr;
    __asm
        ld   l, 4(ix)
        ld   h, 5(ix)
        ld   l, (hl)
        ret
    __endasm;
}

/* Detect MSX generation by reading BIOS ID byte at 0x002D.
 * 0=MSX1, 1=MSX2, 2=MSX2+, 3=turboR. */
static uint8_t detect_msx_gen(void)
{
    return (uint8_t)(peek(0x002Du) & 0x03u);
}

/* Detect VRAM size (best-effort) by reading BIOS variable.
 * 0xF3AEh = VRAM size in KB (MSX2 BIOS only; MSX1 returns 16). */
static uint8_t detect_vram_kb(void)
{
    uint8_t gen = detect_msx_gen();
    if (gen == 0) return 16u;  /* MSX1: always 16KB */
    return peek(0xF3AEu);
}

/* Build host_caps bitfield. */
static uint32_t build_host_caps(uint8_t gen)
{
    uint32_t caps = 0;
    if (gen == 0) caps |= (1u << 0); /* MSX1 */
    if (gen == 1) caps |= (1u << 1); /* MSX2 */
    if (gen == 2) caps |= (1u << 2); /* MSX2+ */
    if (gen == 3) caps |= (1u << 3); /* turboR */
    caps |= (1u << 5);               /* BIOS kbd always available on MSX */
    return caps;
}

/* Build vdp_caps bitfield (best-effort). */
static uint32_t build_vdp_caps(uint8_t gen)
{
    uint32_t caps = (1u << 0);       /* TMS9918-compatible VRAM port writes */
    if (gen >= 2) caps |= (1u << 1); /* palette programmable (MSX2+) */
    return caps;
}

/* ---------------------------------------------------------------------------
 * Current display mode (tracked by stub)
 * ---------------------------------------------------------------------------*/

static uint8_t g_mode = MODE_TEXT_40; /* default: TEXT_40 */

/* ---------------------------------------------------------------------------
 * Command handlers
 * ---------------------------------------------------------------------------*/

static void cmd_get_host_info(volatile uint8_t *data, uint16_t data_cap)
{
    uint8_t gen     = detect_msx_gen();
    uint8_t vram_kb = detect_vram_kb();

    /* Fill HostInfo at data[0..11]. */
    data[0] = (uint8_t)(gen + 1u);  /* 1=MSX1, 2=MSX2, ... */
    data[1] = vram_kb;
    data[2] = 40u;                  /* text_cols: 40 for MSX1 baseline */
    data[3] = 0u;                   /* reserved0 */
    wr32(data + 4, build_host_caps(gen));
    wr32(data + 8, build_vdp_caps(gen));

    /* Also update the header's host_caps/vdp_caps fields. */
    wr32(HEADER_BASE + HDR_HOST_CAPS, build_host_caps(gen));
    wr32(HEADER_BASE + HDR_VDP_CAPS,  build_vdp_caps(gen));

    wr16(MAILBOX_BASE + MBX_OUT_LEN, 12u); /* sizeof(HostInfo) */
    wr16(MAILBOX_BASE + MBX_STATUS,  MENU_OK);

    (void)data_cap;
}

static void cmd_set_mode(uint8_t mode_id)
{
    if (mode_id == MODE_TEXT_40) {
        /* SCREEN 0, 40 cols — invoke BIOS INITXT for clean init. */
        __asm
            call 0x006C   ; INITXT — initialise 40-col text mode
        __endasm;
        g_mode = MODE_TEXT_40;
        wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
    } else {
        wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_UNSUPPORTED);
    }
}

static void cmd_clear(void)
{
    if (g_mode != MODE_TEXT_40) {
        wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_BAD_STATE);
        return;
    }
    bios_cls();
    wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
}

static void cmd_put_text(uint32_t arg0, uint16_t in_len,
                         volatile const uint8_t *data)
{
    uint8_t  x = (uint8_t)(arg0 & 0xFFu);
    uint8_t  y = (uint8_t)((arg0 >> 8) & 0xFFu);
    uint16_t i;

    if (g_mode != MODE_TEXT_40) {
        wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_BAD_STATE);
        return;
    }

    /* POSIT uses 1-based row/col. */
    bios_posit((uint8_t)(y + 1u), (uint8_t)(x + 1u));

    for (i = 0u; i < in_len; i++) {
        uint8_t ch = data[i];
        /* Map non-ASCII to '?' per spec §8 encoding rules. */
        if (ch < 0x20u || ch > 0x7Eu) ch = 0x3Fu; /* '?' */
        bios_chput(ch);
    }

    wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
}

static void cmd_read_input(volatile uint8_t *data, uint16_t data_cap)
{
    uint8_t  i;
    uint8_t  joy1_dir, joy2_dir;
    uint8_t  trig_a1, trig_b1, trig_a2, trig_b2;
    uint8_t  joy1 = 0, joy2 = 0;

    if (data_cap < 16u) {
        wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_OVERFLOW);
        return;
    }

    /* Read all 11 keyboard matrix rows (SNSMAT). */
    for (i = 0u; i < 11u; i++) {
        data[i] = bios_snsmat(i);
    }

    /* Joystick 1 — GTSTCK(1) gives direction 0-8; GTTRIG for buttons. */
    joy1_dir = bios_gtstck(1u);
    trig_a1  = bios_gttrig(0u); /* joy1 trigger A (0xFF=pressed) */
    trig_b1  = bios_gttrig(1u); /* joy1 trigger B */
    /* Encode: bit0=Up bit1=Down bit2=Left bit3=Right bit4=TrigA bit5=TrigB, active-low */
    if (joy1_dir == 1u || joy1_dir == 2u || joy1_dir == 8u) joy1 |= (1u << 0); /* Up */
    if (joy1_dir == 4u || joy1_dir == 5u || joy1_dir == 6u) joy1 |= (1u << 1); /* Down */
    if (joy1_dir == 6u || joy1_dir == 7u || joy1_dir == 8u) joy1 |= (1u << 2); /* Left */
    if (joy1_dir == 2u || joy1_dir == 3u || joy1_dir == 4u) joy1 |= (1u << 3); /* Right */
    if (trig_a1 == 0u) joy1 |= (1u << 4); /* 0xFF=pressed → active-low: 0=pressed */
    if (trig_b1 == 0u) joy1 |= (1u << 5);

    /* Joystick 2 */
    joy2_dir = bios_gtstck(2u);
    trig_a2  = bios_gttrig(2u);
    trig_b2  = bios_gttrig(3u);
    if (joy2_dir == 1u || joy2_dir == 2u || joy2_dir == 8u) joy2 |= (1u << 0);
    if (joy2_dir == 4u || joy2_dir == 5u || joy2_dir == 6u) joy2 |= (1u << 1);
    if (joy2_dir == 6u || joy2_dir == 7u || joy2_dir == 8u) joy2 |= (1u << 2);
    if (joy2_dir == 2u || joy2_dir == 3u || joy2_dir == 4u) joy2 |= (1u << 3);
    if (trig_a2 == 0u) joy2 |= (1u << 4);
    if (trig_b2 == 0u) joy2 |= (1u << 5);

    data[11] = joy1;
    data[12] = joy2;
    data[13] = 0u;
    data[14] = 0u;
    data[15] = 0u;

    wr16(MAILBOX_BASE + MBX_OUT_LEN, 16u); /* sizeof(InputSnapshot) */
    wr16(MAILBOX_BASE + MBX_STATUS,  MENU_OK);
}

static void cmd_idle(uint32_t arg0)
{
    /* Busy-wait approximately arg0 milliseconds (low 16 bits).
     * At Z80 3.5 MHz, ~3500 cycles/ms.  This is a rough approximation;
     * exact timing is not required by the spec. */
    uint16_t ms = (uint16_t)(arg0 & 0xFFFFu);
    uint16_t i, j;
    for (i = 0u; i < ms; i++) {
        for (j = 0u; j < 875u; j++) {
            /* 4-cycle inner loop (approx): djnz */
            __asm
                nop
            __endasm;
        }
    }
    wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
}

/* ---------------------------------------------------------------------------
 * Main poll loop
 * ---------------------------------------------------------------------------*/

void stub_main(void)
{
    uint8_t gen = detect_msx_gen();

#ifdef MENU_STUB_SELF_TEST
    /* Write a known signature so the RP2350 can verify the stub loaded. */
    wr16(MAILBOX_BASE + MBX_STATUS,  0xBEEFu);
    wr16(MAILBOX_BASE + MBX_OUT_LEN, 0xCAFEu);
    /* Halt — RP2350 polls for the signature. */
    while (1) {}
#endif

    /* Initialise: set TEXT_40, fill header caps. */
    __asm
        call 0x006C    ; INITXT — 40-col text mode
    __endasm;
    g_mode = MODE_TEXT_40;

    wr32(HEADER_BASE + HDR_HOST_CAPS, build_host_caps(gen));
    wr32(HEADER_BASE + HDR_VDP_CAPS,  build_vdp_caps(gen));

    /* Main command poll loop. */
    while (1) {
        uint16_t cmd_seq  = rd16(MAILBOX_BASE + MBX_CMD_SEQ);
        uint16_t resp_seq = rd16(MAILBOX_BASE + MBX_RESP_SEQ);

        if (cmd_seq == resp_seq) {
            continue; /* no pending command */
        }

        /* New command — read fields. */
        uint16_t cmd_id = rd16(MAILBOX_BASE + MBX_CMD_ID);
        uint32_t arg0   = rd32(MAILBOX_BASE + MBX_ARG0);
        uint16_t in_len = rd16(MAILBOX_BASE + MBX_IN_LEN);

        /* Validate in_len. */
        if (in_len > DATA_LEN) {
            wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_OVERFLOW);
            wr16(MAILBOX_BASE + MBX_RESP_SEQ, cmd_seq);
            continue;
        }

        /* Dispatch. */
        switch (cmd_id) {
            case CMD_NOP:
                wr16(MAILBOX_BASE + MBX_STATUS, MENU_OK);
                break;

            case CMD_GET_HOST_INFO:
                cmd_get_host_info(DATA_BASE, DATA_LEN);
                break;

            case CMD_SET_MODE:
                cmd_set_mode((uint8_t)(arg0 & 0xFFu));
                break;

            case CMD_CLEAR:
                cmd_clear();
                break;

            case CMD_PUT_TEXT:
                cmd_put_text(arg0, in_len, DATA_BASE);
                break;

            case CMD_READ_INPUT:
                cmd_read_input(DATA_BASE, DATA_LEN);
                break;

            case CMD_IDLE:
                cmd_idle(arg0);
                break;

            default:
                wr16(MAILBOX_BASE + MBX_STATUS, MENU_E_UNSUPPORTED);
                break;
        }

        /* Signal completion: write resp_seq LAST (spec §8 ordering rule). */
        wr16(MAILBOX_BASE + MBX_RESP_SEQ, cmd_seq);
    }
}
