#pragma once
// menu_app.h — RP2350-side menu application state machine.
//
// MenuApp drives the Z80 stub via MenuMailbox to present a navigable
// cartridge UI.  It is non-blocking: each tick() call issues at most one
// command to the mailbox and returns immediately.
//
// Screens: BOOT → BOOT_INFO → MAIN ↔ COLLECTIONS / PROFILES / SETTINGS / LAUNCH → RUNNING

#include "msx/menu/menu_host_abi.h"   // MenuMailbox, HostInfo, InputSnapshot
#include "content/collection_format.h" // CollectionRecord
#include "store/user_data_store.h"     // ProfileRecord, PROF_MAX_PROFILES, PROF_ID_NONE
#include <cstdint>

// Forward declarations — full types are only needed in the .cc file.
class ApiWindow;

class MenuApp {
public:
    // Attach subsystem references and enter BOOT state.
    void init(MenuMailbox& mbx, UserDataStore& uds);

    // Request a transition to the MAIN screen (Stage 18).
    // Thread-safe-enough for Core 1 service loop: sets a flag read by tick().
    void request_reset_to_menu();

    // Bind the ApiWindow pointer so MenuApp can call set_active_payload()
    // when transitioning to/from the LAUNCH screen (Stage 20).
    // Must be called after init().  Optional: if not set, active_payload is not updated.
    void bind_api_window(ApiWindow& win);

    // Set the launch callback invoked just before MENU_CMD_LAUNCH is sent.
    // The callback must remap the ROM for payload_id by calling apply_mapping().
    // ctx is an opaque pointer passed through to fn.  Both are stored by reference;
    // they must remain valid for the lifetime of this MenuApp.
    // If not set, LAUNCH still sends MENU_CMD_LAUNCH (BIOS cold start) but with
    // whatever ROM was already mapped.
    void set_launch_fn(void* ctx, void (*fn)(void*, const char* payload_id));

    bool initialized() const { return initialized_; }

    // Advance the state machine by one step.  Call repeatedly from the
    // service loop.  Returns immediately if waiting for the Z80 stub.
    void tick();

private:
    enum class Screen : uint8_t {
        BOOT = 0,
        BOOT_INFO,
        MAIN,
        COLLECTIONS,
        PROFILES,
        SETTINGS,
        LAUNCH,
        RUNNING,
    };

    Screen   screen_      = Screen::BOOT;
    int      step_        = 0;
    int      cursor_      = 0;
    bool     initialized_ = false;

    HostInfo         host_info_         = {};
    bool             has_collection_    = false;
    CollectionRecord col_record_        = {};
    ProfileRecord    profiles_[PROF_MAX_PROFILES] = {};
    uint8_t          profile_count_     = 0u;
    uint16_t         active_profile_id_ = PROF_ID_NONE;

    bool          reset_requested_ = false;
    bool          wipe_confirm_    = false;

    char          launch_title_[64]      = {};
    char          launch_payload_id_[64] = {};

    MenuMailbox*   mbx_       = nullptr;
    UserDataStore* uds_       = nullptr;
    ApiWindow*     api_win_   = nullptr;

    void*  launch_fn_ctx_ = nullptr;
    void (*launch_fn_)(void*, const char*) = nullptr;

    char fmt_[64] = {};

    void tick_boot();
    void tick_boot_info();
    void tick_main();
    void tick_collections();
    void tick_profiles();
    void tick_settings();
    void tick_launch();
    void tick_running();

    void load_collection_data();
    void load_profile_data();

    void switch_screen(Screen s, int cursor = 0);

    bool put_text(uint8_t col, uint8_t row, const char* text);

    void handle_main_input();
    void handle_collections_input();
    void handle_profiles_input();
    void handle_settings_input();
    void handle_launch_input();

    int profiles_item_count() const;
};
