// menu_app.cc — RP2350-side menu application (Stages 16–20).
//
// Non-blocking state machine.  Each tick() issues at most one mailbox command.
// Screen handlers advance step_ by 1 per completed command; when READ_INPUT
// completes, the "process" step decodes input and resets step_=0 (re-render)
// or calls switch_screen() to transition.
//
// See bootstrapping.md Stages 16–20 for step-by-step design notes.

#include "menu/menu_app.h"
#include "menu/input_decoder.h"
#include "content/content_store.h"
#include "profiles/profile_store.h"
#include "settings/system_settings_store.h"
#include "msx/api/api_window.h"
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void MenuApp::init(MenuMailbox& mbx, KvStore& kv, ProfileStore& ps)
{
    mbx_          = &mbx;
    kv_           = &kv;
    ps_           = &ps;
    ss_           = nullptr;
    saves_kv_     = nullptr;
    api_win_      = nullptr;
    screen_       = Screen::BOOT;
    step_         = 0;
    cursor_       = 0;
    wipe_confirm_ = false;
    initialized_  = true;
}

void MenuApp::bind_settings_store(SystemSettingsStore& ss, KvStore& saves_kv)
{
    ss_       = &ss;
    saves_kv_ = &saves_kv;
}

void MenuApp::request_reset_to_menu()
{
    reset_requested_ = true;
}

void MenuApp::bind_api_window(ApiWindow& win)
{
    api_win_ = &win;
}

// ---------------------------------------------------------------------------
// tick — main dispatch
// ---------------------------------------------------------------------------

void MenuApp::tick()
{
    if (!initialized_) return;

    // Stage 18: handle RESET_TO_MENU requests from the API service layer.
    if (reset_requested_) {
        switch_screen(Screen::MAIN);
        reset_requested_ = false;
    }

    // If a command is pending, check whether the Z80 stub has responded.
    if (mbx_->pending()) {
        if (!mbx_->tick()) return; // still waiting
    }

    // No pending command: advance the current screen's state machine.
    switch (screen_) {
        case Screen::BOOT:        tick_boot();        break;
        case Screen::BOOT_INFO:   tick_boot_info();   break;
        case Screen::MAIN:        tick_main();        break;
        case Screen::COLLECTIONS: tick_collections(); break;
        case Screen::PROFILES:    tick_profiles();    break;
        case Screen::SETTINGS:    tick_settings();    break;
        case Screen::LAUNCH:      tick_launch();      break;
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
    ContentStore cs(*kv_);
    has_collection_ = cs.has_active_collection();
    if (has_collection_) {
        if (!cs.load_collection(col_record_).ok()) {
            has_collection_ = false;
        }
    }
}

void MenuApp::load_profile_data()
{
    profile_count_     = ps_->list(profiles_, PROF_MAX_PROFILES);
    active_profile_id_ = ps_->active();
}

// ---------------------------------------------------------------------------
// BOOT screen — poll until Z80 stub has completed its own init
// ---------------------------------------------------------------------------

void MenuApp::tick_boot()
{
    // No commands sent.  stub_host_caps() returns non-zero once the Z80
    // stub has written its capabilities to the header (spec §8 ordering rule).
    if (mbx_->stub_host_caps() != 0u) {
        switch_screen(Screen::BOOT_INFO);
    }
}

// ---------------------------------------------------------------------------
// BOOT_INFO screen — query host hardware capabilities
// ---------------------------------------------------------------------------
//
// Steps:
//   0: send GET_HOST_INFO
//   1: parse response → load data → switch to MAIN

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
        load_profile_data();
        // Stage 20: if the active collection requests direct boot, skip MAIN.
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
//   5: PUT_TEXT row 4  — item 1: Profiles
//   6: PUT_TEXT row 5  — item 2: Settings
//   7: PUT_TEXT row 22 — footer hint
//   8: READ_INPUT
//   9: process input

static constexpr int MAIN_PROCESS_STEP = 9;

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
    case 5:
    case 6: {
        static const char* const ITEMS[] = {"Collections", "Profiles", "Settings"};
        int i = step_ - 4;
        snprintf(fmt_, sizeof(fmt_), " %c %-20s", cursor_ == i ? '>' : ' ', ITEMS[i]);
        put_text(0u, static_cast<uint8_t>(3 + i), fmt_);
        step_++;
        break;
    }
    case 7:
        put_text(0u, 22u, "  UP/DN:Move  RETURN:Select");
        step_ = 8;
        break;
    case 8:
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
//
// Steps:
//   0: CLEAR
//   1: PUT_TEXT row 0 — header
//   2: PUT_TEXT row 2 — title or "no collection"
//   3: PUT_TEXT row 3 — version + publisher, or install hint
//   4: PUT_TEXT row 5 — payload count or blank
//   5: PUT_TEXT row 7 — "Back"
//   6: PUT_TEXT row 22 — footer
//   7: READ_INPUT
//   8: process input (any key → MAIN)

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
        put_text(0u, 7u, " > Back");
        step_ = 6;
        break;
    case 6:
        put_text(0u, 22u, "  RETURN:Back");
        step_ = 7;
        break;
    case 7:
        mbx_->send_command(MENU_CMD_READ_INPUT);
        step_ = 8;
        break;
    case 8:
        handle_collections_input();
        break;
    }
}

// ---------------------------------------------------------------------------
// PROFILES screen
// ---------------------------------------------------------------------------
//
// Variable step count because the number of profiles is runtime data.
//
// Steps:
//   0:         CLEAR (also refreshes profile data)
//   1:         PUT_TEXT header
//   2+i:       PUT_TEXT profile[i] for i in 0..profile_count_-1
//   base+0:    PUT_TEXT "New Profile..."    (base = 2 + profile_count_)
//   base+1:    PUT_TEXT "Back"
//   base+2:    PUT_TEXT footer
//   base+3:    READ_INPUT
//   base+4:    process input

int MenuApp::profiles_item_count() const
{
    return static_cast<int>(profile_count_) + 2; // profiles + "New Profile..." + "Back"
}

void MenuApp::tick_profiles()
{
    const int base         = 2 + static_cast<int>(profile_count_);
    const int process_step = base + 4;

    if (step_ == process_step) {
        handle_profiles_input();
        return;
    }
    if (step_ == base + 3) {
        mbx_->send_command(MENU_CMD_READ_INPUT);
        step_++;
        return;
    }
    if (step_ == base + 2) {
        put_text(0u, 22u, "  UP/DN:Move  RETURN:Select  ESC:Back");
        step_++;
        return;
    }
    if (step_ == base + 1) {
        // "Back" item
        int back_item = profiles_item_count() - 1;
        snprintf(fmt_, sizeof(fmt_), " %c Back",
                 cursor_ == back_item ? '>' : ' ');
        put_text(0u, static_cast<uint8_t>(3 + back_item), fmt_);
        step_++;
        return;
    }
    if (step_ == base) {
        // "New Profile..." item
        int new_item = static_cast<int>(profile_count_);
        snprintf(fmt_, sizeof(fmt_), " %c New Profile...",
                 cursor_ == new_item ? '>' : ' ');
        put_text(0u, static_cast<uint8_t>(3 + new_item), fmt_);
        step_++;
        return;
    }
    if (step_ >= 2 && step_ < base) {
        // Profile item
        int i = step_ - 2;
        bool is_active = (profiles_[i].profile_id == active_profile_id_);
        snprintf(fmt_, sizeof(fmt_), " %c %-20s%s",
                 cursor_ == i ? '>' : ' ',
                 profiles_[i].name,
                 is_active ? "*" : " ");
        put_text(0u, static_cast<uint8_t>(3 + i), fmt_);
        step_++;
        return;
    }

    switch (step_) {
    case 0:
        load_profile_data(); // refresh on every full render
        mbx_->send_command(MENU_CMD_CLEAR, MENU_CLEAR_ALL);
        step_ = 1;
        break;
    case 1:
        put_text(0u, 0u, "  Profiles");
        step_ = 2;
        break;
    }
}

// ---------------------------------------------------------------------------
// SETTINGS screen
// ---------------------------------------------------------------------------
//
// When SystemSettingsStore is bound (Stage 17+):
//   Steps: 0:CLEAR 1:header 2:WiFi 3:Lang 4:Video 5:Net
//          6:Wipe item 7:Back item 8:footer 9:READ_INPUT 10:process
//
// When SystemSettingsStore is not bound (tests / pre-Stage 17):
//   Steps: 0:CLEAR 1:header 2:placeholder 3:Back 4:footer 5:READ_INPUT 6:process
//
// Items (cursor-based, live path only):
//   0 = "Wipe user data"  (RETURN requires confirmation)
//   1 = "Back"

static constexpr int SETTINGS_PROCESS_STEP      = 10;
static constexpr int SETTINGS_PROCESS_STEP_MIN  = 6;  // fallback path
static constexpr int SETTINGS_ITEMS             = 2;

void MenuApp::tick_settings()
{
    if (!ss_) {
        // Fallback: placeholder screen (no settings store bound).
        switch (step_) {
        case 0:
            mbx_->send_command(MENU_CMD_CLEAR, MENU_CLEAR_ALL);
            step_ = 1;
            break;
        case 1:
            put_text(0u, 0u, "  Settings");
            step_ = 2;
            break;
        case 2:
            put_text(0u, 2u, "  (no settings configured)");
            step_ = 3;
            break;
        case 3:
            put_text(0u, 4u, " > Back");
            step_ = 4;
            break;
        case 4:
            put_text(0u, 22u, "  RETURN:Back");
            step_ = 5;
            break;
        case 5:
            mbx_->send_command(MENU_CMD_READ_INPUT);
            step_ = SETTINGS_PROCESS_STEP_MIN;
            break;
        case SETTINGS_PROCESS_STEP_MIN:
            handle_settings_input();
            break;
        }
        return;
    }

    // Live settings screen.
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
        const SystemSettings& cfg = ss_->get();
        if (cfg.wifi_ssid[0]) {
            snprintf(fmt_, sizeof(fmt_), "  WiFi: %.50s", cfg.wifi_ssid);
        } else {
            strncpy(fmt_, "  WiFi: (none)", sizeof(fmt_) - 1u);
            fmt_[sizeof(fmt_) - 1u] = '\0';
        }
        put_text(0u, 2u, fmt_);
        step_ = 3;
        break;
    }
    case 3: {
        const SystemSettings& cfg = ss_->get();
        snprintf(fmt_, sizeof(fmt_), "  Lang: %.7s", cfg.language);
        put_text(0u, 3u, fmt_);
        step_ = 4;
        break;
    }
    case 4: {
        static const char* const VMODES[] = {"auto", "crt", "vga"};
        const SystemSettings& cfg = ss_->get();
        uint8_t vm = (cfg.video_mode < 3u) ? cfg.video_mode : 0u;
        snprintf(fmt_, sizeof(fmt_), "  Video: %s", VMODES[vm]);
        put_text(0u, 4u, fmt_);
        step_ = 5;
        break;
    }
    case 5: {
        const SystemSettings& cfg = ss_->get();
        snprintf(fmt_, sizeof(fmt_), "  Net: %s",
                 cfg.network_enabled ? "on" : "off");
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
        cursor_ = (cursor_ + 1) % 3;
    } else if (key_up(inp) || joy1_up(inp)) {
        cursor_ = (cursor_ + 2) % 3; // (cursor_ - 1 + 3) % 3
    } else if (key_return(inp) || joy1_trig(inp)) {
        switch (cursor_) {
            case 0: switch_screen(Screen::COLLECTIONS); return;
            case 1: switch_screen(Screen::PROFILES);   return;
            case 2: switch_screen(Screen::SETTINGS);   return;
            default: break;
        }
    }
    // No transition: re-render from the top.
    step_ = 0;
}

void MenuApp::handle_collections_input()
{
    // Any key returns to MAIN.
    switch_screen(Screen::MAIN);
}

void MenuApp::handle_profiles_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    const int item_count = profiles_item_count();

    if (key_down(inp) || joy1_down(inp)) {
        cursor_ = (cursor_ + 1) % item_count;
    } else if (key_up(inp) || joy1_up(inp)) {
        cursor_ = (cursor_ - 1 + item_count) % item_count;
    } else if (key_return(inp) || joy1_trig(inp)) {
        if (cursor_ < static_cast<int>(profile_count_)) {
            // Set selected profile as active.
            ps_->set_active(profiles_[cursor_].profile_id);
            active_profile_id_ = profiles_[cursor_].profile_id;
        } else if (cursor_ == static_cast<int>(profile_count_)) {
            // Create a new profile with an auto-generated name.
            if (profile_count_ < PROF_MAX_PROFILES) {
                char name[PROF_NAME_MAX];
                snprintf(name, sizeof(name), "Profile %d",
                         static_cast<int>(profile_count_) + 1);
                uint16_t new_id = 0u;
                if (ps_->create(name, "en", &new_id).ok()) {
                    ps_->set_active(new_id);
                }
                load_profile_data();
                cursor_ = 0;
            }
        } else {
            // "Back"
            switch_screen(Screen::MAIN);
            return;
        }
    } else if (key_esc(inp)) {
        switch_screen(Screen::MAIN);
        return;
    }
    // Re-render with updated cursor / data.
    step_ = 0;
}

void MenuApp::handle_settings_input()
{
    if (!ss_) {
        // Fallback path: any key returns to MAIN.
        switch_screen(Screen::MAIN);
        return;
    }

    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    if (wipe_confirm_) {
        if (key_return(inp) || joy1_trig(inp)) {
            // Confirmed: execute wipe.
            if (saves_kv_) {
                ss_->wipe_user_data(*saves_kv_, *ps_);
            }
        }
        // Any other key (or after wipe): cancel/clear confirmation and re-render.
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
            // First press: arm confirmation.
            wipe_confirm_ = true;
        } else {
            // "Back"
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
// LAUNCH screen (Stage 20)
// ---------------------------------------------------------------------------
//
// Steps:
//   0: CLEAR; register active payload with ApiWindow
//   1: PUT_TEXT 0,0 "  Launching <title>..."
//   2: PUT_TEXT 0,2 "  (press any key to cancel)"
//   3: READ_INPUT
//   4: process — any key cancels → MAIN; otherwise loop to step 3

void MenuApp::tick_launch()
{
    switch (step_) {
    case 0:
        if (!mbx_->send_command(MENU_CMD_CLEAR)) return;
        if (api_win_) {
            api_win_->set_active_payload(launch_payload_id_);
        }
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
    }
}

void MenuApp::handle_launch_input()
{
    InputSnapshot inp = {};
    if (mbx_->last_out_len() >= sizeof(InputSnapshot)) {
        memcpy(&inp, mbx_->data_buf(), sizeof(InputSnapshot));
    }

    // Any key press cancels and returns to MAIN.
    if (key_any(inp)) {
        if (api_win_) {
            api_win_->set_active_payload("");
        }
        switch_screen(Screen::MAIN);
        return;
    }

    // No key: loop back to READ_INPUT.
    step_ = 3;
}
