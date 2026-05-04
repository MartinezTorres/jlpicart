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
#define BOARD_TUH_RHPORT      0u               // USB FS port — host mode
#define BOARD_TUD_RHPORT      0u               // USB FS port — device mode

// Note: CFG_TUSB_RHPORT0_MODE is omitted intentionally.  Both host and device
// stacks are compiled in; the application selects one at runtime by calling
// either tuh_init(BOARD_TUH_RHPORT) or tud_init(BOARD_TUD_RHPORT) based on
// Platform::msx_clock_present() at early boot.

// ---------------------------------------------------------------------------
// OS: cooperative (no RTOS)
// ---------------------------------------------------------------------------

#define CFG_TUSB_OS           OPT_OS_NONE

// ---------------------------------------------------------------------------
// Debug level (0 = silent)
// ---------------------------------------------------------------------------

#define CFG_TUSB_DEBUG        0

// ---------------------------------------------------------------------------
// Host stack: MSC only (USB flash drives)
// ---------------------------------------------------------------------------

#define CFG_TUH_ENABLED             1
#define CFG_TUH_MSC                 1
#define CFG_TUH_HUB                 0
#define CFG_TUH_CDC                 0
#define CFG_TUH_HID                 0
#define CFG_TUH_VENDOR              0
#define CFG_TUH_ENUMERATION_BUFSIZE 256

// ---------------------------------------------------------------------------
// Device stack: MSC on internal flash (USB config/programming mode)
// ---------------------------------------------------------------------------

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MSC                 1
#define CFG_TUD_ENDPOINT0_SIZE      64
#define CFG_TUD_MSC_EP_BUFSIZE      512
