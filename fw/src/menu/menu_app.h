#pragma once
// menu_app.h — RP2350-side menu application state machine.
//
// MenuApp drives the Z80 stub via MenuMailbox to present a navigable
// cartridge UI.  It is non-blocking: each tick() call issues at most one
// command to the mailbox and returns immediately.
//
// Screens: BOOT → BOOT_INFO → MAIN ↔ COLLECTIONS / PROFILES / SETTINGS
//
// See bootstrapping.md Stage 16 for the full design spec.

#include "msx/menu/menu_host_abi.h"   // MenuMailbox, HostInfo, InputSnapshot
#include "content/collection_format.h" // CollectionRecord
#include "profiles/profile_format.h"  // ProfileRecord, PROF_MAX_PROFILES, PROF_ID_NONE
#include <cstdint>

// Forward declarations — full types are only needed in the .cc file.
class KvStore;
class ProfileStore;
class SystemSettingsStore;

class MenuApp {
public:
    // Attach the subsystem references and enter BOOT state.
    // kv is used (read-only at runtime) to query the active collection.
    void init(MenuMailbox& mbx, KvStore& kv, ProfileStore& ps);

    // Bind the system settings store (Stage 17+).
    // Must be called after init().  If not called, the Settings screen shows
    // a placeholder and the wipe function is unavailable.
    // saves_kv is the KvStore for the SAVES_KV partition (used by wipe operations).
    void bind_settings_store(SystemSettingsStore& ss, KvStore& saves_kv);

    // Request a transition to the MAIN screen (Stage 18).
    // Thread-safe-enough for Core 1 service loop: sets a flag read by tick().
    void request_reset_to_menu();

    bool initialized() const { return initialized_; }

    // Advance the state machine by one step.  Call repeatedly from the
    // service loop.  Returns immediately if waiting for the Z80 stub.
    void tick();

private:
    enum class Screen : uint8_t {
        BOOT = 0,   // waiting for Z80 stub to initialise
        BOOT_INFO,  // sending GET_HOST_INFO
        MAIN,       // top-level navigation
        COLLECTIONS,
        PROFILES,
        SETTINGS,
    };

    // --- Core state ---
    Screen   screen_      = Screen::BOOT;
    int      step_        = 0;    // position within current screen's render sequence
    int      cursor_      = 0;    // highlighted menu item index
    bool     initialized_ = false;

    // --- Cached data refreshed at screen transitions ---
    HostInfo         host_info_         = {};
    bool             has_collection_    = false;
    CollectionRecord col_record_        = {};
    ProfileRecord    profiles_[PROF_MAX_PROFILES] = {};
    uint8_t          profile_count_     = 0u;
    uint16_t         active_profile_id_ = PROF_ID_NONE;

    // --- Reset-to-menu flag (Stage 18) ---
    bool          reset_requested_ = false;

    // --- Wipe confirmation state (SETTINGS screen) ---
    bool          wipe_confirm_    = false;

    // --- Subsystem references ---
    MenuMailbox*          mbx_       = nullptr;
    KvStore*              kv_        = nullptr;
    ProfileStore*         ps_        = nullptr;
    SystemSettingsStore*  ss_        = nullptr;  // nullptr until bind_settings_store()
    KvStore*              saves_kv_  = nullptr;  // nullptr until bind_settings_store()

    // Scratch buffer for formatted strings
    char fmt_[64] = {};

    // --- Per-screen tick handlers ---
    void tick_boot();
    void tick_boot_info();
    void tick_main();
    void tick_collections();
    void tick_profiles();
    void tick_settings();

    // --- Data loaders ---
    void load_collection_data();
    void load_profile_data();

    // --- State transition ---
    void switch_screen(Screen s, int cursor = 0);

    // --- Mailbox helpers ---
    // PUT_TEXT with arg0 = col | (row << 8), in_data = text.
    bool put_text(uint8_t col, uint8_t row, const char* text);

    // --- Input processors (called at the "process" step of each screen) ---
    void handle_main_input();
    void handle_collections_input();
    void handle_profiles_input();
    void handle_settings_input();

    // Number of selectable items in the PROFILES screen.
    // = profile_count_ + 2 (each profile + "New Profile..." + "Back").
    int profiles_item_count() const;
};
