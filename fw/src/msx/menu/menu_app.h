#pragma once
// menu_app.h — RP2350-side menu application state machine.
//
// MenuApp drives the Z80 stub via MenuMailbox to present a navigable
// cartridge UI.  It is non-blocking: each tick() call issues at most one
// command to the mailbox and returns immediately.
//
// Screens: BOOT → BOOT_INFO → MAIN ↔ COLLECTIONS / SETTINGS / LAUNCH → RUNNING

#include "msx/menu/menu_host_abi.h"   // MenuMailbox, HostInfo, InputSnapshot
#include "content/collection_format.h" // CollectionRecord
#include "store/user_data_store.h"
#include <cstdint>

class ApiWindow;

class MenuApp {
public:
    void init(MenuMailbox& mbx, UserDataStore& uds);

    // Request a transition to the MAIN screen (thread-safe-enough for Core 1).
    void request_reset_to_menu();

    // Bind the ApiWindow so MenuApp can call set_active_payload() on LAUNCH.
    void bind_api_window(ApiWindow& win);

    // Set the launch callback invoked just before MENU_CMD_LAUNCH is sent.
    void set_launch_fn(void* ctx, void (*fn)(void*, const char* payload_id));

    bool initialized() const { return initialized_; }

    void tick();

private:
    enum class Screen : uint8_t {
        BOOT = 0,
        BOOT_INFO,
        MAIN,
        COLLECTIONS,
        SETTINGS,
        LAUNCH,
        RUNNING,
    };

    Screen   screen_      = Screen::BOOT;
    int      step_        = 0;
    int      cursor_      = 0;
    bool     initialized_ = false;

    HostInfo         host_info_      = {};
    bool             has_collection_ = false;
    CollectionRecord col_record_     = {};

    bool reset_requested_ = false;
    bool wipe_confirm_    = false;

    char launch_title_[64]      = {};
    char launch_payload_id_[64] = {};

    MenuMailbox*   mbx_     = nullptr;
    UserDataStore* uds_     = nullptr;
    ApiWindow*     api_win_ = nullptr;

    void*  launch_fn_ctx_ = nullptr;
    void (*launch_fn_)(void*, const char*) = nullptr;

    char fmt_[64] = {};

    void tick_boot();
    void tick_boot_info();
    void tick_main();
    void tick_collections();
    void tick_settings();
    void tick_launch();
    void tick_running();

    void load_collection_data();

    void switch_screen(Screen s, int cursor = 0);
    bool put_text(uint8_t col, uint8_t row, const char* text);

    void handle_main_input();
    void handle_collections_input();
    void handle_settings_input();
    void handle_launch_input();
};
