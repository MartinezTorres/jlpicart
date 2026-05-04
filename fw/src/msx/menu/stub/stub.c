/* stub.c — Z80 menu stub.
 *
 * Compiled with SDCC for Z80 target.  No standard library; all BIOS access
 * via inline assembly.  Structs are laid out by-hand to match the packed
 * RP2350-side definitions in menu_host_abi.h — any layout change there must
 * be reflected here.
 *
 * Page layout (Z80 addresses when page mapped at 0x4000):
 *   0x4000  MenuStubHeader  (64 bytes, written by RP2350 before stub starts)
 *   0x4040  MenuMailboxRegs (64 bytes, shared RW)
 *   0x4100  data buffer     (MENU_USABLE_DATA_LEN = 0x3700 bytes)
 *   0x7800  stub code       (this file, up to 2 KB)
 */

#include "stub_bios.h"

/* ---------------------------------------------------------------------------
 * Page-base constants
 * --------------------------------------------------------------------------- */

#define PAGE_BASE       0x4000u
#define MBX_BASE        (PAGE_BASE + 0x0040u)
#define DATA_BASE       (PAGE_BASE + 0x0100u)
#define HDR_BASE        (PAGE_BASE + 0x0000u)

/* Maximum bytes the RP2350 may put in the data buffer for one command. */
#define DATA_CAP        0x3700u

/* ---------------------------------------------------------------------------
 * Mailbox field access (volatile, byte-addressed)
 * --------------------------------------------------------------------------- */

#define MBX_CMD_SEQ     (*(volatile unsigned int  *)(MBX_BASE + 0))
#define MBX_RESP_SEQ    (*(volatile unsigned int  *)(MBX_BASE + 2))
#define MBX_CMD_ID      (*(volatile unsigned int  *)(MBX_BASE + 4))
#define MBX_STATUS      (*(volatile unsigned int  *)(MBX_BASE + 6))
/* arg0 low/high words (SDCC uint32_t = two 16-bit words, little-endian) */
#define MBX_ARG0_LO     (*(volatile unsigned int  *)(MBX_BASE + 8))
#define MBX_ARG0_HI     (*(volatile unsigned int  *)(MBX_BASE + 10))
#define MBX_IN_LEN      (*(volatile unsigned int  *)(MBX_BASE + 24))
#define MBX_OUT_LEN     (*(volatile unsigned int  *)(MBX_BASE + 26))

/* MenuStubHeader: host_caps at offset 16, vdp_caps at offset 20 (32-bit each). */
#define HDR_HOST_CAPS_LO (*(volatile unsigned int *)(HDR_BASE + 16))
#define HDR_HOST_CAPS_HI (*(volatile unsigned int *)(HDR_BASE + 18))
#define HDR_VDP_CAPS_LO  (*(volatile unsigned int *)(HDR_BASE + 20))
#define HDR_VDP_CAPS_HI  (*(volatile unsigned int *)(HDR_BASE + 22))

/* Data buffer pointer. */
#define DATA_BUF        ((volatile unsigned char *)DATA_BASE)

/* ---------------------------------------------------------------------------
 * Command and status codes
 * --------------------------------------------------------------------------- */

#define CMD_NOP            0x0000u
#define CMD_GET_HOST_INFO  0x0001u
#define CMD_SET_MODE       0x0002u
#define CMD_CLEAR          0x0003u
#define CMD_PUT_TEXT       0x0004u
#define CMD_READ_INPUT     0x0005u
#define CMD_IDLE           0x000Au
#define CMD_LAUNCH         0x000Bu

#define MENU_OK            0x0000u
#define MENU_E_UNSUPPORTED 0x0001u
#define MENU_E_BAD_ARG     0x0002u
#define MENU_E_BAD_STATE   0x0003u
#define MENU_E_OVERFLOW    0x0004u
#define MENU_E_HW          0x0005u

/* ---------------------------------------------------------------------------
 * Inline BIOS call wrappers
 * --------------------------------------------------------------------------- */

static void bios_chgmod(unsigned char mode) __naked
{
    mode;
    __asm
        ld   hl, #2
        add  hl, sp
        ld   a, (hl)
        call BIOS_CHGMOD
        ret
    __endasm;
}

static void bios_posit(unsigned char row, unsigned char col) __naked
{
    row; col;
    /* SDCC pushes right-to-left: sp+0=return, sp+2=row, sp+4=col.
     * BIOS_POSIT expects H=row (1-based), L=col (1-based).
     * Load col first into C to survive the HL reuse when loading row. */
    __asm
        ld   hl, #4
        add  hl, sp
        ld   a, (hl)        ; a = col
        ld   c, a           ; save col in C
        ld   hl, #2
        add  hl, sp
        ld   h, (hl)        ; H = row (clobbers HL address; L now garbage)
        ld   l, c           ; L = col (restored from C)
        call BIOS_POSIT
        ret
    __endasm;
}

static void bios_chput(unsigned char ch) __naked
{
    ch;
    __asm
        ld   hl, #2
        add  hl, sp
        ld   a, (hl)
        call BIOS_CHPUT
        ret
    __endasm;
}

/* SNSMAT: sense keyboard matrix row; returns byte for that row. */
static unsigned char bios_snsmat(unsigned char row) __naked
{
    row;
    __asm
        ld   hl, #2
        add  hl, sp
        ld   a, (hl)
        call BIOS_SNSMAT
        ld   l, a
        ld   h, #0
        ret
    __endasm;
}

/* Read PSG register. */
static unsigned char psg_read(unsigned char reg) __naked
{
    reg;
    __asm
        ld   hl, #2
        add  hl, sp
        ld   a, (hl)
        out  (PSG_REG), a
        in   a, (PSG_READ)
        ld   l, a
        ld   h, #0
        ret
    __endasm;
}

/* Busy-wait approximately N milliseconds (rough, calibrated to ~3.58 MHz Z80).
 * Each inner loop ~107 T-states ≈ 30 µs at 3.58 MHz → ~33 iters per ms.
 * Acceptable error: ±30% (good enough for animation pacing). */
static void busy_wait_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; ++i) {
        for (j = 0; j < 33u; ++j) {
            __asm nop __endasm;
            __asm nop __endasm;
            __asm nop __endasm;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Command handlers
 * --------------------------------------------------------------------------- */

/* HostInfo layout (matches RP2350-side, 12 bytes):
 *   uint8_t msx_gen, vram_kb, text_cols, reserved0;
 *   uint32_t host_caps, vdp_caps; */
static void cmd_get_host_info(void)
{
    unsigned char msxver = *(volatile unsigned char *)BIOS_MSXVER;
    unsigned char gen    = (msxver < 4u) ? (unsigned char)(msxver + 1u) : 1u;

    /* VRAM: MSX1 = 16 KB, MSX2+ = 128 KB, MSX2 = 64 KB (best-effort). */
    unsigned char vram_kb  = (gen == 1u) ? 16u : (gen >= 3u) ? 128u : 64u;
    unsigned char text_cols = 40u;  /* default; 80-col requires explicit detection */

    volatile unsigned char *dst = DATA_BUF;
    dst[0]  = gen;
    dst[1]  = vram_kb;
    dst[2]  = text_cols;
    dst[3]  = 0u;
    /* host_caps (4 bytes LE) — copy from header (filled by RP2350 or zeroed) */
    dst[4]  = (unsigned char)(HDR_HOST_CAPS_LO & 0xFFu);
    dst[5]  = (unsigned char)(HDR_HOST_CAPS_LO >> 8);
    dst[6]  = (unsigned char)(HDR_HOST_CAPS_HI & 0xFFu);
    dst[7]  = (unsigned char)(HDR_HOST_CAPS_HI >> 8);
    /* vdp_caps (4 bytes LE) */
    dst[8]  = (unsigned char)(HDR_VDP_CAPS_LO & 0xFFu);
    dst[9]  = (unsigned char)(HDR_VDP_CAPS_LO >> 8);
    dst[10] = (unsigned char)(HDR_VDP_CAPS_HI & 0xFFu);
    dst[11] = (unsigned char)(HDR_VDP_CAPS_HI >> 8);

    MBX_OUT_LEN = 12u;
    MBX_STATUS  = MENU_OK;
}

static unsigned char g_mode = 0u;  /* current screen mode */

static void cmd_set_mode(void)
{
    unsigned char mode = (unsigned char)(MBX_ARG0_LO & 0xFFu);
    if (mode > 2u) {
        MBX_STATUS = MENU_E_UNSUPPORTED;
        return;
    }
    bios_chgmod(mode);  /* clears the screen as a side effect */
    g_mode     = mode;
    MBX_STATUS = MENU_OK;
}

static void cmd_clear(void)
{
    /* Re-issuing CHGMOD with the same mode resets/clears the screen. */
    bios_chgmod(g_mode);
    MBX_STATUS = MENU_OK;
}

static void cmd_put_text(void)
{
    unsigned int in_len = MBX_IN_LEN;
    if (in_len > DATA_CAP) {
        MBX_STATUS = MENU_E_OVERFLOW;
        return;
    }
    if (g_mode != 0u) {
        MBX_STATUS = MENU_E_BAD_STATE;
        return;
    }
    /* arg0: x in low byte (col, 0-based), y in next byte (row, 0-based).
     * POSIT expects 1-based row/col. */
    unsigned char col = (unsigned char)( MBX_ARG0_LO        & 0xFFu);
    unsigned char row = (unsigned char)((MBX_ARG0_LO >> 8)  & 0xFFu);
    bios_posit((unsigned char)(row + 1u), (unsigned char)(col + 1u));

    volatile unsigned char *src = DATA_BUF;
    unsigned int i;
    for (i = 0u; i < in_len; ++i) {
        unsigned char ch = src[i];
        /* Spec: substitute '?' for non-ASCII if no mapping available. */
        if (ch < 0x20u || ch > 0x7Eu) ch = 0x3Fu;
        bios_chput(ch);
    }
    MBX_STATUS = MENU_OK;
}

static void cmd_read_input(void)
{
    /* InputSnapshot: kbd_rows[11] + joy1 + joy2 + reserved[3] = 16 bytes. */
    volatile unsigned char *dst = DATA_BUF;
    unsigned char row;
    for (row = 0u; row < 11u; ++row) {
        dst[row] = bios_snsmat(row);
    }
    /* Joystick 1: PSG register 14, bits 0–5 (active low, 0=pressed). */
    dst[11] = psg_read(PSG_R14_JOY) & 0x3Fu;
    /* Joystick 2: not available via a single PSG register on all MSX;
     * return 0xFF (no buttons pressed) as a safe default. */
    dst[12] = 0xFFu;
    dst[13] = 0u; dst[14] = 0u; dst[15] = 0u;

    MBX_OUT_LEN = 16u;
    MBX_STATUS  = MENU_OK;
}

static void cmd_idle(void)
{
    unsigned int ms = MBX_ARG0_LO;
    busy_wait_ms(ms);
    MBX_STATUS = MENU_OK;
}

/* ---------------------------------------------------------------------------
 * Dispatch
 * --------------------------------------------------------------------------- */

static void dispatch(void)
{
    unsigned int cmd = MBX_CMD_ID;
    MBX_OUT_LEN = 0u;

    switch (cmd) {
    case CMD_NOP:           MBX_STATUS = MENU_OK;           break;
    case CMD_GET_HOST_INFO: cmd_get_host_info();             break;
    case CMD_SET_MODE:      cmd_set_mode();                  break;
    case CMD_CLEAR:         cmd_clear();                     break;
    case CMD_PUT_TEXT:      cmd_put_text();                  break;
    case CMD_READ_INPUT:    cmd_read_input();                break;
    case CMD_IDLE:          cmd_idle();                      break;
    case CMD_LAUNCH:
        /* ROM has been remapped by RP2350.  Acknowledge before jumping so
         * the RP2350 sees the response — the poll loop is never reached again. */
        MBX_STATUS   = MENU_OK;
        MBX_RESP_SEQ = MBX_CMD_SEQ;
        __asm
            jp 0x0000
        __endasm;
        break; /* unreachable; suppresses SDCC fallthrough warning */
    default:                MBX_STATUS = MENU_E_UNSUPPORTED; break;
    }
}

/* ---------------------------------------------------------------------------
 * Entry point (called from crt0.s)
 * --------------------------------------------------------------------------- */

void main(void)
{
    unsigned char msxver = *(volatile unsigned char *)BIOS_MSXVER;

    /* Populate host_caps in the header (RP2350 reads this after stub inits). */
    unsigned int host_caps = 0u;
    if (msxver == 0u) host_caps |= 0x0001u;  /* MSX1 */
    if (msxver == 1u) host_caps |= 0x0002u;  /* MSX2 */
    if (msxver == 2u) host_caps |= 0x0004u;  /* MSX2+ */
    if (msxver == 3u) host_caps |= 0x0008u;  /* turboR */
    host_caps |= 0x0020u;                     /* BIOS_KBD always present */
    HDR_HOST_CAPS_LO = host_caps;
    HDR_HOST_CAPS_HI = 0u;

    /* VDP caps: TMS9918-compatible port writes always supported. */
    HDR_VDP_CAPS_LO = 0x0001u;
    HDR_VDP_CAPS_HI = 0u;

    /* Signal that init is complete: resp_seq = cmd_seq (no command pending). */
    MBX_RESP_SEQ = MBX_CMD_SEQ;

    /* Main loop: spin until RP2350 posts a command, dispatch, acknowledge. */
    for (;;) {
        unsigned int seq;
        /* Spin-wait for new command. */
        do { seq = MBX_CMD_SEQ; } while (seq == MBX_RESP_SEQ);

        dispatch();

        /* Acknowledge: write resp_seq = cmd_seq last (spec §8 ordering rule). */
        MBX_RESP_SEQ = MBX_CMD_SEQ;
    }
}
