// test_menu_app.cc — host tests for MenuApp (Stage 16).
//
// Tests verify the command sequence issued by the MenuApp state machine
// when driven by simulated Z80 acks.  No actual Z80 or MSX hardware is
// needed; the page buffer is driven directly.
//
// Key invariant: after ack(), the C++ pending_ bool is still true.
// The next tick_to_cmd() call will:
//   1. Call app.tick() which calls mbx_->tick() to clear pending_ and
//      advance the state machine (possibly issuing the next command).
//   2. Return immediately once a new command is pending.
//
// So the pattern for navigation is:
//   advance(N)               — drives N commands, leaves Nth acked but pending_=true
//   tick_to_cmd()            — processes Nth ack, issues (N+1)th command, returns it
//   ack_input(...)           — ack READ_INPUT with specific key state
//   tick_to_cmd()            — processes READ_INPUT ack (input handling), issues first
//                              command of next screen/re-render, returns it

#include "menu/menu_app.h"
#include "msx/menu/menu_host_abi.h"
#include "store/profile_store.h"
#include "store/profile_format.h"

#include "fat_test_env.h"
#include "test_helpers.h"
#include <cassert>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct MenuFixture {
    uint8_t      page[MENU_PAGE_SIZE];
    FatTestEnv   env;
    ProfileStore ps;
    MenuMailbox  mbx;
    MenuApp      app;

    MenuFixture() {
        memset(page, 0, sizeof(page));
        mbx.init(page, 0u);
        ps.init();
        app.init(mbx, ps);
    }

    // Simulate Z80 stub completing its own initialisation by writing host_caps.
    void sim_stub_init(uint32_t caps = MENU_HOST_CAP_MSX1 | MENU_HOST_CAP_BIOS_KBD) {
        MenuStubHeader* hdr = reinterpret_cast<MenuStubHeader*>(page + MENU_HEADER_OFS);
        hdr->host_caps = caps;
    }

    // Return the cmd_id of the most recently posted command.
    uint16_t last_cmd() {
        return mbx.raw_regs()->cmd_id;
    }

    // Simulate Z80 ack with optional response bytes written to the data buffer.
    void ack(const uint8_t* out = nullptr, uint16_t out_len = 0u,
             uint16_t status = MENU_OK) {
        if (out && out_len > 0u) {
            memcpy(page + MENU_DATA_OFS, out, out_len);
        }
        MenuMailboxRegs* r = mbx.raw_regs();
        r->status  = status;
        r->out_len = out_len;
        r->resp_seq = r->cmd_seq;
    }

    // Simulate ack with an InputSnapshot.
    // All rows default to 0xFF (released).  Pass 0 bit to simulate key press.
    void ack_input(uint8_t row7 = 0xFFu, uint8_t row8 = 0xFFu) {
        InputSnapshot inp = {};
        memset(inp.kbd_rows, 0xFFu, sizeof(inp.kbd_rows));
        inp.kbd_rows[7] = row7;
        inp.kbd_rows[8] = row8;
        inp.joy1 = 0xFFu;
        inp.joy2 = 0xFFu;
        ack(reinterpret_cast<const uint8_t*>(&inp), sizeof(inp));
    }

    // Simulate ack with a HostInfo response.
    void ack_host_info(uint8_t msx_gen = 1u) {
        HostInfo hi = {};
        hi.msx_gen   = msx_gen;
        hi.vram_kb   = 16u;
        hi.text_cols = 40u;
        hi.host_caps = MENU_HOST_CAP_MSX1 | MENU_HOST_CAP_BIOS_KBD;
        ack(reinterpret_cast<const uint8_t*>(&hi), static_cast<uint16_t>(sizeof(hi)));
    }

    // Tick until the mailbox has a new pending command, then return cmd_id.
    // Returns 0xFFFF if no pending command appears within max_ticks.
    uint16_t tick_to_cmd(int max_ticks = 40) {
        for (int i = 0; i < max_ticks; ++i) {
            app.tick();
            if (mbx.pending()) return last_cmd();
        }
        return 0xFFFFu;
    }

    // Drive n complete command cycles: tick_to_cmd → plain ack.
    void advance(int n) {
        for (int i = 0; i < n; ++i) {
            tick_to_cmd();
            ack();
        }
    }

    // Convenience: boot through to READ_HOST_INFO ack, then process it.
    // After this call the first command of MAIN (SET_MODE) is the next to arrive
    // from tick_to_cmd().
    void boot_to_main() {
        sim_stub_init();
        CHECK(tick_to_cmd() == MENU_CMD_GET_HOST_INFO);
        ack_host_info();
    }
};

// Bit mask helpers — active-low (0 = pressed, use XOR with 0xFF).
static constexpr uint8_t KEY_RETURN = 0x80u; // row 7 bit 7
static constexpr uint8_t KEY_DOWN   = 0x40u; // row 8 bit 6

// Number of rendering commands in one full MAIN screen pass (SET_MODE..footer).
// SET_MODE + CLEAR + header + game_info + 3×items + footer = 8.
static constexpr int MAIN_RENDER_CMDS = 8;

// ---------------------------------------------------------------------------
// test_menu_boot_to_boot_info — no cmd issued until host_caps non-zero
// ---------------------------------------------------------------------------

static void test_menu_boot_to_boot_info() {
    MenuFixture f;

    // Before Z80 init: tick() should not post any command.
    for (int i = 0; i < 5; ++i) {
        f.app.tick();
        CHECK(!f.mbx.pending());
    }

    f.sim_stub_init();

    // tick() transitions BOOT→BOOT_INFO and issues GET_HOST_INFO.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_GET_HOST_INFO);
}

// ---------------------------------------------------------------------------
// test_menu_boot_info_to_main — after GET_HOST_INFO ack, MAIN starts
// ---------------------------------------------------------------------------

static void test_menu_boot_info_to_main() {
    MenuFixture f;
    f.sim_stub_init();

    CHECK(f.tick_to_cmd() == MENU_CMD_GET_HOST_INFO);
    f.ack_host_info();

    // Processing BOOT_INFO response triggers switch_screen(MAIN).
    // First MAIN command is SET_MODE.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
    CHECK(f.mbx.raw_regs()->arg0 == MENU_MODE_TEXT_40);
}

// ---------------------------------------------------------------------------
// test_menu_main_renders — MAIN emits the full expected command sequence
// ---------------------------------------------------------------------------

static void test_menu_main_renders() {
    MenuFixture f;
    f.boot_to_main();

    static const uint16_t expected[] = {
        MENU_CMD_SET_MODE,   // step 0
        MENU_CMD_CLEAR,      // step 1
        MENU_CMD_PUT_TEXT,   // step 2  banner
        MENU_CMD_PUT_TEXT,   // step 3  game info / no-collection line
        MENU_CMD_PUT_TEXT,   // step 4  item 0: Collections
        MENU_CMD_PUT_TEXT,   // step 5  item 1: Profiles
        MENU_CMD_PUT_TEXT,   // step 6  item 2: Settings
        MENU_CMD_PUT_TEXT,   // step 7  footer
        MENU_CMD_READ_INPUT, // step 8
    };
    for (uint16_t exp : expected) {
        uint16_t got = f.tick_to_cmd();
        CHECK(got == exp);
        f.ack();
    }
}

// ---------------------------------------------------------------------------
// test_menu_main_navigation — DOWN input re-renders with cursor advanced
// ---------------------------------------------------------------------------

static void test_menu_main_navigation() {
    MenuFixture f;
    f.boot_to_main();

    // Drive the 8 rendering commands (SET_MODE through footer PUT_TEXT).
    f.advance(MAIN_RENDER_CMDS);

    // Explicitly receive READ_INPUT.
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);

    // Ack READ_INPUT with DOWN key.
    f.ack_input(0xFFu, 0xFFu ^ KEY_DOWN);

    // Input processing moves cursor; re-render starts with SET_MODE.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
}

// ---------------------------------------------------------------------------
// test_menu_main_select_profiles — RETURN on item 1 switches to PROFILES
// ---------------------------------------------------------------------------

static void test_menu_main_select_profiles() {
    MenuFixture f;
    f.boot_to_main();

    // First MAIN render (8 rendering commands + READ_INPUT).
    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);

    // DOWN: move cursor to 1 (Profiles).
    f.ack_input(0xFFu, 0xFFu ^ KEY_DOWN);

    // Second MAIN render (cursor = 1).
    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);

    // RETURN on item 1 → switch to PROFILES.
    f.ack_input(0xFFu ^ KEY_RETURN, 0xFFu);

    // PROFILES screen starts: first command is CLEAR.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_CLEAR);
}

// ---------------------------------------------------------------------------
// test_menu_profiles_renders — PROFILES lists profile then New/Back/footer
// ---------------------------------------------------------------------------

static void test_menu_profiles_renders() {
    MenuFixture f;

    // Create one profile.
    uint16_t pid = 0u;
    CHECK(f.ps.create("Alice", "en", &pid).ok());
    CHECK(f.ps.set_active(pid).ok());

    f.boot_to_main();

    // Navigate to PROFILES: DOWN (cursor→1=Profiles) then RETURN.
    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);
    f.ack_input(0xFFu, 0xFFu ^ KEY_DOWN); // DOWN

    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);
    f.ack_input(0xFFu ^ KEY_RETURN, 0xFFu); // RETURN → PROFILES

    // PROFILES render (1 profile):
    //   CLEAR, header, profile[0], "New Profile...", "Back", footer, READ_INPUT = 7 cmds
    static const uint16_t expected[] = {
        MENU_CMD_CLEAR,      // step 0
        MENU_CMD_PUT_TEXT,   // step 1  "  Profiles"
        MENU_CMD_PUT_TEXT,   // step 2  "Alice *"
        MENU_CMD_PUT_TEXT,   // base=3: "New Profile..."
        MENU_CMD_PUT_TEXT,   // base+1: "Back"
        MENU_CMD_PUT_TEXT,   // base+2: footer
        MENU_CMD_READ_INPUT, // base+3
    };
    for (uint16_t exp : expected) {
        uint16_t got = f.tick_to_cmd();
        CHECK(got == exp);
        f.ack();
    }
}

// ---------------------------------------------------------------------------
// test_menu_profiles_create — selecting "New Profile..." creates a profile
// ---------------------------------------------------------------------------

static void test_menu_profiles_create() {
    MenuFixture f;
    f.boot_to_main();

    // Navigate to PROFILES (cursor=0=Collections → DOWN → cursor=1=Profiles).
    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);
    f.ack_input(0xFFu, 0xFFu ^ KEY_DOWN); // DOWN → cursor=1 (Profiles)

    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);
    f.ack_input(0xFFu ^ KEY_RETURN, 0xFFu); // RETURN → switch to PROFILES

    // PROFILES render with 0 profiles:
    //   CLEAR, header, "New Profile...", "Back", footer = 5 rendering commands.
    // (base = 2+0 = 2; steps: 0:CLEAR, 1:header, 2:New, 3:Back, 4:footer)
    static constexpr int PROFILES_0_RENDER_CMDS = 5;
    f.advance(PROFILES_0_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);

    // cursor=0 = "New Profile..." → RETURN creates a new profile.
    f.ack_input(0xFFu ^ KEY_RETURN, 0xFFu);

    // tick_to_cmd processes input (creates profile) then starts re-render.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_CLEAR); // re-render PROFILES

    // Profile should now exist.
    CHECK(f.ps.count() == 1u);
    CHECK(f.ps.active() != PROF_ID_NONE);
}

// ---------------------------------------------------------------------------
// test_menu_collections_back — any key on COLLECTIONS returns to MAIN
// ---------------------------------------------------------------------------

static void test_menu_collections_back() {
    MenuFixture f;
    f.boot_to_main();

    // Navigate to COLLECTIONS (cursor=0 = Collections) with RETURN.
    f.advance(MAIN_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);
    f.ack_input(0xFFu ^ KEY_RETURN, 0xFFu); // RETURN → COLLECTIONS

    // COLLECTIONS render: CLEAR, header, title, version, payloads,
    //   Launch (or blank), Back, footer = 8 cmds.
    static constexpr int COLLECTIONS_RENDER_CMDS = 8;
    f.advance(COLLECTIONS_RENDER_CMDS);
    CHECK(f.tick_to_cmd() == MENU_CMD_READ_INPUT);

    // ESC → back to MAIN.
    f.ack_input(0xFFu ^ 0x04u, 0xFFu); // row7 bit2 = ESC

    // MAIN re-renders starting with SET_MODE.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    test_menu_boot_to_boot_info();
    test_menu_boot_info_to_main();
    test_menu_main_renders();
    test_menu_main_navigation();
    test_menu_main_select_profiles();
    test_menu_profiles_renders();
    test_menu_profiles_create();
    test_menu_collections_back();

    return test_summary();
}
