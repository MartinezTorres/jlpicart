#pragma once
// tusb_config.h — tinyusb host configuration for JLPiCart (RP2350).
//
// RP2350 uses the same USB FS peripheral as RP2040.  This version of
// tinyusb (ext/tinyusb) does not yet define OPT_MCU_RP2350, so we use
// OPT_MCU_RP2040 which maps to the same register layout.
//
// USB host is driven cooperatively: Core 1 calls UsbHost::poll()
// (which calls tuh_task()) on every service-loop iteration.

// ---------------------------------------------------------------------------
// MCU and port
// ---------------------------------------------------------------------------

#define CFG_TUSB_MCU          OPT_MCU_RP2040   // same USB core as RP2040
#define BOARD_TUH_RHPORT      0u               // RP2350 USB FS port

// ---------------------------------------------------------------------------
// Operating mode: host-only on port 0
// ---------------------------------------------------------------------------

#define CFG_TUSB_RHPORT0_MODE OPT_MODE_HOST

// ---------------------------------------------------------------------------
// OS: cooperative (no RTOS — tuh_task() polled from Core 1 service loop)
// ---------------------------------------------------------------------------

#define CFG_TUSB_OS           OPT_OS_NONE

// ---------------------------------------------------------------------------
// Debug level (0 = silent)
// ---------------------------------------------------------------------------

#define CFG_TUSB_DEBUG        0

// ---------------------------------------------------------------------------
// Host: enable MSC only (no hub, no CDC, no HID)
// ---------------------------------------------------------------------------

#define CFG_TUH_ENABLED       1
#define CFG_TUH_MSC           1
#define CFG_TUH_HUB           0
#define CFG_TUH_CDC           0
#define CFG_TUH_HID           0
#define CFG_TUH_VENDOR        0

// ---------------------------------------------------------------------------
// Enumeration buffer (bytes) — large enough for full device/config descriptor
// ---------------------------------------------------------------------------

#define CFG_TUH_ENUMERATION_BUFSIZE 256
