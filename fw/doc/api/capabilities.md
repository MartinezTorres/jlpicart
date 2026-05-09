# Capability ID Catalog

This document lists all defined capability IDs for the JLPiCart platform.

A capability ID is a stable dotted string name (e.g. `sw.mapper`) registered in the
firmware's capability registry. The registry is populated from two sources:
- **Board descriptor** (`src/platform/board_descriptor_jlpicart.cc`) — hardware capabilities
- **Driver descriptor table** (`src/spine/driver_descriptor_table.cc`) — software capabilities

## What is (and isn't) a capability

**Capabilities describe what the platform itself provides** — the board's physical
hardware (bus interface, network hardware, audio output, video output, I/O ports,
UI) and the firmware's always-on services (API, mapper emulation, ESP32 transport).

**MSX bus device chips are NOT capabilities.** Audio synthesizers (PSG, SCC,
OPL4), video processors (V9990), storage controllers (Sunrise IDE), and any
other chips that a Collection places in cartridge slots are **collection
devices**. They are declared per-payload in the Collection Manifest and
provisioned on demand subject to resource availability.

Concretely:
- `audio.out` is a capability — it says the board has an audio output jack.
- A PSG chip is a collection device — a payload declares it in `devices`.
- `video.crt` is a capability — it says the board has a CRT/VGA output.
- A V9990 chip is a collection device — a payload declares it in `devices`.

## How capabilities work

Every capability goes through a three-stage pipeline before the platform uses it:

1. **Declared** — the capability exists in the Board Definition (for hardware
   capabilities) or the driver descriptor table (for software capabilities).
   Being declared means "this board/firmware knows about this."

2. **Allowed** — the signed policy document does not mask this capability. Being
   allowed means "the policy permits this capability to be active." Currently
   only `net.esp32` is policy-gated (on `POLICY_ALLOW_NETWORK_COLLECTION_INSTALL`).
   All other capabilities are never masked by policy.

3. **Activated** — for hardware capabilities: a safe probe confirmed the hardware
   is present (or the capability is trusted as board-level silicon). For software
   capabilities: the allocator confirmed the required resources (SRAM) are available.
   Being activated means "this capability is live for this session."

The `GET_CAPABILITIES` API returns the registered set with per-capability status:
`is_hw` (hardware or software), `allowed` (policy does not mask), `activated`
(probe succeeded and resources allocated).

## Registered capabilities

### Hardware capabilities (board descriptor)

Declared in `src/platform/board_descriptor_jlpicart.cc`. All are marked as
hardware capabilities. Those with `safe_verify = true` are probed at activation time.

| String ID | Description | Safe verify | Probe method |
|---|---|---|---|
| `bus.msx` | MSX bus interface — address/data/control GPIO | No | Board-trust (RP2350 silicon) |
| `storage.ext_flash` | External SPI flash (W25Q080) for payload and config storage | No | Board-trust |
| `net.wifi` | ESP32 WiFi co-processor via UART AT interface | Yes | UART0 AT ping, 1500 ms timeout |
| `io.usb_host` | USB host port for controllers, USB sticks | Yes | Always true (RP2350 silicon) |
| `ui.oled` | SSD1306 128x32 OLED display via I2C | Yes | I2C0 scan 0x3C/0x3D, deinit after |
| `video.crt` | CRT/VGA analog video output | No | Board-trust |
| `audio.out` | Stereo DAC audio output | No | Board-trust |
| `io.adc` | ADC channels (battery voltage sensor) | Yes | Always true (RP2350 silicon) |

### Software capabilities (driver descriptors)

Declared in `src/spine/driver_descriptor_table.cc`. Resource requirements are
enforced by the allocator.

| String ID | SRAM bytes | Description |
|---|---|---|
| `api.core` | 16384 | Core API service — API Window ring-based IPC (GET_BUILD_INFO, GET_CAPABILITIES, GET_DEVICE_ID) |
| `sw.mapper` | 0 | MSX ROM mapper emulation (ROM, ROM_32K_MIRRORED, KONAMI, KONAMI_SCC, KONAMI_Z, ASCII8, ASCII16, RAM) |
| `net.esp32` | 0 | Software driver for ESP32 AT transport (HTTP client) |

## Capability entry structure

Each registered capability is stored as a `CapabilityEntry`:

```c
struct CapabilityEntry {
    const char*          name;           // Dotted string name, e.g. "bus.msx"
    bool                 is_hw;          // true = hardware, false = software
    bool                 safe_verify;    // true = can be probed safely
    bool                 allowed;        // true = policy does not mask
    bool                 activated;      // true = probe succeeded + resources allocated
    ResourceRequirements requirements;   // SRAM, PIO, DMA requirements
};
```

Maximum 32 capabilities in the registry (`CAPABILITY_REGISTRY_MAX = 32`).

## Hardware probes

Hardware capabilities with `safe_verify = true` are probed by `hw_probe()` during
activation. Probes must not permanently configure hardware, must complete within
~2 seconds, and must be safe when the hardware is absent.

| Capability | Probe |
|---|---|
| `net.wifi` | Sends AT command over UART0 (GPIO 44/45, 115200 baud), waits for OK within 1500 ms |
| `ui.oled` | Scans I2C0 for addresses 0x3C and 0x3D, then deinitializes I2C0 |
| `io.usb_host` | Always returns true (RP2350 USB controller is always present) |
| `io.adc` | Always returns true (RP2350 ADC is always present) |

## Resource model

The allocator checks resource availability before activating software capabilities:

| Resource | Total | System reserve | Available |
|---|---|---|---|
| SRAM | 520 KB | 128 KB | 392 KB |
| PIO state machines | 12 | 4 | 8 |
| DMA channels | 16 | 2 | 14 |

## Policy gating

Only one capability is policy-gated:

| Capability | Policy flag | Behavior when flag is clear |
|---|---|---|
| `net.esp32` | `POLICY_ALLOW_NETWORK_COLLECTION_INSTALL` | Not allowed; cannot be activated |

All other capabilities are never masked by policy.

## Adding a new capability

Only add a capability if it describes something the **board or firmware always
provides**, independent of which collection is loaded.

Do NOT add a capability for:
- MSX audio chips (PSG, SCC, OPL4, ...) — declare them in the manifest `devices` list
- MSX video chips (V9990, ...) — same
- MSX storage controllers (Sunrise IDE, floppy, ...) — same

To add a genuine platform capability:

1. Add the string ID to `src/platform/board_descriptor_jlpicart.cc` (hardware) or
   `src/spine/driver_descriptor_table.cc` (software).

2. If hardware and probeable, implement the probe in `src/spine/hw_probe.cc`.

3. If software and resource-intensive, declare SRAM/PIO/DMA requirements in the
   `ResourceDecl`.

4. If policy-gated, add the check in `src/spine/capability_registry.cc::is_masked_by_policy()`.
