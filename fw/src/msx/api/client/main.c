/* main.c — JLPiCart Z80 API reference client.
 *
 * Issues core.GET_API_INFO and prints a hex dump of the response.
 * Compiled with pinned SDCC 4.5.0 targeting MSX Z80.
 *
 * Assumptions:
 * - API window is mapped at 0x8000 (Z80 page 2, subslot 2).
 * - MSX BIOS CHPUT at 0x00A2 is reachable from page 1 via direct call.
 * - No interrupts are assumed or enabled.
 * - Runs on MSX1 or later; no model-specific features required.
 *
 * Uses ring-framed request/response (see api_window.cc) and System service 0x00.
 */

#include <stdint.h>

/* API window base address (subslot 2, Z80 page 2 = 0x8000–0xBFFF). */
#define API_WIN  ((volatile uint8_t *)0x8000u)

/* Offsets within the window. */
#define WIN_SIG_OFS          0x0000u
#define WIN_API_MAJOR_OFS    0x0004u
#define WIN_API_MINOR_OFS    0x0005u
#define WIN_REGS_OFS         0x0040u
#define WIN_REQ_RING_OFS     0x0060u
#define WIN_RSP_RING_OFS     0x0268u

/* Ring header layout (RingHeader, 8 bytes). */
#define RING_HEAD_OFS  0u
#define RING_TAIL_OFS  2u
#define RING_SIZE_OFS  4u
#define RING_DATA_OFS  8u

/* ApiRegs layout (at WIN_REGS_OFS). */
#define REGS_HOST_KICK_OFS  0u

/* Service / method IDs. */
#define SVC_SYSTEM          0x00u
#define SYS_GET_API_INFO    0x00u

/* Status OK. */
#define API_OK 0x0000u

/* Response buffer size (must be >= API_MAX_FRAME = 256). */
#define RSP_BUF  256u

/* ---------------------------------------------------------------------------
 * Low-level helpers
 * ---------------------------------------------------------------------------*/

static uint16_t read_u16_le(volatile const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u16_le(volatile uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

/* MSX BIOS CHPUT: print one character (Z80 reg A = char). */
void bios_chput(uint8_t c) __naked
{
    c; /* suppress unused-arg warning */
    __asm
        ld   a, l          ; SDCC passes uint8_t in L
        call 0x00A2        ; MSX BIOS CHPUT
        ret
    __endasm;
}

static void print_str(const char *s)
{
    while (*s) bios_chput((uint8_t)*s++);
}

static void print_hex8(uint8_t v)
{
    const char *hex = "0123456789ABCDEF";
    bios_chput((uint8_t)hex[v >> 4]);
    bios_chput((uint8_t)hex[v & 0x0Fu]);
}

static void print_hex16(uint16_t v)
{
    print_hex8((uint8_t)(v >> 8));
    print_hex8((uint8_t)(v & 0xFFu));
}

/* ---------------------------------------------------------------------------
 * Ring push: write msg_bytes (msg_len bytes) as a framed request.
 * Returns 0 on success.
 * ---------------------------------------------------------------------------*/

static uint8_t ring_push(volatile uint8_t *ring_base,
                         const uint8_t *msg, uint16_t msg_len)
{
    volatile uint8_t *data = ring_base + RING_DATA_OFS;
    uint16_t size      = read_u16_le(ring_base + RING_SIZE_OFS);
    uint16_t head      = read_u16_le(ring_base + RING_HEAD_OFS);
    uint16_t tail      = read_u16_le(ring_base + RING_TAIL_OFS);
    uint16_t frame_len = (uint16_t)(msg_len + 2u);
    uint16_t used;
    uint16_t free_bytes;
    uint16_t to_end;
    uint16_t i;

    /* Compute free space. */
    if (head >= tail) {
        used = (uint16_t)(head - tail);
    } else {
        used = (uint16_t)(size - (tail - head));
    }
    free_bytes = (uint16_t)(size - used - 1u);

    if (free_bytes < frame_len) {
        return 1; /* ring full */
    }

    to_end = (uint16_t)(size - head);
    if (frame_len > to_end) {
        /* Write wrap marker if room, then wrap head. */
        if (to_end >= 2u) {
            data[head]     = 0x00;
            data[head + 1] = 0x00;
        }
        head = 0;
    }

    /* Write frame_len (LE) then msg_bytes. */
    data[head]     = (uint8_t)(frame_len & 0xFFu);
    data[head + 1] = (uint8_t)(frame_len >> 8);
    for (i = 0; i < msg_len; i++) {
        data[(uint16_t)(head + 2u + i)] = msg[i];
    }

    head = (uint16_t)((head + frame_len) % size);
    write_u16_le(ring_base + RING_HEAD_OFS, head);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Ring pop: read one framed message (skipping wrap markers).
 * Returns msg_len (bytes after frame_len prefix), or 0 if empty.
 * ---------------------------------------------------------------------------*/

static uint16_t ring_pop(volatile uint8_t *ring_base,
                         uint8_t *dst, uint16_t dst_max)
{
    volatile uint8_t *data = ring_base + RING_DATA_OFS;
    uint16_t size  = read_u16_le(ring_base + RING_SIZE_OFS);
    uint16_t head;
    uint16_t tail;
    uint16_t frame_len;
    uint16_t msg_len;
    uint16_t i;

retry:
    head = read_u16_le(ring_base + RING_HEAD_OFS);
    tail = read_u16_le(ring_base + RING_TAIL_OFS);

    if (head == tail) {
        return 0; /* empty */
    }

    frame_len = read_u16_le(data + tail);

    if (frame_len == 0) {
        /* Wrap marker: advance tail to 0. */
        write_u16_le(ring_base + RING_TAIL_OFS, 0);
        goto retry;
    }

    if (frame_len < 2u) {
        /* Malformed: skip 2 bytes. */
        write_u16_le(ring_base + RING_TAIL_OFS, (uint16_t)((tail + 2u) % size));
        return 0;
    }

    msg_len = (uint16_t)(frame_len - 2u);
    if (msg_len > dst_max) {
        msg_len = dst_max; /* truncate */
    }

    for (i = 0; i < msg_len; i++) {
        dst[i] = data[(uint16_t)(tail + 2u + i)];
    }

    write_u16_le(ring_base + RING_TAIL_OFS,
                 (uint16_t)((tail + frame_len) % size));
    return msg_len;
}

/* ---------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------------*/

void main(void)
{
    volatile uint8_t *win = API_WIN;
    uint8_t  req_msg[16]; /* MsgHeader only — no payload for GET_API_INFO */
    uint8_t  rsp_buf[RSP_BUF];
    uint16_t rsp_len;
    uint16_t i;
    uint16_t timeout;

    /* Verify API window signature. */
    if (win[WIN_SIG_OFS]     != 'J' ||
        win[WIN_SIG_OFS + 1] != 'L' ||
        win[WIN_SIG_OFS + 2] != 'P' ||
        win[WIN_SIG_OFS + 3] != '1') {
        print_str("JLPiCart: no API\r\n");
        return;
    }

    print_str("JLPiCart API v");
    print_hex8(win[WIN_API_MAJOR_OFS]);
    bios_chput('.');
    print_hex8(win[WIN_API_MINOR_OFS]);
    print_str("\r\n");

    /* Build MsgHeader for GET_API_INFO: 16 bytes, all zero except seq/svc/method. */
    for (i = 0; i < 16u; i++) req_msg[i] = 0x00;
    req_msg[0]  = 0x01; req_msg[1] = 0x00; /* seq = 1 (little-endian) */
    req_msg[2]  = SVC_SYSTEM;               /* service */
    req_msg[3]  = SYS_GET_API_INFO;         /* method */
    req_msg[10] = 0xFF; req_msg[11] = 0xFF; /* scratch_ofs = 0xFFFF (none) */

    /* Push request frame. */
    if (ring_push(win + WIN_REQ_RING_OFS, req_msg, 16u) != 0) {
        print_str("Ring full\r\n");
        return;
    }

    /* Doorbell: increment host_kick. */
    win[WIN_REGS_OFS + REGS_HOST_KICK_OFS]++;

    /* Poll response ring (~1 second at Z80 3.5 MHz). */
    timeout = 60000u;
    while (timeout > 0u) {
        uint16_t rh = read_u16_le(win + WIN_RSP_RING_OFS + RING_HEAD_OFS);
        uint16_t rt = read_u16_le(win + WIN_RSP_RING_OFS + RING_TAIL_OFS);
        if (rh != rt) break;
        timeout--;
    }

    if (timeout == 0u) {
        print_str("Timeout\r\n");
        return;
    }

    /* Pop and print response. */
    rsp_len = ring_pop(win + WIN_RSP_RING_OFS, rsp_buf, RSP_BUF);
    if (rsp_len == 0u) {
        print_str("Empty response\r\n");
        return;
    }

    /* Check MsgHeader status (bytes 6–7 = status, little-endian). */
    {
        uint16_t status = (uint16_t)((uint16_t)rsp_buf[6] | ((uint16_t)rsp_buf[7] << 8));
        if (status != API_OK) {
            print_str("Error: 0x");
            print_hex16(status);
            print_str("\r\n");
            return;
        }
    }

    /* Hex dump of msg_bytes (MsgHeader + payload). */
    print_str("Response hex:\r\n");
    for (i = 0u; i < rsp_len; i++) {
        print_hex8(rsp_buf[i]);
        bios_chput(' ');
        if ((i & 0x0Fu) == 0x0Fu) {
            print_str("\r\n");
        }
    }
    if ((rsp_len & 0x0Fu) != 0u) {
        print_str("\r\n");
    }

    print_str("Done.\r\n");
}
