#pragma once
// system_settings.h — SystemSettings packed struct and KV key constants (Stage 17).
//
// The entire settings struct is stored as a single blob under KV_SYS_SETTINGS in
// the SYSTEM_KV partition.  All callers should access settings via SystemSettingsStore;
// this header is shared between the store, host tests, and the Menu Settings screen.
//
// Spec references: §4.5 "System Configuration", §6.2 "System Settings schema (jlpicart.system.v1)".

#include <cstdint>

// KV key for the settings blob in SYSTEM_KV.
static constexpr const char* KV_SYS_SETTINGS = "sys.settings";

// video_mode values (SYS_VIDEO_AUTO = let the hardware decide at boot)
static constexpr uint8_t SYS_VIDEO_AUTO    = 0u;
static constexpr uint8_t SYS_VIDEO_CRT     = 1u;
static constexpr uint8_t SYS_VIDEO_VGA     = 2u;

// source_priority values (ordered preference for collection sources)
static constexpr uint8_t SYS_SRC_FLASH     = 0u;
static constexpr uint8_t SYS_SRC_USB       = 1u;
static constexpr uint8_t SYS_SRC_OPTICAL   = 2u;
static constexpr uint8_t SYS_SRC_NETWORK   = 3u;

#pragma pack(push, 1)
// Stored as a single blob in SYSTEM_KV["sys.settings"].
// All reserved bytes MUST be zero.  Struct size is fixed at 192 bytes.
struct SystemSettings {
    char    wifi_ssid[64];       // NUL-terminated SSID; empty = no credentials
    char    wifi_pass[64];       // NUL-terminated passphrase
    char    language[8];         // BCP-47 language tag (e.g. "en", "es"); default "en"
    uint8_t video_mode;          // SYS_VIDEO_* (0=auto by default)
    uint8_t network_enabled;     // 1=on (default), 0=globally disabled
    uint8_t guest_allowed;       // 1=on (default), 0=disabled
    uint8_t source_priority[4];  // ordered SYS_SRC_* values; default flash,usb,optical,network
    uint8_t reserved[49];        // MUST be zero; pads struct to 192 bytes
};
#pragma pack(pop)

static_assert(sizeof(SystemSettings) == 192, "SystemSettings must be 192 bytes");
