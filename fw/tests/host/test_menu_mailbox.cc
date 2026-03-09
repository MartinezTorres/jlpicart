// test_menu_mailbox.cc — Host tests for MenuMailbox (Stage 5, spec.md §8).
//
// Tests run on the build host (Linux amd64) with no RP2350 SDK.
// The MenuMailbox class is fully portable; Z80 stub responses are simulated
// by writing directly through raw_regs().

#include "msx/menu/menu_host_abi.h"

#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, #expr); \
            ++g_fail; \
        } else { \
            ++g_pass; \
        } \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

// Simulate a Z80 stub acknowledging the last command.
static void sim_z80_ack(MenuMailbox& mbx, uint16_t status = MENU_OK, uint16_t out_len = 0)
{
    MenuMailboxRegs* r = mbx.raw_regs();
    r->status  = status;
    r->out_len = out_len;
    // Z80 writes resp_seq last, matching cmd_seq, to signal completion.
    r->resp_seq = r->cmd_seq;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_not_initialized_before_init()
{
    MenuMailbox mbx;
    CHECK(!mbx.initialized());
    CHECK(!mbx.pending());
}

static void test_init_sets_header_fields()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    CHECK(mbx.initialized());

    const auto* hdr = reinterpret_cast<const MenuStubHeader*>(page + MENU_HEADER_OFS);
    CHECK(hdr->sig[0] == 'J');
    CHECK(hdr->sig[1] == 'L');
    CHECK(hdr->sig[2] == 'M');
    CHECK(hdr->sig[3] == 'N');
    CHECK_EQ(hdr->abi_major,   MENU_ABI_MAJOR);
    CHECK_EQ(hdr->abi_minor,   MENU_ABI_MINOR);
    CHECK_EQ(hdr->header_len,  static_cast<uint16_t>(sizeof(MenuStubHeader)));
    CHECK_EQ(hdr->mailbox_ofs, MENU_MAILBOX_OFS);
    CHECK_EQ(hdr->data_ofs,    MENU_DATA_OFS);
    CHECK_EQ(hdr->data_len,    MENU_DATA_LEN);
    CHECK_EQ(hdr->stub_entry,  static_cast<uint16_t>(0x0100u));
    // host_caps and vdp_caps are 0 until the Z80 stub fills them in.
    CHECK_EQ(hdr->host_caps,   0u);
    CHECK_EQ(hdr->vdp_caps,    0u);
}

static void test_init_zeros_mailbox()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    // Pre-dirty the page so we know init actually zeroed it.
    memset(page, 0xFF, MENU_PAGE_SIZE);
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    const auto* r = reinterpret_cast<const MenuMailboxRegs*>(page + MENU_MAILBOX_OFS);
    CHECK_EQ(r->cmd_seq,  0u);
    CHECK_EQ(r->resp_seq, 0u);
}

static void test_send_command_basic()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    bool ok = mbx.send_command(MENU_CMD_NOP);
    CHECK(ok);
    CHECK(mbx.pending());

    // cmd_seq should have advanced to 1.
    CHECK_EQ(mbx.raw_regs()->cmd_seq, 1u);
    CHECK_EQ(mbx.raw_regs()->cmd_id,  MENU_CMD_NOP);
}

static void test_send_command_rejected_while_pending()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    mbx.send_command(MENU_CMD_NOP);
    bool second = mbx.send_command(MENU_CMD_NOP);
    CHECK(!second);
}

static void test_tick_returns_false_while_waiting()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    mbx.send_command(MENU_CMD_NOP);

    // resp_seq still 0, cmd_seq = 1 — stub hasn't responded yet.
    CHECK(!mbx.tick());
    CHECK(mbx.pending());
}

static void test_tick_returns_true_on_ack()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    mbx.send_command(MENU_CMD_NOP);

    sim_z80_ack(mbx);
    CHECK(mbx.tick());
    CHECK(!mbx.pending());
}

static void test_tick_captures_status_and_out_len()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    mbx.send_command(MENU_CMD_GET_HOST_INFO);

    sim_z80_ack(mbx, MENU_OK, 12u);
    mbx.tick();

    CHECK_EQ(mbx.last_status(),  MENU_OK);
    CHECK_EQ(mbx.last_out_len(), 12u);
}

static void test_tick_captures_error_status()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    mbx.send_command(MENU_CMD_SET_MODE, MENU_MODE_BITMAP);

    sim_z80_ack(mbx, MENU_E_UNSUPPORTED, 0u);
    mbx.tick();

    CHECK_EQ(mbx.last_status(), MENU_E_UNSUPPORTED);
    CHECK_EQ(mbx.last_out_len(), 0u);
}

static void test_send_command_writes_args()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    mbx.send_command(MENU_CMD_PUT_TEXT,
                     /*arg0=*/0x0205u, /*arg1=*/0u, /*arg2=*/0u, /*arg3=*/0xDEADu);

    const MenuMailboxRegs* r = mbx.raw_regs();
    CHECK_EQ(r->cmd_id, MENU_CMD_PUT_TEXT);
    CHECK_EQ(r->arg0,   0x0205u);
    CHECK_EQ(r->arg3,   0xDEADu);
}

static void test_send_command_copies_payload()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    const uint8_t payload[] = { 'H', 'e', 'l', 'l', 'o' };
    mbx.send_command(MENU_CMD_PUT_TEXT, 0u, 0u, 0u, 0u,
                     payload, static_cast<uint16_t>(sizeof(payload)));

    CHECK_EQ(mbx.raw_regs()->in_len, static_cast<uint16_t>(sizeof(payload)));
    // Data is at page + MENU_DATA_OFS.
    const uint8_t* data = page + MENU_DATA_OFS;
    CHECK(memcmp(data, payload, sizeof(payload)) == 0);
}

static void test_send_command_null_payload_ignored()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    // in_len > 0 but in_data = nullptr — no copy, no crash.
    bool ok = mbx.send_command(MENU_CMD_PUT_TEXT, 0u, 0u, 0u, 0u, nullptr, 5u);
    CHECK(ok);
    CHECK_EQ(mbx.raw_regs()->in_len, 5u);
}

static void test_send_command_rejects_oversized_payload()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    // MENU_DATA_LEN + 1 must be rejected.
    bool ok = mbx.send_command(MENU_CMD_VRAM_WRITE, 0u, 0u, 0u, 0u,
                                nullptr, static_cast<uint16_t>(MENU_DATA_LEN + 1u));
    CHECK(!ok);
    CHECK(!mbx.pending()); // should not have become pending
}

static void test_data_buf_readable_after_tick()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    mbx.send_command(MENU_CMD_GET_HOST_INFO);

    // Simulate stub writing a HostInfo into the data buffer.
    HostInfo hi = {};
    hi.msx_gen  = 2u;
    hi.vram_kb  = 128u;
    hi.text_cols = 80u;
    hi.host_caps = MENU_HOST_CAP_MSX2 | MENU_HOST_CAP_TEXT_80;
    memcpy(page + MENU_DATA_OFS, &hi, sizeof(hi));

    sim_z80_ack(mbx, MENU_OK, static_cast<uint16_t>(sizeof(hi)));
    mbx.tick();

    HostInfo out = {};
    memcpy(&out, mbx.data_buf(), sizeof(out));
    CHECK_EQ(out.msx_gen,   2u);
    CHECK_EQ(out.vram_kb,   128u);
    CHECK_EQ(out.text_cols, 80u);
    CHECK(out.host_caps & MENU_HOST_CAP_MSX2);
    CHECK(out.host_caps & MENU_HOST_CAP_TEXT_80);
}

static void test_multiple_sequential_commands()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    // Issue three commands in sequence; verify cmd_seq increments each time.
    for (uint16_t i = 1; i <= 3; ++i) {
        bool ok = mbx.send_command(MENU_CMD_NOP);
        CHECK(ok);
        CHECK_EQ(mbx.raw_regs()->cmd_seq, i);

        sim_z80_ack(mbx);
        bool done = mbx.tick();
        CHECK(done);
        CHECK(!mbx.pending());
    }
}

static void test_cmd_seq_wraps_correctly()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);

    // Force cmd_seq near uint16_t max to test wrap.
    mbx.raw_regs()->cmd_seq  = 0xFFFFu;
    mbx.raw_regs()->resp_seq = 0xFFFFu;

    bool ok = mbx.send_command(MENU_CMD_NOP);
    CHECK(ok);
    // 0xFFFF + 1 wraps to 0x0000.
    CHECK_EQ(mbx.raw_regs()->cmd_seq, 0x0000u);

    sim_z80_ack(mbx);
    CHECK(mbx.tick());
}

static void test_data_capacity()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    CHECK_EQ(mbx.data_capacity(), MENU_DATA_LEN);
}

static void test_tick_no_pending_returns_false()
{
    uint8_t page[MENU_PAGE_SIZE] = {};
    MenuMailbox mbx;
    mbx.init(page, 0x0100u);
    // No command was sent.
    CHECK(!mbx.tick());
}

static void test_tick_not_initialized_returns_false()
{
    MenuMailbox mbx; // not initialised
    CHECK(!mbx.tick());
}

static void test_send_command_not_initialized_returns_false()
{
    MenuMailbox mbx;
    CHECK(!mbx.send_command(MENU_CMD_NOP));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_not_initialized_before_init();
    test_init_sets_header_fields();
    test_init_zeros_mailbox();
    test_send_command_basic();
    test_send_command_rejected_while_pending();
    test_tick_returns_false_while_waiting();
    test_tick_returns_true_on_ack();
    test_tick_captures_status_and_out_len();
    test_tick_captures_error_status();
    test_send_command_writes_args();
    test_send_command_copies_payload();
    test_send_command_null_payload_ignored();
    test_send_command_rejects_oversized_payload();
    test_data_buf_readable_after_tick();
    test_multiple_sequential_commands();
    test_cmd_seq_wraps_correctly();
    test_data_capacity();
    test_tick_no_pending_returns_false();
    test_tick_not_initialized_returns_false();
    test_send_command_not_initialized_returns_false();

    fprintf(stderr, "\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
