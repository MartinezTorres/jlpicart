# Capability ID Catalog

This document lists all defined capability IDs for the JLPiCart platform.

A capability ID is a stable dotted string name (e.g. `sw.mapper`) combined with
a stable 16-bit numeric ID for use in the `GET_CAPS` API response. Both forms
are stable across firmware versions — once assigned, an ID is never reused or
renumbered.

## How capabilities work

Every capability goes through a three-stage pipeline before the platform uses it:

1. **Declared** — the capability exists in the Board Definition (for hardware
   capabilities) or the firmware Build Descriptor (for software capabilities).
   Being declared means "this board/firmware knows about this."

2. **Allowed** — the signed policy document does not mask this capability. Being
   allowed means "the policy permits this capability to be active."

3. **Activated** — for hardware capabilities: a safe probe confirmed the hardware
   is present. For software capabilities: the allocator confirmed the required
   resources (RAM, ports, MSX pages) are available for this payload. Being
   activated means "this capability is live for this session."

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
| 0x0102 | `storage.mass` | sw | Nextor-compatible block device interface for MSX-DOS/SymbOS | Planned |
| 0x0103 | `storage.floppy` | sw | WD279x-compatible floppy controller emulation | Planned |

### Domain 0x0200: Network

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0201 | `net.wifi` | hw | ESP32 WiFi co-processor via UART AT interface | Declared, not activated |
| 0x0202 | `net.eth` | hw | Ethernet adapter (not on reference board; reserved for custom boards) | Planned |
| 0x0203 | `net.esp32` | sw | Software driver for ESP32 AT transport | Planned |

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

### Domain 0x0500: Video

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0501 | `video.crt` | hw | CRT/VGA analog video output via PIO+DMA | Declared, not implemented |
| 0x0502 | `video.v9990` | sw | V9990/Graphics9000 compatible VDP emulation | Planned |

### Domain 0x0600: Audio

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x0601 | `audio.out` | hw | Stereo DAC audio output | Declared, not implemented |
| 0x0602 | `audio.opl4` | sw | OPL4 (MoonSound FM+PCM) emulation at ports 0x7E–0x7F | Planned |

### Domain 0x1000: Software capabilities (sw.*)

| Numeric ID | String ID | H/W | Description | Status |
|---|---|---|---|---|
| 0x1001 | `api.core` | sw | Core API service (GET_API_INFO, GET_CAPS, GET_RANDOM, etc.) | Implemented |
| 0x1002 | `sw.mapper` | sw | MSX ROM mapper emulation (ROM, Konami, ASCII8, ASCII16, etc.) | Implemented |
| 0x1010 | `sw.psg` | sw | AY-3-8910 PSG audio emulation | Planned |
| 0x1011 | `sw.scc` | sw | Konami SCC/SCC+ audio emulation | Planned |
| 0x1020 | `sw.menu` | sw | Menu host ABI and Z80 menu stub | Implemented |

## Status definitions

| Status | Meaning |
|---|---|
| **Implemented** | Fully functional in current firmware. Included in `GET_CAPS` response when allowed. |
| **Declared, not activated** | Descriptor exists. Hardware probe not yet implemented (probe returns not-present). Not in `GET_CAPS` response unless `verify: none`. |
| **Declared, not implemented** | Descriptor exists. Driver code not yet written. Activation always fails. |
| **Planned** | Numeric ID reserved. Descriptor not yet in the build. Not in `GET_CAPS` response. |

## Adding a new capability

1. Assign a numeric ID in the appropriate domain above. Update this document.
   Update `spec.md §5.1` with the same assignment.

2. Add the string ID to `src/boards/board_descriptor_jlpicart.cc` (hardware) or
   `src/drivers/driver_descriptor_table.cc` (software).

3. Add the numeric ID mapping to `src/msx/api/api_types.h` in the `CapId` enum.

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
                         //   audio.*:   0 (feature presence only)
};

// Response payload layout:
// [u16 count] [CapEntry × count]
```
