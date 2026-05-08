// menu_app.cc — RP2350-side menu application.
//
// Non-blocking state machine.  Each tick() issues at most one mailbox command.
// Screen handlers advance step_ by 1 per completed command; when READ_INPUT
// completes, the "process" step decodes input and resets step_=0 (re-render)
// or calls switch_screen() to transition.

#include "msx/menu/menu_app.h"
#include "msx/menu/input_decoder.h"
#include "content/content_store.h"
#include "store/user_data_store.h"
#include "msx/api/api_window.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void MenuApp::init(MenuMailbox& mbx, UserDataStore& uds)
{
    mbx_          = &mbx;
    uds_          = &uds;
    api_win_      = nullptr;
    screen_       = Screen::BOOT;
    step_         = 0;
    cursor_       = 0;
    wipe_confirm_ = false;
    initialized_  = true;
}

void MenuApp::request_reset_to_menu()
{
    reset_requested_ = true;
}

void MenuApp::bind_api_window(ApiWindow& win)
{
    api_win_ = &win;
}

void MenuApp::set_launch_fn(void* ctx, void (*fn)(void*, const char*))
{
    launch_fn_ctx_ = ctx;
    launch_fn_     = fn;
}

// ---------------------------------------------------------------------------
// tick — main dispatch
// ---------------------------------------------------------------------------

void MenuApp::tick()
{
    if (!initialized_) return;

    if (reset_requested_) {
        switch_screen(Screen::MAIN);
        reset_requested_ = false;
    }

    if (mbx_->pending()) {
        if (!mbx_->tick()) return;
    }

    switch (screen_) {
        case Screen::BOOT:        tick_boot();        break;
        case Screen::BOOT_INFO:   tick_boot_info();   break;
        case Screen::MAIN:        tick_main();        break;
        case Screen::COLLECTIONS: tick_collections(); break;
        case Screen::SETTINGS:    tick_settings();    break;
        case Screen::LAUNCH:      tick_launch();      break;
        case Screen::RUNNING:     tick_running();     break;
    }
}

// ---------------------------------------------------------------------------
// switch_screen
// ---------------------------------------------------------------------------

void MenuApp::switch_screen(Screen s, int cursor)
{
    screen_ = s;
    step_   = 0;
    cursor_ = cursor;
}

// ---------------------------------------------------------------------------
// put_text helper
// ---------------------------------------------------------------------------

bool MenuApp::put_text(uint8_t col, uint8_t row, const char* text)
{
    uint32_t arg0 = col | (static_cast<uint32_t>(row) << 8u);
    uint16_t len  = static_cast<uint16_t>(strlen(text));
    return mbx_->send_command(MENU_CMD_PUT_TEXT, arg0, 0u, 0u, 0u,
                              reinterpret_cast<const uint8_t*>(text), len);
}

// ---------------------------------------------------------------------------
// Data loaders
// ---------------------------------------------------------------------------

void MenuApp::load_collection_data()
{
    ContentStore cs;
    has_collection_ = cs.has_active_collection();
    if (has_collection_) {
        if (!cs.load_collection(col_record_).ok()) {
            has_collection_ = false;
        }
    }
}

// ---------------------------------------------------------------------------
// BOOT screen
// ---------------------------------------------------------------------------

void MenuApp::tick_boot()
{
    if (mbx_->stub_host_caps() != 0u) {
        switch_screen(Screen::BOOT_INFO);
    }
}

// ---------------------------------------------------------------------------
// BOOT_INFO screen
// ---------------------------------------------------------------------------

void MenuApp::tick_boot_info()
{
    switch (step_) {
    case 0:
        mbx_->send_command(MENU_CMD_GET_HOST_INFO);
        step_ = 1;
        break;
    case 1:
        if (mbx_->last_out_len() >= sizeof(HostInfo)) {
            memcpy(&host_info_, mbx_->data_buf(), sizeof(HostInfo));
        }
        load_collection_data();
        if (has_collection_ && col_record_.boot_mode == 1u) {
            strncpy(launch_title_,      col_record_.title,             sizeof(launch_title_) - 1u);
            strncpy(launch_payload_id_, col_record_.default_payload_id, sizeof(launch_payload_id_) - 1u);
            launch_title_[sizeof(launch_title_) - 1u]           = '\0';
            launch_payload_id_[sizeof(launch_payload_id_) - 1u] = '\0';
            switch_screen(Screen::LAUNCH);
        } else {
            switch_screen(Screen::MAIN);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// MAIN screen
// ---------------------------------------------------------------------------
//
// Steps:
//   0: SET_MODE(TEXT_40)
//   1: CLEAR
//   2: PUT_TEXT row 0  — banner
//   3: PUT_TEXT row 1  — active collection title or "(no collection)"
//   4: PUT_TEXT row 3  — item 0: Collections
//   5: PUT_TEXT row 4  — item 1: Settings
//   6: PUT_TEXT row 22 — footer hint
//   7: READ_INPUT
//   8: process input

static constexpr int MAIN_PROCESS_STEP = 8;
static constexpr int MAIN_ITEMS        = 2;

void MenuApp::tick_main()
{
    switch (step_) {
    case 0:
        mbx_->send_command(MENU_CMD_SET_MODE, MENU_MODE_TEXT_40);
        step_ = 1;
        break;
    case 1:
        mbx_->send_command(MENU_CMD_CLEAR, MENU_CLEAR_ALL);
        step_ = 2;
        break;
    case 2:
        put_text(0u, 0u, "  JLPiCart");
        step_ = 3;
        break;
    case 3:
        if (has_collection_) {
            snprintf(fmt_, sizeof(fmt_), "  %.38s", col_record_.title);
        } else {
            strncpy(fmt_, "  (no collection loaded)", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 1u, fmt_);
        step_ = 4;
        break;
    case 4:
    case 5: {
        static const char* const ITEMS[] = {"Collections", "Settings"};
        int i = step_ - 4;
        snprintf(fmt_, sizeof(fmt_), " %c %-20s", cursor_ == i ? '>' : ' ', ITEMS[i]);
        put_text(0u, static_cast<uint8_t>(3 + i), fmt_);
        step_++;
        break;
    }
    case 6:
        put_text(0u, 22u, "  UP/DN:Move  RETURN:Select");
        step_ = 7;
        break;
    case 7:
        mbx_->send_command(MENU_CMD_READ_INPUT);
        step_ = MAIN_PROCESS_STEP;
        break;
    case MAIN_PROCESS_STEP:
        handle_main_input();
        break;
    }
}

// ---------------------------------------------------------------------------
// COLLECTIONS screen
// ---------------------------------------------------------------------------

void MenuApp::tick_collections()
{
    switch (step_) {
    case 0:
        mbx_->send_command(MENU_CMD_CLEAR, MENU_CLEAR_ALL);
        step_ = 1;
        break;
    case 1:
        put_text(0u, 0u, "  Collections");
        step_ = 2;
        break;
    case 2:
        if (has_collection_) {
            snprintf(fmt_, sizeof(fmt_), "  %.38s", col_record_.title);
        } else {
            strncpy(fmt_, "  No collection installed.", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 2u, fmt_);
        step_ = 3;
        break;
    case 3:
        if (has_collection_) {
            snprintf(fmt_, sizeof(fmt_), "  v%.10s  %.20s",
                     col_record_.version, col_record_.publisher_id);
        } else {
            strncpy(fmt_, "  Insert USB to install.", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 3u, fmt_);
        step_ = 4;
        break;
    case 4:
        if (has_collection_) {
            snprintf(fmt_, sizeof(fmt_), "  Payloads: %d",
                     static_cast<int>(col_record_.payload_count));
            put_text(0u, 5u, fmt_);
        } else {
            put_text(0u, 5u, "");
        }
        step_ = 5;
        break;
    case 5:
        if (has_collection_) {
            snprintf(fmt_, sizeof(fmt_), " %c Launch", cursor_ == 0 ? '>' : ' ');
            put_text(0u, 7u, fmt_);
        } else {
            put_text(0u, 7u, "");
        }
        step_ = 6;
        break;
    case 6: {
        int back_item = has_collection_ ? 1 : 0;
        snprintf(fmt_, sizeof(fmt_), " %c Back", cursor_ == back_item ? '>' : ' ');
        put_text(0u, 8u, fmt_);
        step_ = 7;
        break;
    }
    case 7:
        put_text(0u, 22u, has_collection_ ? "  UP/DN:Move  RETURN:Select  ESC:Back"
                                           : "  RETURN:Back");
        step_ = 8;
        break;
    case 8:
        mbx_->send_command(MENU_CMD_READ_INPUT);
        step_ = 9;
        break;
    case 9:
        handle_collections_input();
        break;
    }
}

// ---------------------------------------------------------------------------
// SETTINGS screen
// ---------------------------------------------------------------------------
//
// Steps: 0:CLEAR 1:header 2:WiFi 3:Lang 4:Video 5:Net
//        6:Wipe item 7:Back item 8:footer 9:READ_INPUT 10:process

static constexpr int SETTINGS_PROCESS_STEP = 10;
static constexpr int SETTINGS_ITEMS        = 2;

void MenuApp::tick_settings()
{
    switch (step_) {
    case 0:
        mbx_->send_command(MENU_CMD_CLEAR, MENU_CLEAR_ALL);
        step_ = 1;
        break;
    case 1:
        put_text(0u, 0u, "  Settings");
        step_ = 2;
        break;
    case 2: {
        if (uds_) {
            const SystemSettings& cfg = uds_->settings();
            if (cfg.wifi_ssid[0]) {
                snprintf(fmt_, sizeof(fmt_), "  WiFi: %.50s", cfg.wifi_ssid);
            } else {
                strncpy(fmt_, "  WiFi: (none)", sizeof(fmt_) - 1u);
                fmt_[sizeof(fmt_) - 1u] = '\0';
            }
        } else {
            strncpy(fmt_, "  WiFi: (none)", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 2u, fmt_);
        step_ = 3;
        break;
    }
    case 3: {
        if (uds_) {
            snprintf(fmt_, sizeof(fmt_), "  Lang: %.7s", uds_->settings().language);
        } else {
            strncpy(fmt_, "  Lang: en", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 3u, fmt_);
        step_ = 4;
        break;
    }
    case 4: {
        static const char* const VMODES[] = {"auto", "crt", "vga"};
        uint8_t vm = 0u;
        if (uds_) {
            vm = uds_->settings().video_mode;
            if (vm >= 3u) vm = 0u;
        }
        snprintf(fmt_, sizeof(fmt_), "  Video: %s", VMODES[vm]);
        put_text(0u, 4u, fmt_);
        step_ = 5;
        break;
    }
    case 5: {
        bool net = uds_ && uds_->settings().network_enabled;
        snprintf(fmt_, sizeof(fmt_), "  Net: %s", net ? "on" : "off");
        put_text(0u, 5u, fmt_);
        step_ = 6;
        break;
    }
    case 6:
        snprintf(fmt_, sizeof(fmt_), " %c Wipe user data",
                 cursor_ == 0 ? '>' : ' ');
        put_text(0u, 7u, fmt_);
        step_ = 7;
        break;
    case 7:
        snprintf(fmt_, sizeof(fmt_), " %c Back",
                 cursor_ == 1 ? '>' : ' ');
        put_text(0u, 8u, fmt_);
        step_ = 8;
        break;
    case 8:
        if (wipe_confirm_) {
            put_text(0u, 22u, "  !! RETURN to CONFIRM wipe, any other key cancels");
        } else {
            put_text(0u, 22u, "  UP/DN:Move  RETURN:Select");
        }
        step_ = 9;
        break;
    case 9:
        mbx_->send_command(MENU_CMD_READ_INPUT);
        step_ = SETTINGS_PROCESS_STEP;
        break;
    case SETTINGS_PROCESS_STEP:
        handle_settings_input();
        break;
    }
}

// ---------------------------------------------------------------------------
// Input processors
// ---------------------------------------------------------------------------

void MenuApp::handle_main_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    if (key_down(inp) || joy1_down(inp)) {
        cursor_ = (cursor_ + 1) % MAIN_ITEMS;
    } else if (key_up(inp) || joy1_up(inp)) {
        cursor_ = (cursor_ + MAIN_ITEMS - 1) % MAIN_ITEMS;
    } else if (key_return(inp) || joy1_trig(inp)) {
        switch (cursor_) {
            case 0: switch_screen(Screen::COLLECTIONS); return;
            case 1: switch_screen(Screen::SETTINGS);    return;
            default: break;
        }
    }
    step_ = 0;
}

void MenuApp::handle_collections_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    const int item_count = has_collection_ ? 2 : 1;
    const int back_item  = item_count - 1;

    if (key_down(inp) || joy1_down(inp)) {
        cursor_ = (cursor_ + 1) % item_count;
        step_ = 5;
        return;
    }
    if (key_up(inp) || joy1_up(inp)) {
        cursor_ = (cursor_ - 1 + item_count) % item_count;
        step_ = 5;
        return;
    }
    if (key_esc(inp)) {
        switch_screen(Screen::MAIN);
        return;
    }
    if (key_return(inp) || joy1_trig(inp)) {
        if (has_collection_ && cursor_ == 0) {
            strncpy(launch_title_,      col_record_.title,             sizeof(launch_title_) - 1u);
            strncpy(launch_payload_id_, col_record_.default_payload_id, sizeof(launch_payload_id_) - 1u);
            launch_title_[sizeof(launch_title_) - 1u]           = '\0';
            launch_payload_id_[sizeof(launch_payload_id_) - 1u] = '\0';
            switch_screen(Screen::LAUNCH);
        } else if (cursor_ == back_item) {
            switch_screen(Screen::MAIN);
        }
        return;
    }

    step_ = 8;
}

void MenuApp::handle_settings_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    if (wipe_confirm_) {
        if ((key_return(inp) || joy1_trig(inp)) && uds_) {
            uds_->wipe_user_data();
        }
        wipe_confirm_ = false;
        step_ = 0;
        return;
    }

    if (key_down(inp) || joy1_down(inp)) {
        cursor_ = (cursor_ + 1) % SETTINGS_ITEMS;
    } else if (key_up(inp) || joy1_up(inp)) {
        cursor_ = (cursor_ + SETTINGS_ITEMS - 1) % SETTINGS_ITEMS;
    } else if (key_return(inp) || joy1_trig(inp)) {
        if (cursor_ == 0) {
            wipe_confirm_ = true;
        } else {
            switch_screen(Screen::MAIN);
            return;
        }
    } else if (key_esc(inp)) {
        switch_screen(Screen::MAIN);
        return;
    }

    step_ = 0;
}

// ---------------------------------------------------------------------------
// LAUNCH screen
// ---------------------------------------------------------------------------

void MenuApp::tick_launch()
{
    switch (step_) {
    case 0:
        if (!mbx_->send_command(MENU_CMD_CLEAR)) return;
        if (api_win_) api_win_->set_active_payload(launch_payload_id_);
        step_ = 1;
        break;
    case 1:
        snprintf(fmt_, sizeof(fmt_), "  Launching %.38s...", launch_title_);
        if (!put_text(0u, 0u, fmt_)) return;
        step_ = 2;
        break;
    case 2:
        if (!put_text(0u, 2u, "  (press any key to cancel)")) return;
        step_ = 3;
        break;
    case 3:
        if (!mbx_->send_command(MENU_CMD_READ_INPUT)) return;
        step_ = 4;
        break;
    case 4:
        handle_launch_input();
        break;
    case 5:
        if (!mbx_->send_command(MENU_CMD_LAUNCH)) return;
        step_ = 6;
        break;
    case 6:
        if (mbx_->pending()) return;
        switch_screen(Screen::RUNNING);
        break;
    }
}

void MenuApp::handle_launch_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    if (key_any(inp)) {
        if (api_win_) api_win_->set_active_payload("");
        switch_screen(Screen::MAIN);
        return;
    }

    if (launch_fn_) launch_fn_(launch_fn_ctx_, launch_payload_id_);
    step_ = 5;
}

// ---------------------------------------------------------------------------
// RUNNING
// ---------------------------------------------------------------------------

void MenuApp::tick_running()
{
    // Z80 is running the game — nothing to do.
}
