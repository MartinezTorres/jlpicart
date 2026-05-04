# Capability ID Catalog

This document lists all defined capability IDs for the JLPiCart platform.

A capability ID is a stable dotted string name (e.g. `sw.mapper`) combined with
a stable 16-bit numeric ID for use in the `GET_CAPS` API response. Both forms
are stable across firmware versions — once assigned, an ID is never reused or
renumbered.

## What is (and isn't) a capability

**Capabilities describe what the platform itself provides** — the board's physical
hardware (bus interface, network hardware, audio output, video output, I/O ports,
UI) and the firmware's always-on services (API, mapper emulation, menu ABI).

**MSX bus device chips are NOT capabilities.** Audio synthesisers (PSG, SCC,
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
   capabilities) or the firmware Build Descriptor (for software capabilities).
   Being declared means "this board/firmware knows about this."

2. **Allowed** — the signed policy document does not mask this capability. Being
   allowed means "the policy permits this capability to be active."

3. **Activated** — for hardware capabilities: a safe probe confirmed the hardware
   is present. For software capabilities: the allocator confirmed the required
   resources (RAM, ports, MSX pages) are available. Being activated means "this
   capability is live for this session."

`GET_CAPS` returns the **allowed** set by default. The response includes
`cap_flags` which indicates the current activation status.

## cap_flags field

| Bit | Meaning |
|---|---|
| 0 | Activated (1 = active for current session) |
| 1 | Hardware-backed (1 = hw, 0 = sw emulation) |
| 2 | Probe succeeded (1 = verified present, 0 = board-trust or sw) |
| 3–15 | Reserved, zero |

## ID table

IDs are grouped by domain. Within each domain, hardware capabilities use
IDs 0x0001–0x00FF and software capabilities use IDs 0x0100–0x01FF.
Each domain block is 0x0100 wide.

### Domain 0x0000: Core platform services

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0001 | `bus.msx` | hw | MSX bus interface — memory and IO read/write at Z80 bus speeds | Implemented |

### Domain 0x0100: Storage

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0101 | `storage.ext_flash` | hw | External SPI flash (16MB) for payload and config storage | Implemented |
| 0x0102 | `storage.mass` | sw | Nextor-compatible block device layer (bridges USB/flash to MSX-DOS/SymbOS) | Planned |

### Domain 0x0200: Network

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0201 | `net.wifi` | hw | ESP32 WiFi co-processor via UART AT interface | Declared, not activated |
| 0x0202 | `net.eth` | hw | Ethernet adapter (not on reference board; reserved for custom boards) | Planned |
| 0x0203 | `net.esp32` | sw | Software driver for ESP32 AT transport | Declared, not activated |

### Domain 0x0300: I/O

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0301 | `io.usb_host` | hw | USB host port for controllers, USB sticks, optical drives | Implemented |
| 0x0302 | `io.adc` | hw | ADC channels (temperature sensor, battery voltage, GPIO) | Implemented |
| 0x0303 | `io.rs232` | hw | RS232 / UART interface (custom boards only; not on reference design) | Planned |

### Domain 0x0400: UI

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0401 | `ui.oled` | hw | SSD1306 128×32 OLED display via I2C | Declared, not implemented |
| 0x0402 | `ui.eink` | hw | E-ink display interface (custom boards only) | Planned |

### Domain 0x0500: Video outputs

Video capabilities describe physical output hardware on the board, not the video
chip generating the signal. The video source is a collection device.

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0501 | `video.crt` | hw | CRT/VGA analog video output via PIO+DMA | Declared, not implemented |

### Domain 0x0600: Audio outputs

Audio capabilities describe physical output hardware on the board, not the audio
chip generating the signal. All active audio devices in the current payload are
mixed and sent to `audio.out`. The source chips (PSG, SCC, OPL4, …) are
collection devices.

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0601 | `audio.out` | hw | Stereo DAC audio output | Declared, not implemented |

### Domain 0x1000: Software capabilities (sw.*)

Always-on firmware services that are active regardless of which collection is loaded.

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x1001 | `api.core` | sw | Core API service (GET_API_INFO, GET_CAPS, GET_RANDOM, etc.) | Implemented |
| 0x1002 | `sw.mapper` | sw | MSX ROM mapper emulation (ROM, Konami, ASCII8, ASCII16, etc.) | Implemented |
| 0x1020 | `sw.menu` | sw | Menu host ABI and Z80 menu stub | Implemented |

## Status definitions

| Status | Meaning |
|---|---|
| **Implemented** | Fully functional in current firmware. Included in `GET_CAPS` response when allowed. |
| **Declared, not activated** | Descriptor exists. Hardware probe not yet implemented (probe returns not-present). Not in `GET_CAPS` response unless `verify: none`. |
| **Declared, not implemented** | Descriptor exists. Driver code not yet written. Activation always fails. |
| **Planned** | Numeric ID reserved. Descriptor not yet in the build. Not in `GET_CAPS` response. |

## Adding a new capability

Only add a capability if it describes something the **board or firmware always
provides**, independent of which collection is loaded.

Do NOT add a capability for:
- MSX audio chips (PSG, SCC, OPL4, …) — declare them in the manifest `devices` list
- MSX video chips (V9990, …) — same
- MSX storage controllers (Sunrise IDE, floppy, …) — same

To add a genuine platform capability:

1. Assign a numeric ID in the appropriate domain above. Update this document.

2. Add the string ID to `src/boards/board_descriptor_jlpicart.cc` (hardware) or
   `src/drivers/driver_descriptor_table.cc` (software).

3. Add the numeric ID constant and mapping to `src/msx/api/api_types.h`.

4. Implement the capability and add host tests.

IDs are assigned permanently. An ID that was once used for capability X must
never be reassigned to capability Y, even if X is later removed.

## GET_CAPS response format

```c
// Response payload from GET_CAPS (method 0x02 of System service 0x00).
// Fixed-size entries follow the u16 count field.

struct CapEntry {
    uint16_t cap_id;     // stable numeric ID from this table
    uint16_t cap_flags;  // see cap_flags field definition above
    uint32_t cap_param;  // capability-specific parameter:
                         //   sw.mapper: 0 (no fixed parameter)
                         //   storage.*: capacity in KB
                         //   audio.out: sample rate in Hz
                         //   video.crt: 0 (presence only; config via Board Definition)
};

// Response payload layout:
// [u16 count] [CapEntry × count]
```
