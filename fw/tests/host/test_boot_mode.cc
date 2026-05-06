// test_boot_mode.cc — host tests for Stage 20 boot mode enforcement.
//
// Verifies that:
//   - A collection with boot_mode=1 (direct) causes MenuApp to enter the LAUNCH
//     screen after BOOT_INFO, issuing CLEAR as the first command (not SET_MODE).
//   - A collection with boot_mode=0 (menu_first) boots to MAIN (SET_MODE first).
//   - No collection installed boots to MAIN.
//   - The LAUNCH screen loops on READ_INPUT and cancels back to MAIN on any key.
//   - ApiWindow::active_payload_id() reflects the launch state.

#include "msx/menu/menu_app.h"
#include "msx/menu/menu_host_abi.h"
#include "msx/api/api_window.h"
#include "msx/api/api_types.h"
#include "content/collection_format.h"
#include "store/user_data_store.h"
#include "spine/security_posture.h"
#include "spine/policy_store.h"
#include "spine/capability_registry.h"
#include "platform/platform.h"
#include "spine/driver_descriptor.h"
#include "filesystem/fat_util.h"

#include "fat_test_env.h"
#include "test_helpers.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Helper: write a CollectionRecord to FAT (simulates install).
// ---------------------------------------------------------------------------

static void write_collection(const char* title,
                              uint8_t boot_mode,
                              const char* default_payload_id = "payload-001")
{
    CollectionRecord rec = {};
    strncpy(rec.collection_id,      "test-col-001",      sizeof(rec.collection_id) - 1u);
    strncpy(rec.version,            "1.0",               sizeof(rec.version) - 1u);
    strncpy(rec.publisher_id,       "test-pub",          sizeof(rec.publisher_id) - 1u);
    strncpy(rec.title,              title,               sizeof(rec.title) - 1u);
    strncpy(rec.default_payload_id, default_payload_id,  sizeof(rec.default_payload_id) - 1u);
    rec.boot_mode     = boot_mode;
    rec.payload_count = 1u;

    fat_ensure_dir("1:/collections");
    fat_ensure_dir("1:/collections/test-col-001");
    fat_write_file("1:/collections/test-col-001/collection.bin",
                   &rec, sizeof(rec));
    fat_write_file("1:/collections/active.txt",
                   "test-col-001", strlen("test-col-001"));
}

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

struct BootFixture {
    uint8_t       page[MENU_PAGE_SIZE];
    FatTestEnv    env;
    UserDataStore uds;
    MenuMailbox   mbx;
    MenuApp       app;

    SecurityPosture    posture;
    PolicyStore        policy_store;
    CapabilityRegistry registry;
    ApiWindow          win;

    BootFixture() {
        memset(page, 0, sizeof(page));
        mbx.init(page, 0u);
        uds.init();
        app.init(mbx, uds);

        posture = {};
        policy_store.load(posture);
        registry.init(BoardDescriptor::for_current_board(),
                      kDriverDescriptors, kDriverDescriptorCount,
                      policy_store.info());
        win.init(posture, policy_store, registry);
        win.bind_user_data(uds);
    }

    void sim_stub_init() {
        MenuStubHeader* hdr = reinterpret_cast<MenuStubHeader*>(page + MENU_HEADER_OFS);
        hdr->host_caps = MENU_HOST_CAP_MSX1 | MENU_HOST_CAP_BIOS_KBD;
    }

    uint16_t last_cmd() {
        return mbx.raw_regs()->cmd_id;
    }

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

    void ack_host_info() {
        HostInfo hi = {};
        hi.msx_gen   = 1u;
        hi.vram_kb   = 16u;
        hi.text_cols = 40u;
        hi.host_caps = MENU_HOST_CAP_MSX1 | MENU_HOST_CAP_BIOS_KBD;
        ack(reinterpret_cast<const uint8_t*>(&hi), static_cast<uint16_t>(sizeof(hi)));
    }

    // Tick until a new pending command appears; returns cmd_id.
    uint16_t tick_to_cmd(int max_ticks = 40) {
        for (int i = 0; i < max_ticks; ++i) {
            app.tick();
            if (mbx.pending()) return last_cmd();
        }
        return 0xFFFFu;
    }

    // Ack READ_INPUT with all keys released (no input).
    void ack_no_input() {
        InputSnapshot inp = {};
        memset(inp.kbd_rows, 0xFFu, sizeof(inp.kbd_rows));
        inp.joy1 = 0xFFu;
        inp.joy2 = 0xFFu;
        ack(reinterpret_cast<const uint8_t*>(&inp), sizeof(inp));
    }

    // Ack READ_INPUT with a key pressed (RETURN = row7 bit7).
    void ack_key_return() {
        InputSnapshot inp = {};
        memset(inp.kbd_rows, 0xFFu, sizeof(inp.kbd_rows));
        inp.kbd_rows[7] = static_cast<uint8_t>(0xFFu & ~0x80u); // RETURN pressed
        inp.joy1 = 0xFFu;
        inp.joy2 = 0xFFu;
        ack(reinterpret_cast<const uint8_t*>(&inp), sizeof(inp));
    }

    // Drive the BOOT phase up to and including the GET_HOST_INFO ack.
    // Precondition: sim_stub_init() already called (or will be by this helper).
    void boot_to_host_info_ack() {
        sim_stub_init();
        CHECK(tick_to_cmd() == MENU_CMD_GET_HOST_INFO);
        ack_host_info();
    }
};

// ---------------------------------------------------------------------------
// test_direct_boot — boot_mode=1 → first post-HostInfo command is CLEAR
// ---------------------------------------------------------------------------

static void test_direct_boot()
{
    BootFixture f;
    write_collection("My Game", 1u /* direct */);

    f.boot_to_host_info_ack();

    // Next command should be MENU_CMD_CLEAR (LAUNCH screen step 0), not SET_MODE.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_CLEAR);
}

// ---------------------------------------------------------------------------
// test_menu_first_boot — boot_mode=0 → first post-HostInfo command is SET_MODE
// ---------------------------------------------------------------------------

static void test_menu_first_boot()
{
    BootFixture f;
    write_collection("My Game", 0u /* menu_first */);

    f.boot_to_host_info_ack();

    // Next command should be MENU_CMD_SET_MODE (MAIN screen step 0).
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
}

// ---------------------------------------------------------------------------
// test_no_collection_boot — no collection → boots to MAIN regardless
// ---------------------------------------------------------------------------

static void test_no_collection_boot()
{
    BootFixture f;
    // No collection written to FAT.

    f.boot_to_host_info_ack();

    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
}

// ---------------------------------------------------------------------------
// test_launch_cancel — any key in LAUNCH returns to MAIN
// ---------------------------------------------------------------------------

static void test_launch_cancel()
{
    BootFixture f;
    write_collection("My Game", 1u);

    f.boot_to_host_info_ack();

    // Drive through LAUNCH steps 0-3 (CLEAR, two PUT_TEXT, READ_INPUT).
    for (int step = 0; step < 4; ++step) {
        CHECK(f.tick_to_cmd() != 0xFFFFu);
        f.ack();
    }

    // Now in step 4 (process): ack READ_INPUT with a key press.
    f.ack_key_return();

    // After processing key press, MenuApp should transition to MAIN → SET_MODE.
    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_SET_MODE);
}

// ---------------------------------------------------------------------------
// test_launch_no_input_launches — no key in LAUNCH sends MENU_CMD_LAUNCH
// ---------------------------------------------------------------------------

static void test_launch_no_input_launches()
{
    BootFixture f;
    write_collection("My Game", 1u);

    f.boot_to_host_info_ack();

    // Drive CLEAR + 2× PUT_TEXT + READ_INPUT.
    for (int step = 0; step < 4; ++step) {
        CHECK(f.tick_to_cmd() != 0xFFFFu);
        f.ack();
    }

    // Ack READ_INPUT with no key — should proceed to MENU_CMD_LAUNCH.
    f.ack_no_input();

    uint16_t cmd = f.tick_to_cmd();
    CHECK(cmd == MENU_CMD_LAUNCH);
}

// ---------------------------------------------------------------------------
// test_active_payload_id — ApiWindow reflects LAUNCH payload, cleared on cancel
// ---------------------------------------------------------------------------

static void test_active_payload_id()
{
    BootFixture f;
    write_collection("My Game", 1u, "my-payload-id");
    f.app.bind_api_window(f.win);

    f.boot_to_host_info_ack();

    // Initially no active payload.
    CHECK(f.win.active_payload_id()[0] == '\0');

    // Step 0: CLEAR — sets active_payload_id in ApiWindow.
    CHECK(f.tick_to_cmd() == MENU_CMD_CLEAR);
    // active_payload_id is set synchronously during step 0.
    CHECK(strcmp(f.win.active_payload_id(), "my-payload-id") == 0);
    f.ack();

    // Step 1-3: render + READ_INPUT.
    for (int step = 1; step < 4; ++step) {
        CHECK(f.tick_to_cmd() != 0xFFFFu);
        f.ack();
    }

    // Cancel: any key → back to MAIN, clears active_payload_id.
    f.ack_key_return();
    CHECK(f.tick_to_cmd() == MENU_CMD_SET_MODE);
    CHECK(f.win.active_payload_id()[0] == '\0');
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main()
{
    test_direct_boot();
    test_menu_first_boot();
    test_no_collection_boot();
    test_launch_cancel();
    test_launch_no_input_launches();
    test_active_payload_id();

    return test_summary();
}
