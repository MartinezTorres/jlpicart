// board_descriptor_jlpicart.cc — hardware capabilities for jlpicart_board.
//
// This is the ONLY file that declares hardware capabilities for this board.
// No other file may add or remove capabilities here.
// See bootstrapping.md Appendix: "Do not add scattered compile-time flags
// for peripherals; add descriptors instead."

#include "board_descriptor.h"

// ---------------------------------------------------------------------------
// Declared hardware capabilities for the JLPiCart reference board.
//
// safe_verify=true means the firmware MAY probe this capability (e.g. read
// an I2C register) to determine if it is physically present. Probing is
// deferred until Stage 8 (Activation v1) and only runs when requested.
// safe_verify=false means no probing is ever permitted (e.g. writing to
// an absent chip could cause bus conflicts on this board).
// ---------------------------------------------------------------------------
// Capability IDs use functional domain prefixes per spec.md §5.1.
// The hw/sw origin is metadata recorded in the BoardDescriptor/DriverDescriptor,
// NOT part of the capability ID itself — IDs must be stable across board variants.
static const BoardCapabilityDecl kJlpicartCapabilities[] = {
    // MSX bus interface — always present on any JLPiCart board.
    { "bus.msx",           false },  // core; no probing needed

    // External SPI flash (16MB, for payload storage and config).
    { "storage.ext_flash", false },  // verified by SDK at boot

    // ESP32 Wi-Fi co-processor (UART AT interface).
    { "net.wifi",          true  },  // safe to ping via AT command

    // USB host port (controllers, USB sticks, optical drives).
    { "io.usb_host",       true  },  // safe to enumerate

    // OLED display (SSD1306, 128×32, I2C).
    { "ui.oled",           true  },  // safe to probe I2C address

    // CRT/VGA analog video output (via PIO+DMA).
    { "video.crt",         false },  // PIO-driven; avoid unless configured

    // Stereo audio output.
    { "audio.out",         false },  // DAC-driven; avoid unless configured

    // ADC (temperature, battery voltage, general GPIO).
    { "io.adc",            true  },  // safe to read ADC channels
};

static const BoardDescriptor kJlpicartDescriptor = {
    .board_id         = "jlpicart_v1",
    .capabilities     = kJlpicartCapabilities,
    .capability_count = sizeof(kJlpicartCapabilities) / sizeof(kJlpicartCapabilities[0]),
};

const BoardDescriptor& BoardDescriptor::for_current_board() {
    return kJlpicartDescriptor;
}
