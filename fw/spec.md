# JLPiCart — Specification Book

**Status:** Draft (living document)  
**Revision:** v20  **Last updated:** 2026-03-07 (Europe/Berlin)

## 1. Introduction

JLPiCart is a modern multi-function MSX cartridge platform built around an RP2350-class MCU and an ESP32-class Wi‑Fi coprocessor. It aims to combine “plug-and-play” usability with a clear, testable set of contracts so that firmware, tools, and content can evolve without breaking compatibility.

This book is the single normative reference for what JLPiCart **is**, what it **exposes to MSX software**, and what it **guarantees** to publishers and developers. It intentionally mixes narrative (to keep the mental model clear) with RFC‑style requirements (MUST/SHOULD/MAY) where behavior must be stable.

### 1.1 Reader’s guide

This document captures the current product vision, shared terminology, fixed constraints, committed architectural choices, and expected platform behavior for JLPiCart. It is intentionally a single “source of truth” document for *what the platform is* and *how it is expected to behave*, while keeping implementation details flexible unless they are explicitly marked as fixed.

When this document says “Collection Manifest” or “Payload Manifest”, it refers to the Manifest file that belongs to that entity. “System Settings” and “User Profiles” are cartridge-wide and persist across Collection changes.

### 1.2 Normative language

The key words **MUST**, **MUST NOT**, **SHOULD**, **SHOULD NOT**, and **MAY** are to be interpreted as described in RFC 2119.

### 1.3 Formatting conventions

This spec uses a small set of formatting rules to keep it readable and implementable.

- Use **bold** for named domain concepts when they are being defined (especially in the Glossary) and for short callout labels like **Note:** and **Rationale:**.
- Use `inline code` for technical identifiers: register names, port numbers, memory ranges, file paths, method names, enum values, and on-disk field names.
- Use fenced code blocks for anything that must be copied verbatim: binary layouts, struct definitions, schemas, examples, and protocol transcripts. All fenced blocks are tagged with a language (usually `text`, `c`, or `json`).
- Use `-` for unordered lists, and numbered lists only when order matters.

## Table of contents

- [1. Introduction](#1-introduction)
- [2. Vision, positioning, and use cases](#2-vision-positioning-and-use-cases)
- [3. Glossary](#3-glossary)
- [4. Platform overview](#4-platform-overview)
- [5. Core runtime contracts](#5-core-runtime-contracts)
- [6. Content and configuration specifications](#6-content-and-configuration-specifications)
- [7. JLPiCart developer APIs](#7-jlpicart-developer-apis)
- [8. Menu host stub ABI](#8-menu-host-stub-abi)
- [9. Peripheral framework and catalog](#9-peripheral-framework-and-catalog)
- [10. Security, licensing, and privacy](#10-security-licensing-and-privacy)
- [11. Updates and provisioning](#11-updates-and-provisioning)
- [12. Online services](#12-online-services)
- [13. Diagnostics and conformance](#13-diagnostics-and-conformance)
- [14. Open investigations and references](#14-open-investigations-and-references)
- [Appendix A. Writing and maintenance conventions](#appendix-a-writing-and-maintenance-conventions)
- [Appendix B. Reference repository layout and module map](#appendix-b-reference-repository-layout-and-module-map)

## 2. Vision, positioning, and use cases

### 2.1 Vision

JLPiCart is an open collection of tools (PCB design, firmware, and specifications) built around the RP2350B microcontroller for anyone who wants to publish software for the MSX ecosystem.

It is not meant as a flash cartridge for end-users. Carnivore2, MegaFlashROM, FlashJacks, and Pico+ already cover that space well. JLPiCart is for publishers. It provides everything needed to design and distribute inexpensive, purpose-built MSX cartridges with wide hardware support and a coherent end-user experience, without building any of that infrastructure from scratch. Mappers, RAM expansion, audio emulation, floppy and mass storage, networking, user identity, save management, and high score sync are all provided by the platform. A publisher focuses on the software. JLPiCart handles the rest.

The platform is fully open source. Publishers who need capabilities beyond the standard feature set can modify the firmware and PCB design freely. Anything built on JLPiCart can be published and shared. Everything works offline. Network features are always additive and never required.

### 2.2 Scope and positioning

JLPiCart is built for publishers and collection authors who want to ship purpose-built MSX cartridges with a coherent end-user experience and a stable software interface.

JLPiCart is not intended to compete with general-purpose end-user flash cartridges. It aims to be a publisher-focused platform: content packaging, device emulation, identity/profile support, save management, and (optional) online features are provided by the platform so that MSX software can stay simple.

Everything must work offline. Network features are additive.

### 2.3 Use cases

The following named use cases define the scenarios the platform explicitly supports. They serve as acceptance criteria: a platform that handles all of these is delivering on its promise.

**Inexperienced collection author**
A non-technical user wants to bundle a set of ROMs into a polished collection with artwork and descriptions and share it with the community. They use a USB stick to load content onto the cartridge, write a simple Manifest file, and optionally add Assets. No coding required. The Menu handles the rest.

**Game publisher**
A developer has written an MSX game and wants to distribute it as a purpose-built cartridge. They declare the hardware requirements (mapper, audio, RAM) in a Manifest. The platform provides user identity, save management, high score submission, and network access via the JLPiCart API. The publisher focuses on the game. The cartridge handles infrastructure.

**Developer with custom hardware extension**
A developer wants to add custom hardware to their cartridge — for example, an RS232 port or a proprietary input device. They modify the firmware and board design, expose it as a standard MSX peripheral via IO ports, and integrate it with their game via the JLPiCart API. The open source codebase and board definition system make this straightforward.

**Multi-game collection (curated)**
A publisher wants to release a curated back-catalog of their games, similar to a SNES Mini. The Collection contains multiple Payloads, each with its own Assets. The cartridge always boots to the Menu first. The user selects a game, which launches directly. Profiles, saves, and high scores are tracked per game per user.

**SymbOS distribution**
A distributor wants to ship a SymbOS-based product. The cartridge provides RAM expansion, a Nextor-compatible mass storage device, and network access. SymbOS boots from the cartridge on any MSX2 machine with 64KB internal RAM. Full compatibility requires dedicated investigation — see Open Investigations.

**CD-based game release**
A publisher distributes games on CD. The cartridge is configured to boot from the optical drive at highest priority. On power-on, a loading screen appears while the disc is read, then the game launches. The cartridge provides whatever devices the game requires (audio, RAM, network). The user experience is seamless — insert disc, power on, play.

**Online multiplayer game**
A publisher releases a game with network features: online high scores, head-to-head multiplayer, and ghost data sync. The game uses the JLPiCart API for all network interactions. The cartridge manages user identity and WiFi credentials. The game never handles network configuration directly. Because the title uses the JLPiCart API, it requires a JLPiCart-class cartridge that provides the network stack (ESP32/WiFi). Separately, the cartridge may expose legacy-compatibility layers (e.g., MSX-UNAPI networking and/or SymbOS-specific drivers) for older software, but this does not make JLPiCart-API titles portable to other MSX network cartridges.

**Guest session / demo**
A publisher wants to let users try a game before purchasing. A licensed user invites a friend to play via the JLPiCart API. The friend's cartridge downloads a temporary Collection over the network, plays the game within publisher-defined limits, and the session expires automatically. No physical media required.

**Firmware and platform customization**
A developer wants to add a device emulator not currently supported by the platform (e.g., a custom sound chip or a specific MSX peripheral). They fork the firmware, implement the new device following the existing peripheral device pattern, and distribute their modified board definition and firmware. Other users can adopt their work by flashing it.

---

## 3. Glossary

**Collection** — A named bundle containing a Manifest, zero or more Payloads, and optional Assets. Only one Collection is active at a time.

**Payload** — A single emulatable media item within a Collection, consisting of its Content, a Manifest, and optional Assets. A Collection may have multiple Payloads active simultaneously (e.g., a ROM plus a floppy bundle plus a mass storage image).

**Content** — The primary data of a Payload: a ROM image, a mass storage image, or a floppy bundle (one or more floppy disk images).

**Manifest** — Structured textual metadata describing a Collection or Payload. Includes identity (name, author, version), hardware requirements (mapper, RAM, audio devices, VDP, network access), licensing terms, and boot defaults. Machine-readable. Hard requirements in a Manifest cannot be overridden by the user; soft defaults can.

**Assets** — Optional non-textual media associated with a Collection or Payload, used by the Menu: images, animations, music.

**Source** — Where a Collection is loaded from. Sources are: internal flash, USB stick, optical drive (CD/DVD), or network. External sources take priority over internal flash.

**System Settings** — Cartridge-wide configuration that persists across Collections: WiFi credentials, video output mode, language, firmware settings.

**User Profile** — A named identity stored on the cartridge, optionally synced to the cloud. Contains language preference, save data, and high scores across all Collections. Multiple profiles may exist on one cartridge. A profile has a local ID and an optional cloud identity (username/password) for sync.

**Guest Session** — A temporary profile tied to a licensed user's Collection. Allows a user without a copy of a Collection to play it, subject to publisher-defined limits (time, sessions, features).

**Persistent Storage** — Flash storage that survives Collection changes: System Settings, User Profiles, save data, high scores, cached network payloads.

**JLPiCart API** — The stable, versioned interface exposed by the cartridge to MSX software. Provides games and applications access to: user identity, save/load operations, high score submission, network sockets, peripheral queries, and multiplayer session management. Designed to be usable from Z80 assembly without pain.

**Menu** — The cartridge's own software, not part of any Collection. Handles Collection selection, System Settings, User Profile management, and publisher tools. Adapts its rendering to the detected MSX generation (text-only on MSX1/TMS9918, richer graphics on MSX2/V9938, best quality on MSX2+/V9958).

**Provisioning Bundle** — A signed installation bundle used to initialize a blank device (unprovisioned → provisioned) and optionally install a default Collection and lock policy.

**Install Intent** — A single install directory on provisioning media (`/JLPICART/INSTALL/<install_id>/`) containing metadata, publisher identity, bundle bytes, and signatures.

**Install Receipt** — An append-only local record describing an installation decision (installed/rejected) including bundle digests and stable reason codes.

**Publisher Identity Certificate (PIC)** — A certificate binding a stable Publisher ID to public keys and allowed usages, validated against a Publisher Root Key (PRK).

**Lock Policy** — The device’s trust posture defining which signers are accepted for firmware and content (platform-only, publisher-only, platform+publisher) and what recovery/rotation is permitted.

---

## 4. Platform overview

This chapter introduces the *physical* and *logical* shape of the platform: what is fixed, what is configurable, and how the system behaves at runtime.

### 4.1 Fixed constraints and non‑negotiables

This section lists constraints that are treated as “foundation” and are not expected to change because they impact hardware, compatibility, or long-term support.

### Hardware baseline

The platform targets the RP2350 family as the main MCU (reference designs may use RP2350B + external flash; minimal boards may use an RP2350 variant with integrated flash if appropriate).

The platform supports a range of boards; capabilities are a function of the board design. Firmware is configured per board at build time via a board definition.

### Real-time MSX bus handling

One core is dedicated to MSX bus servicing (memory and IO) as a latency-critical, never-returning loop running from fast memory. The remaining core is used for all non-bus work (drivers, UI, IO, networking, etc.).

### Networking module

WiFi is provided by an ESP32 co-processor using an AT-command interface over UART.

### Legacy networking compatibility (MSX ecosystem)

Optionally, the cartridge exposes legacy-compatibility layers so existing MSX software can use its networking and storage without modification. This may include MSX-UNAPI networking (Ethernet and/or TCP/IP) and, where needed, SymbOS-specific drivers. This is orthogonal to the JLPiCart API and does not make JLPiCart-API titles portable to other network cartridges.

### USB operating mode

During normal operation, the USB port operates in host mode. User-facing workflows must not rely on presenting the cartridge as a USB device to a PC. USB device mode is limited to initial firmware flashing via ROM bootloader, and MAY be disabled on sealed units as part of the security posture (see Security Contracts).

### Offline-first

Core functions (loading and running Collections, save management, device emulation, menu UX) must remain usable with no network connectivity. Network features are optional and user-controllable.

### 4.2 Committed architectural choices and current system model

This section captures architectural choices and system structure that are already assumed by the platform. Where appropriate, it distinguishes between intent (“what must be true”) and a typical mechanism (“how we currently expect to do it”).

### Board configurability

JLPiCart is a reference design, not a fixed product. Boards may omit peripherals; supported devices are derived from a board definition and enforced by validation at load time.

### Cartridge control mode and UI

When the cartridge needs to present UI (menu, loading screens), it does so by running MSX-side code on the Z80 and using the host MSX peripherals (VDP, keyboard, etc.) for display and input. The specific mechanism may evolve, but the user-visible behavior is a stable expectation.

### Payloads loaded from non-RAM sources

Because the MSX bus is real-time, payload content cannot be mapped directly from slow sources (USB storage, optical drive, network) in the general case. The platform therefore relies on RAM-buffered strategies and (when required) use of the MSX WAIT line to cover cache misses. The exact cache size is a tuning parameter, not a product guarantee.

### JLPiCart API model

The JLPiCart API is the *platform contract* between MSX software and the cartridge firmware. It must be stable, versioned, and testable in emulation.

### Access mechanisms

The API access mechanism must balance three competing needs: performance (games), interoperability (running under MSX-DOS / SymbOS), and avoiding global resource conflicts (I/O ports).

**Design intent**

- Primary access: a **memory-mapped API window** (mailbox + ring buffers) exposed in the cartridge address space. This avoids global I/O port conflicts and enables fast bulk transfers using standard Z80 block operations.
- Optional fast-path: a minimal **I/O “doorbell/status” port pair** for quick polling / wakeups (useful when an application cannot keep the API window mapped continuously).
- Optional discovery wrapper: expose a small **MSX-UNAPI compatible** call gate (API identifier `JLPICART`) that lets software discover the API version and locate the active API window via EXTBIO. This keeps the core API memory-oriented, while still allowing access from contexts that are not executing out of the cartridge slot.


### Fixed MSX-visible allocations for the JLPiCart API (v1)

This section freezes the MSX-visible “constants” required for interoperability: where the API window lives, how it can be discovered, and which (minimal) I/O registers are reserved.

#### Memory-mapped API window placement

The JLPiCart API window is a **16KB memory-mapped region** at `0x8000–0xBFFF` (Z80 page 2). This region is the primary transport for requests, responses, and bulk data.

The Menu host stub program is mapped at `0x4000–0x7FFF` (Z80 page 1) when the Menu is active.

These placements are chosen to match MSX-UNAPI requirements for an entry point on page 1, while keeping bulk transfer on page 2 where most applications can temporarily switch mappings without relocating their entire code.

#### Subslot layout (expanded-slot baseline)

To reduce mapping conflicts, the cartridge **presents itself as an expanded slot** (secondary slots / “subslots”) and uses a stable internal subslot convention:

- **Subslot 0 — Payload**: the active Payload ROM / mapper-visible memory.
- **Subslot 1 — System**: Menu stub, loader helpers, and the MSX-UNAPI call gate.
- **Subslot 2 — API**: the JLPiCart API window at `0x8000–0xBFFF` plus any additional API-only MMIO windows.
- **Subslot 3 — Reserved**: future use.

Primary slot selection is performed through the MSX PPI primary-slot register at I/O port `0xA8`. Secondary slot selection for an expanded slot is performed through the secondary-slot register at memory address `0xFFFF` **within that slot**; reading the register returns the value inverted on many implementations.

Software MUST treat subslot switching as a privileged mapping operation and restore the previous mapping after performing API transfers.

#### Minimal doorbell/status registers (switchable I/O ports)

The API is memory-first, but many applications benefit from a tiny port-level fast path for polling and wakeups. JLPiCart reserves **switchable I/O ports** using the standard mechanism controlled by port `0x40`:

- To select JLPiCart extended I/O, write device ID `0xC8` (decimal 200) to port `0x40`.
- With that ID selected, ports `0x41–0x4F` are owned by JLPiCart.

JLPiCart reserves the following ports under its switchable I/O ID:

- `0x41` **DOORBELL** (write-only): write any value to signal “work pending” to the cartridge (e.g., request ring has been filled).
- `0x42` **STATUS** (read-only): status bits:

  - bit 0: `API_READY` (1 when the API window is mapped and ready)
  - bit 1: `REQ_PENDING` (1 when request ring is non-empty)
  - bit 2: `RESP_PENDING` (1 when response ring is non-empty)
  - bit 3: `IRQ_CAPABLE` (1 if MSX INT signaling is supported by the board)
  - bit 7: `ERROR` (1 if a sticky error has occurred; read error details via the API)

- `0x43` **API_MAP** (read/write): selects where the API window is mapped.
  - write `0` → unmap API window (all reads return open bus / 0xFF; writes ignored)
  - write `1` → map API window to `0x8000–0xBFFF` (default and required)
  - read returns current mapping (0 or 1)

All other ports in `0x44–0x4F` are reserved for future expansion and MUST read as `0xFF` and ignore writes when not implemented.

#### Optional MSX-UNAPI discovery wrapper (fixed identifier)

When enabled, JLPiCart exposes a small MSX-UNAPI discovery/call gate with identifier `JLPICART` that allows software to locate the active API implementation and discover its entry point. The wrapper follows the MSX-UNAPI discovery procedure (EXTBIO hook at `0xFFCA`) with `DE=0x2222`, and uses the identifier string (NUL-terminated) placed in the `ARG` buffer at `0xF847`.

The UNAPI entry point MUST live in Subslot 1 on Z80 page 1 (`0x4000–0x7FFF`) so that client software can reach it through standard inter-slot calls.

The UNAPI wrapper is **not** the main API. It exists to make it easy for software running under MSX-DOS / SymbOS environments to discover “where the API lives” without hardcoding slot details.

**References (MSX-visible allocations)**

- Switchable I/O ports method and ID ranges: https://www.msx.org/wiki/Switchable_I/O_ports
- MSX I/O port ranges and common device port assignments (OPL4, V9990, external VDP, etc.): https://www.msx.org/wiki/I/O_Ports_List
- Primary slot selection register at `0xA8`: https://www.msx.org/wiki/Programmable_Peripheral_Interface
- Secondary slot register at `0xFFFF`: https://problemkaputt.de/portar.htm
- MSX-UNAPI discovery procedure (identifier length, `DE=0x2222`, `ARG` at `0xF847`): https://raw.githubusercontent.com/Konamiman/MSX-UNAPI-specification/master/docs/MSX%20UNAPI%20specification%201.1.md

### Relationship to legacy compatibility

The JLPiCart API is distinct from legacy MSX networking compatibility layers.

- A title that uses the JLPiCart API requires a JLPiCart-class cartridge (ESP32/WiFi + identity services) and is not expected to work on unrelated network cartridges.
- Separately, the cartridge may expose legacy compatibility layers (MSX-UNAPI Ethernet/TCP-IP, SymbOS drivers, etc.) for older software.

### Source priority

Source priority is configurable via System Settings. Some board designs or publisher-locked Collections may constrain or fix the allowed source priority.

### 4.3 Hardware platform overview

JLPiCart is a reference design, not a fixed product. Anyone can build their own JLPiCart-compatible board with any combination of peripherals. Capabilities are a function of the board design. The firmware is configured per board at build time via a board definition file.

### Minimum Requirements

The only hard requirement for a JLPiCart-compatible board is:

- **RP2350-family MCU** — RP2350B (external flash) or RP2354B (integrated flash), providing dual-core ARM Cortex-M33, 520KB SRAM, and USB.
- **A programming interface** — USB connector or SWD debug header.
- **MSX cartridge edge connector** — address bus A0-A15, data bus D0-D7, full control signals.

A board with only these components is a valid JLPiCart. Its capabilities will be limited but the firmware will build and run for it.

### Core Peripherals

Most boards will include these. Together they unlock almost all JLPiCart functionality:

- **External flash** — larger payload and save storage. The reference design uses 16MB.
- **ESP32 co-processor** (UART, AT firmware) — WiFi connectivity.
- **USB host connector** — connects controllers, USB sticks, and optical drives.

### Situational Peripherals

Optional peripherals for specific use cases:

- **OLED display** (e.g., SSD1306, 128×32) — status and debugging aid.
- **Video output connector** — analog video output (CRT and/or VGA) via PIO. The reference design uses a single shared connector; nothing prevents using two separate connectors.
- **Stereo audio output** — analog audio output for emulated audio devices.
- **GPIO with ADC** — general purpose I/O, temperature sensing, battery voltage monitoring, or custom hardware extensions.

### Reference Design

The JLPiCart reference design targets the **RP2350B** with 16MB external flash and includes all core and situational peripherals listed above. The default firmware build targets this configuration.

---

### 4.4 System architecture overview

### Core Assignment
- **Core 0**: MSX bus loop. Never-returning tight loop handling all memory and IO bus cycles. Latency-critical. Runs from scratch RAM.
- **Core 1**: Task scheduler. Handles peripheral drivers, display updates, WiFi communication, USB, and all non-bus work.

### Bus Abstraction
The bus loop operates on an array of **Cartridge slots**, each with:
- 8 × 8KB memory segments, each with optional direct memory pointer and/or read/write callback
- 256 IO port callbacks (read and write)
- Lifecycle callbacks (init, deinit)

Subslots are supported via the standard MSX slot-expansion mechanism. In expanded slots, a secondary slot-select register is exposed at memory address **0xFFFF** (within the expanded slot’s own address space) to choose the active subslot per 16KB page. Primary slot selection is controlled via the MSX PPI register at **I/O port 0xA8**.

Design intent: the platform supports subslots via the standard MSX slot-expansion mechanism. JLPiCart itself presents as an expanded slot by default to reduce mapping conflicts between the Payload, the Menu/System helpers, and the API window. Payloads and peripherals MAY still choose not to rely on subslots, but JLPiCart-aware software SHOULD assume subslot switching is available on JLPiCart devices.

### Cartridge Control Mode
When the cartridge needs to present its own UI — Menu, loading screens — it does so by mapping a program into the MSX address space that runs on the Z80 and uses the host MSX peripherals (VDP, keyboard) for display and input. The implementation mechanism is not prescribed. The requirement is that the cartridge can present a UI and respond to user input using whatever MSX hardware is available on the host machine, across all supported MSX generations.

### JLPiCart API
From the MSX software perspective, the JLPiCart API is a standard peripheral and does not require cartridge control mode. Primary access is through a memory-mapped API window; a minimal I/O doorbell/status pair and a UNAPI-style discovery/call gate are optional extensions. A game or application uses the API the same way it uses any other MSX peripheral. The exact API window layout and protocol are defined in Chapter 7 (JLPiCart developer APIs). Legacy compatibility layers (MSX-UNAPI and/or SymbOS drivers) are optional and independent.

### Payload Source Priority
Payload source priority is configurable as part of System Settings. A board or Collection may lock the priority order — for example, a CD-focused cartridge may fix the optical drive as the highest priority source regardless of user preference.

### USB
The USB port operates in host mode at all times during normal operation. It connects controllers, USB sticks, and optical drives. Content loading (Collections) is done by plugging a USB stick into the host port or via WiFi — never by presenting the cartridge as a USB device to a PC.

USB device mode is only used during initial firmware flashing, handled transparently by the RP2350B bootrom. This is a production/setup operation, not a user-facing feature.

---

### 4.5 Platform behavior model

This section describes required platform behavior. It is organized by domain and intentionally avoids implementation-specific identifiers.

### Collections and Payloads
- A Collection consists of a Manifest, zero or more Payloads, and optional Assets.
- Text descriptions of a Collection are part of its Manifest, not Assets.
- The Collection format is source-agnostic: the same format is valid on internal flash, USB stick, optical disc, or downloaded from the network.
- Only one Collection may be active at a time.
- A Collection may contain Payloads of mixed types active simultaneously (e.g., a ROM + a floppy bundle + a mass storage image).
- Each Payload has a Manifest specifying required and optional emulated hardware.
- Hard requirements declared in a Manifest cannot be overridden by the user.
- Soft defaults declared in a Manifest may be overridden by the user.
- The Manifest has an authored, read-only section provided by the publisher.
- The Manifest has a mutable user data section stored in Persistent Storage.
- Assets included in a Collection or Payload are consumed by the Menu and do not affect runtime behavior.
- A Collection may declare direct boot behavior in its Manifest, causing the cartridge to boot directly into the specified Payload without showing the Menu.
- A Collection may declare menu-first boot behavior in its Manifest, causing the Menu to always appear before any Payload launches.
- A Collection may declare in its Manifest that it handles its own first-run configuration via the JLPiCart API, suppressing automatic Menu intervention.

### Payload Sources

The MSX bus operates in real time — the RP2350 has nanoseconds to respond to memory reads. Direct mapping from USB, CD, or network is not possible in the general case. The cartridge uses a RAM sliding window cache as the default runtime mechanism: Payload content is loaded on demand, and the MSX WAIT line is asserted on a cache miss while the next region is fetched from the source.
- The default storage mode is a RAM sliding window cache filled on demand from the Source (typical target: 64KB; size is tunable).
- The MSX WAIT line is asserted on a cache miss while the cache is refilled from the Source.
- The RAM cache mode requires no upfront copy before the MSX starts.
- A Payload Manifest may request full RAM copy mode: the entire Payload is copied to RAM before the MSX starts. No cache misses occur during play. Content is volatile and lost on power off.
- A Payload Manifest may request flash cache mode: the Payload is written to a flash cache area before the MSX starts. Persistent across power cycles. Managed automatically; evicted when space is needed.
- Permanently installed Collections are stored in flash. Explicit user or publisher action is required to install or remove them.
- The cartridge decides the final storage mode based on available resources, using the Manifest preference as a hint.
- Internal flash is the default Source. It holds permanently installed Collections and the flash cache area.
- A USB stick contains exactly one Collection.
- A USB stick may contain a full cartridge initializer (firmware + security settings + Collection) for publisher provisioning of blank cartridges.
- An optical drive (CD/DVD) contains exactly one Collection.
- An optical drive Source is read-only.
- An optical drive may contain a cartridge initializer, identical in function to a USB stick initializer.
- A network-delivered Collection is always a complete Collection with a Manifest, even if minimal.
- Network-delivered Collections are stored in flash cache or RAM before the MSX starts.
- When multiple Sources are present simultaneously, priority order is user-configurable in System Settings.
- A board definition or Collection Manifest may lock the Source priority order, preventing user modification.

### ROM Payload Properties

A ROM Payload declares its mapper type in its Manifest. The mapper is not a device — it is a property of how the ROM is addressed on the MSX bus.
- A ROM Payload declares its mapper type in its Manifest.
- The linear mapper (no banking) must be supported.
- The Konami mapper (4-bank, 8KB pages) must be supported.
- The Konami SCC mapper (Konami with SCC audio) must be supported.
- The ASCII8 mapper (8KB banking) must be supported.
- The ASCII16 mapper (16KB banking) must be supported.
- Additional mapper types must be addable without architectural changes.
- Slot configuration — which subslots carry ROM, RAM, and devices — must be fully described in the Payload Manifest.
- Slot configuration must be configurable per Payload, not hardcoded.

### Peripherals

Peripheral behavior, exposure modes (bus emulation vs monitor vs API-only), and MSX-visible mapping rules are defined in **Peripherals**. Payload Manifests request peripherals by ID and exposure mode; the cartridge validates combinations at load time and produces a clear error if the request cannot be satisfied.
### Runtime Behavior
- Boot behavior is driven by the active Collection's Manifest (direct boot or menu first).
- The Menu appears automatically only when something requires user attention and no Collection or game has declared it will handle that itself.
- If the cartridge has no Collection loaded, the Menu appears automatically.
- The user may always enter the Menu via a defined action at boot, regardless of the Collection's declared boot behavior.
- If an optical drive is connected and a disc is present at boot, a loading screen is shown while the disc is read before the Collection boots.
- If a Collection has only one Payload and declares direct boot, the Payload launches immediately without any intermediate screen.
- After a direct boot launch, the Menu remains accessible via the defined user action.
- On MSX reset, the cartridge reinitializes the active Collection's device configuration and relaunches according to its boot behavior.

### Menu
- The Menu runs on the MSX screen using the host machine's VDP and keyboard.
- On MSX1 / TMS9918, the Menu renders in text mode.
- On MSX2 / V9938, the Menu renders with enhanced graphics.
- On MSX2+ / V9958, the Menu renders at best available quality.
- The Menu provides Collection selection.
- The Menu provides Payload selection within a Collection.
- The Menu provides access to System Settings.
- The Menu provides User Profile management.
- The Menu provides publisher tools (signing, locking).
- The Menu is accessible from a running Collection via a defined key combination or hardware action.
- The Menu is always available regardless of what Collection is loaded or whether any Collection is loaded.
- The Menu is not part of any Collection and cannot be replaced or modified by a Collection.

### User Profiles and Identity
- Multiple User Profiles may exist on one cartridge.
- Each profile has a display name.
- Each profile stores a language preference.
- Each profile stores save data per Payload.
- Each profile stores high scores per Payload.
- Each profile stores extended game-specific data per Payload (ghosts, replays, etc.).
- Profiles are system-level and persist across Collection changes.
- Each profile has a local identity stored on the cartridge.
- Each profile may optionally have a cloud identity (username + password/token) for network sync.
- A User Profile holder may invite another JLPiCart user to play a Collection they do not own.
- The guest receives a temporary Collection via network for the duration of the Guest Session.
- Guest Sessions are subject to publisher-defined session count limits.
- Guest Sessions are subject to publisher-defined time limits.
- Guest Sessions are subject to publisher-defined feature restrictions.
- The cartridge acts as identity provider for games and applications.
- Via the JLPiCart API, a game receives opaque identity tokens for active players.
- A game never manages usernames, passwords, or sync logic directly.
- The cartridge supports multiple simultaneously active profiles for local multiplayer.
- Users have explicit, granular control over what data leaves the device.
- Sync consent is configurable per profile.
- Sync consent is configurable per data type (saves, high scores, ghosts, profile data).
- All profile data is stored locally on the cartridge.
- Network sync is optional and additive. The cartridge is fully functional offline.

### Connectivity

Connectivity is provided by the **Networking peripheral** (see Peripherals → Networking). This section captures user-facing policy and configuration expectations.

- WiFi connectivity is provided by the ESP32 co-processor via AT command interface.
- WiFi credentials are stored in System Settings and are cartridge-wide.
- Per-Collection network access policy is declared in the Collection Manifest and may be further restricted by the user.
- The user may disable network access globally.
- Network-facing game features (high scores, multiplayer session setup, ghost sync, collection updates) are exposed via the JLPiCart API and are subject to the Security and Provisioning contracts.
### USB
- The USB port operates in host mode at all times during normal operation.
- USB HID gamepad and joystick devices are supported and mapped to MSX joystick ports.
- Controller mapping (USB HID to MSX joystick) is a System Setting.
- USB HID keyboard devices are supported and mapped to the MSX keyboard matrix.
- Keyboard mapping is a System Setting.
- USB sticks are a Payload Source, detected at boot via USB host.
- USB sticks may be exposed as Nextor volumes.
- USB optical drives (CD/DVD) are a Payload Source, detected at boot via USB host.
- Collections are loaded onto the cartridge via USB stick or network only.
- USB device mode is used only during initial firmware flashing via the RP2350B bootrom. It is not a user-facing feature.
- TinyUSB is the USB stack.
- The existing TinyUSB patch enabling optical drive (bulk-only transport) support must be maintained.

### System Configuration

**System Settings** (cartridge-wide, user-owned):
- System Settings store WiFi network credentials.
- System Settings store the video output mode (CRT / VGA).
- System Settings store language and locale.
- System Settings store controller mapping (USB HID to MSX joystick/keyboard).
- System Settings store the global network sync enable/disable flag.
- System Settings store the Payload Source priority order.
- System Settings store firmware update preferences.

**Payload Manifest** (per Payload, authored by publisher):
- The Manifest declares the required mapper type.
- The Manifest declares the required RAM amount and mapping.
- The Manifest declares the required audio devices (PSG, SCC, OPL4).
- The Manifest declares the required IO devices (floppy controller, Nextor, network).
- The Manifest declares VDP requirements.
- The Manifest declares network access requirements.
- The Manifest declares a preferred storage mode.

**User Overrides** (per Payload, per User Profile):
- The user may override audio device selection (internal emulation vs. system hardware) for soft Manifest defaults.
- The user may override network access enable/disable for a specific Payload.
- The user may override the video output mode for a specific Payload.

**Configuration Layering**:
- System Settings provide the base defaults.
- Payload Manifest requirements are applied on top of System Settings. Hard Manifest requirements cannot be overridden.
- User Overrides are applied last, on top of Manifest soft defaults only.

---

## 5. Core runtime contracts

This chapter defines the contracts that make the platform predictable: how resources are declared, how launch decisions are made, and what persistence guarantees exist. These contracts are written so they can be tested with a conformance harness.

### 5.1 Resource and capability model contract (v1)

**Goal:** the platform can deterministically decide *what this unit can do* (hardware), *what this firmware can do* (software), *what is permitted* (policy), and *what will be active for a given Payload* (allocation).

This contract intentionally separates:

- **hardware reality** (PCB-dependent),
- **software availability** (what drivers/emulators are compiled in),
- **policy** (publisher/user restrictions),
- **activation** (what is actually enabled for this Payload).

**Definitions**

- **Capability id**: a stable dotted identifier for a feature (examples: `net.wifi`, `net.eth`, `storage.mass`, `storage.floppy`, `audio.opl4`, `video.v9990`, `io.rs232`, `ui.eink`).
- **Peripheral**: an implementation module that can provide one or more capability ids.
- **Capability descriptor**: a declarative record describing a candidate capability and how it may be activated.
- **Capability Registry**: the authoritative runtime table of capabilities with status and metadata.
- **Resource**: a consumable budget that constrains activation (SRAM, flash-cache bytes, CPU budget, PIO SMs, DMA channels, MSX pages, I/O port ranges, subslots, etc.).
- **Exposure interface**: how a peripheral is exposed when active: `msx_emulation`, `msx_monitor`, or `api_only`.
- **Launch Plan**: the computed, deterministic allocation describing which peripherals are active and how they are mapped.

**Hardware vs software capabilities**

The registry distinguishes *where a capability comes from*:

- **Hardware capability (hw)**: depends on the PCB (chips, connectors, wiring). Hardware capabilities are **declared** by the Board Definition and may optionally be **verified** by a safe probe.
- **Software capability (sw)**: depends on the firmware image (emulators, services). Software capabilities are **declared** by the firmware’s Build Descriptor and are **activated** only if the allocator can satisfy their resource/mapping requests for the current Payload.

A capability id may exist in both forms on different boards (for example, `io.rs232` could be a physical UART transceiver on one PCB, and an emulated UART-over-API service on another). The origin (`hw` vs `sw`) is metadata, not part of the capability id.

**Capability lifecycle: Declared → Allowed → Activated**

Every capability candidate flows through the same three-stage pipeline:

- **Declared**: the capability is a *candidate* on this unit because it appears in the Board Definition (hw) or Build Descriptor (sw).
- **Allowed**: the capability is not masked by signed policy (and the requested exposure interface is permitted).
- **Activated**: the capability is actually enabled for this run.

Activation gates differ by origin:

- **hw activation gate = verification** (optional): if the descriptor says verification is safe, the firmware MAY run a *non-invasive* probe to confirm presence/health. If verification fails, the capability is not activated.
- **sw activation gate = allocation**: the allocator must be able to satisfy required resources and MSX-visible mappings (if any) for the current Payload. If allocation fails, the capability is not activated.

The platform MUST NOT perform “discovery probing” outside this pipeline. In particular:

- A peripheral MUST NOT probe by toggling pins that may be shared with the MSX bus or other peripherals.
- A peripheral MUST NOT probe hardware that is not both Declared and Allowed.
- If a capability has `verify: none`, the platform MUST treat it as “declared present” (board-trust) and MUST NOT attempt probing.

**Board Definition (hardware declaration)**

A Board Definition MUST declare:

- a stable `board_id` and `board_rev`,
- hw capability descriptors (what the PCB intends to have),
- which probes are safe (`verify: none | safe_probe`),
- fixed constraints and reserved resources (e.g., MSX bus core reservation, fixed PIO usage, fixed flash partitioning),
- any hard-fixed MSX mappings that cannot move (if any).

**Build Descriptor (software declaration)**

A firmware image MUST carry a Build Descriptor that declares:

- sw capability descriptors for each compiled-in peripheral/emulator/service,
- the supported exposure interfaces per capability (`msx_emulation`, `msx_monitor`, `api_only`),
- required resource/mapping requests *if activated*.

This is the mechanism that replaces scattered `#define` feature flags: if a peripheral is compiled in, it appears in the descriptor table; if not compiled in, it does not.

**Policy (signed configuration)**

Signed policy MUST be able to:

- mask capability ids or groups (e.g., disable all `net.*`),
- restrict exposure interfaces (e.g., allow `api_only` but forbid `msx_emulation` for a capability),
- define default enablement rules (what becomes “requested” absent a Payload requirement).

Policy MUST NOT invent capabilities that are not Declared.

**Payload Manifest (activation requests)**

A Payload Manifest MUST declare:

- hard required capability ids,
- optional/preferred capability ids,
- any hard mapping constraints that the Payload truly depends on.

A Payload MAY request software peripherals (e.g., “needs `audio.opl4` emulation”) and may request hw peripherals (e.g., “requires `net.eth` if available”), but the Launch Plan is authoritative.

**Deterministic algorithm (normative)**

Given a Board Definition, Build Descriptor, signed policy, and Payload Manifest, the platform MUST compute the Launch Plan as follows:

1. **Declared set**: build the candidate list by unioning:
   - Board hw capability descriptors, and
   - Build sw capability descriptors.
2. **Allowed set**: apply policy masks and interface restrictions.
3. **Requested set**:
   - add any hard requirements from the Payload,
   - add policy defaults (system-wide enablement),
   - add user-selected toggles (if policy permits).
4. **Activate hardware candidates** (requested ∩ hw):
   - if `verify: safe_probe`, run probe; activate only on success,
   - if `verify: none`, activate without probing.
5. **Activate software candidates** (requested ∩ sw):
   - run allocation; activate only if all required resources/mappings can be reserved.
6. **Validate hard requirements**:
   - if any hard required capability is not Activated, the Launch Plan MUST fail with a structured reason.
7. **Finalize mappings deterministically**:
   - exclusive mappings (ports/pages/subslots) MUST be resolved by a deterministic priority rule (policy > payload hard constraints > peripheral preferred profile),
   - any remaining conflicts MUST fail.

Ordering MUST be stable. When iterating candidates, the platform MUST sort by `(capability_id, instance_id)` where `instance_id` is a stable string from the descriptor (e.g., `eth0`, `wifi0`, `opl4_emu0`).

**Failure reasons**

If launch fails, the platform MUST return a structured failure reason including:

- `reason_kind`: `MISSING_CAPABILITY`, `POLICY_DISABLED`, `VERIFY_FAILED`, `ALLOC_FAILED`, `MAPPING_CONFLICT`, `UNSUPPORTED_INTERFACE`,
- the `capability_id` and `instance_id` involved,
- (when possible) one actionable remediation.

The platform MUST NOT silently partially enable a capability.


**Resource classes**

The allocator distinguishes:

- **Exclusive** resources: can only be assigned to one owner at a time (I/O port ranges, MSX pages/subslots, “single-owner” DMA usage, etc.).
- **Quantifiable** resources: allocated from a budget (SRAM bytes, flash-cache bytes, queue sizes).
- **Shareable** resources: may be shared only under explicitly defined rules (for example, a single network stack shared across API clients).

Where an Exclusive resource would be overlapped by two Activated peripherals, the Launch Plan MUST fail unless the conflict can be resolved deterministically by selecting a non-overlapping legacy profile that the peripheral explicitly supports.



### 5.2 Launch workflow contract (v1)

**Goal:** launching a Payload is predictable, safe, and power-loss resilient, while allowing multiple active Payload types (ROM + floppy bundle + mass storage, etc.).

A launch attempt MUST follow these phases:

- **Preflight**: resolve the active Collection, select Payload(s), compute Launch Plan, validate resources.
- **Staging**: prepare required storage mode (fill RAM cache lazily, copy full RAM image, or populate flash cache), and prepare any emulated device images.
- **Activation**: switch MSX-visible mappings atomically from “menu/system” to “payload runtime”, then release the MSX reset/WAIT strategy as required.
- **Run**: provide runtime services (API window, device emulation, source I/O).
- **Exit**: on user exit or reset, persist mutable data, clear transient credentials, and return to Menu according to Collection policy.

If staging requires a long operation (e.g., flash-cache population), the platform MUST provide progress feedback in the Menu environment and MUST remain recoverable from power loss.

### 5.3 Persistence contract (v1)

The platform MUST categorize persisted data and apply consistent rules:

- **System Settings**: device-wide, survive Collection changes, include WiFi credentials, pairing tokens, and global preferences.
- **User Profiles**: per-user configuration, achievements, play history, optional cloud identity bindings.
- **Saves**: per Payload, per user (unless publisher explicitly declares “shared saves”), with defined size limits and migration rules.
- **Extended Game Data**: optional per Payload data (ghosts, replays, custom content) separated from saves.
- **Caches**: evictable and rebuildable data (flash cache, thumbnails, downloaded metadata).
- **Logs**: diagnostic event logs, never required for correct operation.

All critical writes (System Settings, Profiles, Saves) MUST be power-loss safe. The persistence layer MUST provide atomic update semantics (e.g., write-new + verify + atomic rename, or journaling).

A factory reset MUST be able to delete user data and caches without deleting a permanently installed Collection, unless the user explicitly selects “full wipe”.

## 6. Content and configuration specifications

This chapter specifies the portable representation of Collections and Payloads, and the rules for configuration layering and signing.

### 6.1 Collection format contract (v1)

This contract defines the portable, source-agnostic representation of a **Collection** on media and in storage. The goal is that a Collection can be copied between Sources (internal flash, USB, optical disc, network download) without changing its internal structure or references.

### Purpose

A Collection bundle MUST be self-contained and deterministic:

- The bundle MUST include a single **Collection Manifest** (`manifest.json`) that describes the bundle identity, version, and payload entries.
- All file references in the manifest MUST be relative paths within the bundle.
- The bundle MAY be signed (and optionally encrypted at rest once installed), but the on-media structure remains the same.

### Bundle root and required files

A Collection is a directory tree called a **bundle**.

The bundle root MUST contain:

- `manifest.json` — UTF‑8 JSON, conforming to `jlpicart.collection.v1`.

The bundle root MAY contain:

- `bundle.sig` — a signature envelope descriptor for the bundle (see *Manifest and configuration schema contract (v1)*).
- `assets/` — optional user-interface assets (icons, screenshots, banners, localized strings).
- `payloads/` — recommended location for payload content files (ROMs, disks, binaries, data blobs).
- `docs/` — optional documentation for humans.

The device MUST ignore unknown files and directories, except where a signature envelope explicitly enumerates the files that must be present and hashed.

### Paths and naming rules

All paths used inside manifests (e.g., `payloads[].content.path`) MUST follow these rules:

- Paths MUST be relative to the bundle root.
- Paths MUST use `/` as a separator.
- Paths MUST NOT start with `/` and MUST NOT contain `..` segments.
- Paths MUST be valid UTF‑8 and MUST NOT contain NUL bytes.
- Publishers SHOULD avoid relying on case-only differences in filenames (for FAT/ISO9660 compatibility).
- Publishers SHOULD keep filenames and directory names within a conservative portable subset (ASCII letters/digits plus `-`, `_`, `.`, `/`), and SHOULD keep paths shorter than 200 bytes.

### Representation across Sources

The Collection format is source-agnostic. A Source MAY store the bundle as:

- an extracted directory tree (typical for internal flash storage), or
- a transport container (zip/tar/other) that expands into the exact same directory tree before launch (typical for network download or installer bundles).

Regardless of transport, the device MUST treat the effective bundle root as the canonical reference root for `manifest.json` and all `path` fields.

#### USB and optical media

A *simple content* USB stick / disc MUST contain exactly one Collection bundle.

The device MUST support the following content layouts:

- **Root bundle layout**: the filesystem root is the Collection bundle root (i.e., `manifest.json` is at the root).
- **Initializer layout**: the media contains `/JLPICART/INSTALL/...` install intents (see *Update, initialization, and provisioning workflow contract*). In this layout, the Collection bundle is carried as `collection.bundle` (transport container) and installed into internal storage before use.

The device MAY support additional convenience layouts (e.g., a top-level `collection/` folder), but such layouts are not required by this contract.

#### Network delivery

A network-delivered Collection MUST be complete (it MUST include a valid `manifest.json`). The device MAY download the bundle as a transport container, but MUST validate signatures (if present/required) and MUST materialize the bundle into RAM and/or flash cache before the MSX starts executing payload code.

### Identity and versioning

A Collection’s identity is defined by:

- `manifest.json.schema` (currently `jlpicart.collection.v1`)
- `manifest.json.id` (stable publisher-chosen identifier)
- `manifest.json.version` (publisher-chosen version string; semantic versioning is recommended)

Install/update behavior:

- Installing a Collection with the same `id` and a higher `version` SHOULD be treated as an update.
- If the device enforces signed Collections, the update MUST verify under the same Publisher Identity chain and policy as the installed version (unless an explicit key-rotation / revocation policy applies).

### Determinism requirements

To support reproducible verification and reliable installs:

- If `bundle.sig` is present, the device MUST verify the signature and the declared file hashes before installing or launching.
- The bundle MUST NOT rely on file timestamps for behavior.
- The device MUST treat `manifest.json` as the single source of truth for payload enumeration and behavior (directory scanning is not allowed to change semantics).

### 6.2 Manifest and configuration schema contract (v1)

### Purpose

This contract defines:

- the normative schema for Collection and Payload manifests,
- the override/precedence rules that compute “effective configuration” at runtime,
- and the canonicalization/signing rules required for sealed units.

It is intentionally conservative: schema evolution happens by adding fields (minor) and introducing new schema versions (major), never by changing existing meaning.

### File roles

A complete JLPiCart installation uses four configuration layers:

1. **Board Definition** (immutable per hardware build; compiled into firmware or installed as board pack)
2. **System Settings** (device-wide; mutable; power-loss safe)
3. **Collection Manifest** (signed content; immutable on sealed units)
4. **User/Profile Overrides** (per profile; mutable; power-loss safe)

A Payload Manifest is embedded within the Collection Manifest (or referenced as a separate signed file inside the Collection bundle).

### Canonicalization and signing

Human-friendly authoring formats are allowed, but signatures require a canonical byte representation.

Rules:

- Signed artifacts MUST be verified against **canonical UTF‑8 JSON** bytes.
- Canonicalization MUST be deterministic. The recommended canonicalization is **RFC 8785 JSON Canonicalization Scheme (JCS)**.
- The signature MUST cover the canonical bytes of the manifest and all referenced payload bytes.

Authoring tools MAY accept YAML/TOML, but MUST compile them into canonical JSON before signing.

### Bundle signature envelope (minimal v1)

A signed bundle MUST include a `bundle.sig` descriptor:

```json
{
  "sig_schema": "jlpicart.signature.v1",
  "alg": "ecdsa_secp256k1_sha256",
  "key_id": "publisher-key-2026-01",
  "files": [
    {"path": "manifest.json", "sha256": "…"},
    {"path": "payloads/game.rom", "sha256": "…"}
  ],
  "manifest_path": "manifest.json",
  "signature": "BASE64(…)"
}
```

Verification behavior:

- The device MUST hash each listed file and compare to `sha256`.
- The device MUST verify the signature over the canonical JSON bytes of the `bundle.sig` object with `signature` removed (or set to empty), plus the canonical bytes of the referenced manifest. (This keeps the envelope extensible without re-defining “what is signed” each time.)
- Devices MUST reject bundles that fail verification whenever policy requires signature verification.

### IDs and versioning

IDs MUST be stable, globally unique strings using reverse-DNS style.

- Collection `id`: `com.publisher.collection_name`
- Payload `id`: unique within a Collection
- Publisher `id`: `com.publisher`

Versions MUST be semantic versions `"MAJOR.MINOR.PATCH"` as strings.

Schema versioning:

- Each manifest has a `schema` string such as `jlpicart.collection.v1`.
- A new schema version is introduced when backward compatibility cannot be preserved.

### Override classes

Every configuration field is one of:

- **hard**: cannot be overridden by users or system policy (only by publishing a new signed bundle)
- **default**: can be overridden by user/system settings if allowed
- **policy**: can be restricted by system policy (e.g., “network disabled globally”)
- **local**: never signed; stored only in System Settings or Profile Overrides

The manifest schema MUST clearly label which fields are hard vs default vs policy.

### Effective configuration algorithm (normative)

To compute effective configuration for a Payload launch:

1. Load Board Definition defaults.
2. Merge System Settings (policy and device-wide preferences).
3. Merge Collection Manifest defaults.
4. Merge Payload Manifest defaults.
5. Apply policy restrictions (system may clamp or deny features).
6. Apply Profile Overrides (only for fields marked default/local).
7. Produce a Launch Plan; if requirements conflict after merges, refuse to launch with a structured reason.

Merge rules:

- Objects merge by key.
- Arrays replace entirely unless explicitly documented as “append”.
- Fields marked **hard** MUST NOT be changed by merges from later layers.

### Board Definition schema (jlpicart.board.v1)

The Board Definition describes what the hardware build can do and which fixed mappings exist.

Minimal shape:

```json
{
  "schema": "jlpicart.board.v1",
  "id": "jlpicart.ref.rp2350b.esp32",
  "hw": {
    "mcu": "rp2350b",
    "wifi": "esp32-at",
    "flash_mib": 16,
    "psram_kib": 0
  },
  "capabilities": [
    {"id": "wifi", "param": 1},
    {"id": "mass_storage", "param": 1}
  ],
  "resources": {
    "sram_kib_total": 520,
    "sram_kib_reserved": 160,
    "flash_cache_kib_max": 8192
  },
  "mappings": {
    "menu_page_preferred": "page1",
    "api_window_preferred": "page2",
    "io_ports_optional": {"doorbell": [0,0]}
  },
  "legacy": {"unapi": true, "symbos": false}
}
```

### Collection Manifest schema (jlpicart.collection.v1)

The Collection Manifest is the signed top-level description of a distributable bundle.

Required fields:

- `schema` (must be `jlpicart.collection.v1`)
- `id`, `version`
- `publisher` object with `id`, `name`
- `payloads` array (at least one)

Optional fields:

- `title`, `description`, `icon`
- `boot` policy (menu-first, default payload)
- `defaults` (collection-wide defaults applied to payloads)
- `services` configuration (leaderboard endpoints, etc.)

Normative minimal shape:

```json
{
  "schema": "jlpicart.collection.v1",
  "id": "com.example.collection1",
  "version": "1.0.0",
  "publisher": {
    "id": "com.example",
    "name": "Example Studio",
    "support": {"url": "…"}
  },
  "boot": {
    "mode": "menu_first",
    "default_payload": "game"
  },
  "defaults": {
    "network": {"allowed": true},
    "storage": {"mode": "ram_window"}
  },
  "payloads": [
    {
      "id": "game",
      "type": "rom",
      "content": {"path": "payloads/game.rom", "mapper": "konami_scc"},
      "requirements": {"capabilities": ["scc"], "ram_kib": 128},
      "defaults": {"network": {"allowed": true}},
      "stats": {"enabled": true, "leaderboards": [{"id":"main"}]}
    }
  ]
}
```

### Payload Manifest (embedded) fields

A payload entry in `payloads[]` must include:

- `id` (string)
- `type` (`rom`, `disk`, `mixed`, `app`, etc.)
- `content` describing files and mapper/device bindings
- `requirements` describing required capabilities and resources

`requirements` v1 fields (minimal):

- `capabilities`: string list (hard)
- `ram_kib`: integer (hard)
- `network`: object with `allowed` (default/policy) and `required` (hard)
- `storage`: object with `needs_save` (default) and `max_save_kib` (hard)
- `devices`: optional list of device instances (hard). Each device instance is an object with:
  - `id` (peripheral id),
  - `interface` (`msx_emulation` | `msx_monitor` | `api_only`, optional if a default exists),
  - `legacy_profile` (optional, for `msx_emulation`),
  - `optional` (boolean; default false),
  - `params` (optional small configuration object).

`defaults` v1 fields (minimal):

- `storage.mode`: `"ram_window" | "ram_full" | "flash_cache"`
- `network.allowed`: boolean
- `ui.language`: string

### System Settings schema (jlpicart.system.v1)

System Settings are local, mutable, and never signed.

They MUST include:

- WiFi credentials (if present)
- global policy toggles: `network_enabled`, `guest_allowed`
- cache limits and housekeeping settings
- trusted key sets (if the device is provisioned/sealed)

System Settings MUST be stored with power-loss safe atomicity.

### Profile Overrides schema (jlpicart.profile.v1)

Profiles store:

- user name/alias
- save ownership and per-title preferences
- achievements/stats local cache
- optional cloud binding tokens

Profile overrides MUST be restricted to fields documented as overrideable.

### Validation rules (normative)

A conforming implementation MUST validate:

- schema string matches known schema version,
- required fields exist and types match,
- IDs are well-formed strings and do not exceed reasonable limits,
- referenced files exist in the bundle and match hashes,
- override attempts do not change hard fields,
- effective configuration fits board resources.

If validation fails, the platform MUST produce a structured, user-visible reason.



### 6.3 Machine-readable schemas (draft)

This section provides **draft JSON Schema** documents for the key files. These schemas are intended to be extracted into standalone `.schema.json` files in the repository, but are included here so the contract is implementable from this book alone.

The canonical signing form for JSON is **RFC 8785 (JCS)**. When this book says “sign `manifest.json`”, it means: parse JSON → canonicalize using JCS → hash → sign the hash. (See RFC 8785.)

#### 6.3.1 Collection manifest JSON Schema (draft)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://jlpicart.dev/schema/collection-manifest.v1.json",
  "title": "JLPiCart Collection Manifest (v1)",
  "type": "object",
  "required": ["format_version", "collection_id", "version", "payloads"],
  "properties": {
    "format_version": { "type": "string", "const": "1.0" },
    "collection_id": { "type": "string", "minLength": 1, "maxLength": 64 },
    "version": { "type": "string", "minLength": 1, "maxLength": 32 },
    "publisher": {
      "type": "object",
      "required": ["publisher_id", "name"],
      "properties": {
        "publisher_id": { "type": "string", "minLength": 1, "maxLength": 64 },
        "name": { "type": "string", "minLength": 1, "maxLength": 128 }
      },
      "additionalProperties": true
    },
    "title": { "type": "string", "minLength": 1, "maxLength": 128 },
    "description": { "type": "string", "maxLength": 4096 },
    "assets": {
      "type": "object",
      "properties": {
        "cover_image": { "type": "string" },
        "screenshots": { "type": "array", "items": { "type": "string" } }
      },
      "additionalProperties": true
    },
    "boot": {
      "type": "object",
      "properties": {
        "mode": { "type": "string", "enum": ["menu_first", "direct"] },
        "payload_id": { "type": "string" }
      },
      "additionalProperties": false
    },
    "payloads": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["payload_id", "path"],
        "properties": {
          "payload_id": { "type": "string", "minLength": 1, "maxLength": 64 },
          "path": { "type": "string", "minLength": 1 },
          "title": { "type": "string", "minLength": 1, "maxLength": 128 }
        },
        "additionalProperties": true
      }
    }
  },
  "additionalProperties": true
}
```

#### 6.3.2 Payload manifest JSON Schema (draft)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://jlpicart.dev/schema/payload-manifest.v1.json",
  "title": "JLPiCart Payload Manifest (v1)",
  "type": "object",
  "required": ["format_version", "payload_id", "type", "requirements"],
  "properties": {
    "format_version": { "type": "string", "const": "1.0" },
    "payload_id": { "type": "string", "minLength": 1, "maxLength": 64 },
    "type": { "type": "string", "enum": ["rom", "disk", "bundle", "mixed"] },
    "entry": { "type": "string" },
    "requirements": {
      "type": "object",
      "properties": {
        "msx_gen_min": { "type": "integer", "minimum": 1, "maximum": 4 },
        "ram_kb_min": { "type": "integer", "minimum": 64 },
        "vram_kb_min": { "type": "integer", "minimum": 16 },
        "peripherals": {
          "type": "array",
          "items": {
            "type": "object",
            "required": ["name", "mode"],
            "properties": {
              "name": { "type": "string", "minLength": 1 },
              "mode": { "type": "string", "enum": ["required", "preferred", "disabled"] },
              "interface": { "type": "string", "enum": ["emulation", "monitor", "api_only"] },
              "legacy_profile": { "type": "string" }
            },
            "additionalProperties": true
          }
        }
      },
      "additionalProperties": true
    },
    "defaults": { "type": "object" }
  },
  "additionalProperties": true
}
```

#### 6.3.3 Peripheral descriptor JSON Schema (draft)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "https://jlpicart.dev/schema/peripheral-descriptor.v1.json",
  "title": "JLPiCart Peripheral Descriptor (v1)",
  "type": "object",
  "required": ["name", "version", "interfaces"],
  "properties": {
    "name": { "type": "string", "minLength": 1, "maxLength": 64 },
    "version": { "type": "string", "minLength": 1, "maxLength": 32 },
    "interfaces": {
      "type": "array",
      "minItems": 1,
      "items": {
        "type": "object",
        "required": ["type"],
        "properties": {
          "type": { "type": "string", "enum": ["emulation", "monitor", "api_only"] },
          "legacy_profile": { "type": "string" },
          "ports": { "type": "array", "items": { "type": "string" } },
          "memory_ranges": { "type": "array", "items": { "type": "string" } },
          "notes": { "type": "string", "maxLength": 2048 }
        },
        "additionalProperties": true
      }
    }
  },
  "additionalProperties": true
}
```

**References (canonical JSON)**

- RFC 8785 (JSON Canonicalization Scheme): https://www.rfc-editor.org/rfc/rfc8785
## 7. JLPiCart developer APIs

The JLPiCart API is the preferred interface for new software: it avoids MSX I/O port collisions and supports richer semantics (structured requests, versioning, and capability discovery). Legacy emulation interfaces remain available for compatibility, but API‑first titles SHOULD use this chapter’s contracts as their primary integration surface.

### 7.1 JLPiCart API service contract (v1)

**Goal:** provide a stable, versioned, high-performance API between Z80 software and the cartridge firmware that:

- avoids global I/O port conflicts by default,
- is discoverable from MSX-DOS / SymbOS contexts,
- supports both “game loop” usage (fast polling, bulk transfer) and “OS/app” usage (blocking calls, cancellation).

This contract intentionally draws from the MSX-UNAPI approach for *discovery and call routing* while keeping the primary data plane memory-mapped for performance.

#### Discovery and presence

The cartridge MUST support at least one discovery mechanism:

- **Slot scanning**: software can detect the API window by scanning slot pages for a fixed signature. This is simple but less friendly from MSX-DOS contexts.
- **EXTBIO discovery (preferred)**: provide an EXTBIO call gate compatible with the MSX extended BIOS mechanism used by UNAPI, exposing an implementation identifier (proposed: `JLPICART`) and returning a call entry point plus location info.

Using EXTBIO keeps the API usable even when code is not running from the cartridge slot, and mirrors the “discover implementations + call a single entry point with routine index in A” model described by MSX‑UNAPI.

#### Primary data plane: memory-mapped API window

The primary API surface is a **memory-mapped API window** exposed in the cartridge address space.

- The window MUST be **16KB** for API v1.
- The window MUST contain a header with:
  - a fixed signature (`"JLP1"`),
  - API semantic version (major/minor),
  - window size and layout version,
  - offsets to rings/buffers,
  - feature flags (which optional services are present).
- The window MUST be readable without side effects. Writes MUST only affect explicitly defined registers/queues.

**Why memory-mapped:** MSX I/O ports are globally shared and frequently collide; many devices decode only 8 address bits, so “16-bit I/O ports” are not reliably isolated. The design therefore treats I/O ports as an optional fast-path only.

#### Doorbells and wakeups

The v1 API provides doorbells in the **memory-mapped** window via `ApiRegs` (see the on-wire specification below). This avoids global I/O port collisions and is sufficient for both polling and “soft wakeup” patterns.

Boards MAY additionally expose one or more **optional I/O ports** that *mirror* the doorbell semantics (e.g., write-to-kick / read-status). If present:

- the ports MUST be configurable per board variant,
- they MUST be advertised in board documentation (and ideally via a capability flag),
- and software MUST NOT require them (memory doorbells are the baseline).
#### Message model

The API is message-based: the host submits requests; the cartridge returns responses.

The API window MUST provide:

- a **host→cart request ring**,
- a **cart→host response ring**,
- a small **scratch area** for bulk payload transfer (optional “zero-copy” region).

Each message MUST include:

- `seq` (monotonic request id),
- `service` and `method`,
- `flags` (e.g., “blocking allowed”, “best effort”, “idempotent”),
- `status` (responses only),
- a length-delimited payload.

The protocol MUST support:

- non-blocking polling,
- timeouts and cancellation,
- a bounded-memory behavior (rings must not grow without limit).

#### Standard service set (v0.1 draft)

This document treats the API as a set of services. Each service may be absent if the board/policy does not support it; absence MUST be discoverable via feature flags.

- **System**: get API version, get device id, get capabilities/resources, read diagnostics, request soft reset.
- **Storage**: enumerate “save slots”, read/write save blobs, atomic commit/rollback, integrity check.
- **Network**: create TCP/UDP sockets (or higher-level HTTP-ish calls), DNS, connect state, rate limiting.
- **Identity**: enumerate profiles, guest mode rules, login tokens, “active profile” selection.
- **UserStats**: stats/achievements/leaderboards (see next contract).
- **Update**: query update channels, install signed bundles, get update status, schedule reboot.

A v0.1 contract SHOULD focus on making these services minimal and composable, rather than baking in “one true workflow” per game.

#### Versioning rules

- Major version changes MAY break compatibility.
- Minor version changes MUST be backward compatible (new methods/features, never changing existing semantics).
- The header MUST expose both a “protocol layout version” and an “API semantic version”.

#### Error model

All responses MUST include a standardized error code namespace, with a stable mapping to human-readable diagnostics. Errors MUST include “capability not present” and “policy denied” as distinct cases.

---

### 7.2 JLPiCart API on‑wire specification (v1)

### Purpose

The JLPiCart API is the primary Z80↔cartridge interface for modern JLPiCart-targeted software. It is designed to be:

- fast on real Z80 hardware,
- stable and versioned,
- free of global I/O port conflicts by default (memory-mapped first),
- capable of both “polling in a game loop” and “blocking in an OS/app”.

This section fully specifies the **memory window layout** and **message framing** (“on-wire”) for v1.

### Address space and mapping

During Payload runtime, the cartridge MUST expose a **JLPiCart API Window** as a read/write memory-mapped page.

- The window size MUST be 16KB for v1.
- The window mapping location (0x0000–0x3FFF / 0x4000–0x7FFF / 0x8000–0xBFFF / 0xC000–0xFFFF) is chosen by the Launch Plan.
- The window MUST be discoverable either by slot scanning for the signature or by a documented discovery helper (optional).

### Endianness

All multi-byte fields in the API Window are **little-endian**.

### Fixed layout within the 16KB API Window

Offsets are relative to the base of the 16KB window.

- `0x0000..0x003F`  ApiWindowHeader (64 bytes)
- `0x0040..0x005F`  ApiRegs (32 bytes)
- `0x0060..`        Rings and scratch areas as described by the header

The header provides exact offsets and sizes so future versions can rearrange internals without breaking discovery.

### ApiWindowHeader (64 bytes)

```c
struct ApiWindowHeader {
  char     sig[4];          // "JLP1"
  uint8_t  api_major;       // MUST be 1 for this spec
  uint8_t  api_minor;       // backward compatible increments
  uint8_t  layout_ver;      // MUST be 1 for this framing
  uint8_t  flags;           // bitfield (see below)
  uint16_t win_size;        // MUST be 0x4000
  uint16_t regs_ofs;        // MUST be 0x0040
  uint16_t req_ring_ofs;    // offset to RingHeader + data
  uint16_t req_ring_len;    // total bytes (header + ring data)
  uint16_t rsp_ring_ofs;
  uint16_t rsp_ring_len;
  uint16_t h2c_scratch_ofs; // host->cart scratch buffer
  uint16_t h2c_scratch_len;
  uint16_t c2h_scratch_ofs; // cart->host scratch buffer
  uint16_t c2h_scratch_len;
  uint32_t feature_bits;    // services/features present
  uint16_t max_frame;       // maximum frame bytes (including length prefix)
  uint16_t reserved0;
  uint32_t reserved1;       // MUST be zero in v1
};
```

Header `flags`:

- bit 0: rings enabled (MUST be 1)
- bit 1: ApiRegs doorbells enabled (MUST be 1 for v1)
- other bits reserved (MUST be 0)

`feature_bits` is a bitmap that advertises which **services** are implemented. The mapping of bits to services is defined below.

### ApiRegs (32 bytes)

ApiRegs provides simple doorbells and observability without consuming I/O ports.

```c
struct ApiRegs {
  volatile uint8_t host_kick;     // host increments to notify "requests posted"
  volatile uint8_t cart_event;    // cart increments to notify "responses posted"
  volatile uint8_t host_flags;    // reserved for future (MUST write 0)
  volatile uint8_t cart_flags;    // bit0=error_latched, bit1=busy (best-effort)
  volatile uint16_t last_err;     // last error code (best-effort)
  volatile uint16_t last_seq;     // seq associated with last_err (best-effort)
  volatile uint8_t  reserved[24]; // MUST be zero in v1
};
```

Behavior:

- The host MUST post one or more request frames into the request ring, then increment `host_kick` (wrap allowed).
- The cart MUST post one or more response frames into the response ring, then increment `cart_event`.
- The protocol does not require doorbells for correctness; they are hints to reduce polling.

### Ring layout and invariants

At `req_ring_ofs` and `rsp_ring_ofs` the window contains a `RingHeader` followed by the ring data bytes.

```c
struct RingHeader {
  volatile uint16_t head;    // producer writes (byte offset into ring_data)
  volatile uint16_t tail;    // consumer writes (byte offset into ring_data)
  uint16_t size;             // ring_data size in bytes (power of two)
  uint16_t flags;            // reserved (MUST be zero in v1)
  // uint8_t ring_data[size] follows
};
```

- `head` and `tail` are offsets in `[0, size)`, relative to the start of `ring_data`.
- The ring is empty when `head == tail`.
- The ring is full when there is insufficient space to write the next frame (producer must detect and return `E_RING_FULL` to the caller).

#### Frame encoding in the ring

The ring is a sequence of frames. Each frame begins with a 16-bit length prefix:

- `frame_len` is the total number of bytes in the frame including the length prefix.
- A `frame_len` value of `0` is a **wrap marker**: it indicates the producer padded to the end and the next frame begins at offset `0`.

Frame structure:

```text
u16 frame_len
u8  msg_bytes[frame_len - 2]
```

If the producer is at an offset where fewer than 2 bytes remain to end-of-ring, it MUST wrap by setting `head=0` without writing a marker.

#### Writing a frame (producer algorithm)

A producer MUST:

1. compute `needed = frame_len` (including the 2-byte prefix),
2. compute free space from current `head` and observed `tail`,
3. if insufficient, fail the host call with `E_RING_FULL`,
4. if `needed` does not fit contiguously to end-of-ring:
   - if there is room for a 2-byte marker, write `frame_len=0` at current head and advance head to 0,
   - then write the frame at head 0.
5. write the entire frame bytes (prefix, header, payload),
6. update `head` last.

A consumer MUST treat a `frame_len=0` marker as “wrap to 0” and continue.

### Message header (MsgHeader, 16 bytes)

The first 16 bytes of `msg_bytes` are a fixed header.

```c
struct MsgHeader {
  uint16_t seq;         // host-chosen request id
  uint8_t  service;     // service id
  uint8_t  method;      // method id within service
  uint16_t flags;       // request/response flags
  uint16_t status;      // response status; MUST be 0 in requests
  uint16_t payload_len; // bytes after this header
  uint16_t scratch_ofs; // offset within direction scratch; 0xFFFF = none
  uint16_t scratch_len; // bytes valid in scratch
  uint16_t reserved;    // MUST be 0
};
```

- In requests, `status` MUST be 0.
- In responses, `status` is `OK` or an error code and `payload_len` describes the response payload length.
- `scratch_ofs` is relative to the appropriate scratch region base for this direction, not the window base:
  - requests use host→cart scratch (`h2c_*`),
  - responses use cart→host scratch (`c2h_*`).

If `scratch_ofs != 0xFFFF`, then `scratch_ofs + scratch_len` MUST fit within the corresponding scratch buffer.

### Request/response ordering

- The host MAY pipeline multiple requests.
- The cart MUST process requests in FIFO order.
- The cart MUST emit responses in the same order as requests (v1 rule), even if internally some operations complete earlier. This keeps Z80 clients simple.
- A future version may allow out-of-order responses gated by a feature bit; v1 forbids it.

### Sequence number rules

- `seq` is chosen by the host and MUST be nonzero.
- The host SHOULD increment `seq` monotonically and wrap.
- The cart MUST echo `seq` in the response.

### Timeout and cancellation

The v1 framing includes cancellation as a method-level policy rather than a generic ring feature.

- Any method that can block MUST define how to cancel it (typically by a `CANCEL(seq)` method in that service).
- If a client does not cancel, the cart MAY return `E_TIMEOUT` for operations that exceed the method’s defined maximum time.

### Error code namespace (API status)

Status codes are 16-bit.

`0x0000 OK`

Client/request errors (`0x1xxx`):

- `0x1001 E_BAD_REQ` (malformed frame/header)
- `0x1002 E_BAD_ARG` (invalid argument values)
- `0x1003 E_UNSUPPORTED` (unknown service/method)
- `0x1004 E_DENIED` (policy denied; not authenticated; feature disabled)
- `0x1005 E_NOT_FOUND` (unknown id/handle)
- `0x1006 E_BUSY` (resource busy)
- `0x1007 E_TIMEOUT` (operation exceeded limit)
- `0x1008 E_RING_FULL` (cannot enqueue)
- `0x1009 E_TOO_BIG` (payload/scratch exceeds allowed limits)

Server/runtime errors (`0x2xxx`):

- `0x2001 E_IO` (storage/network I/O failure)
- `0x2002 E_CORRUPT` (integrity check failed)
- `0x2003 E_INTERNAL` (unexpected firmware error)
- `0x2004 E_UNAVAILABLE` (capability temporarily unavailable)

### Service identifiers and feature bits (v1)

Service IDs:

- `0x00 System`
- `0x01 Storage`
- `0x02 Network` (optional; low-level)
- `0x03 Identity`
- `0x04 UserStats` (achievements/leaderboards; uses Network internally)
- `0x05 Update` (menu/service use; games usually not)
- `0x7E Vendor` (publisher/private services; not standardized)
- `0x7F Diagnostics` (optional)

Feature bits (in `feature_bits`):

- bit 0: System
- bit 1: Storage
- bit 2: Network
- bit 3: Identity
- bit 4: UserStats
- bit 5: Update
- bits 16..31: reserved for vendor/private advertisement

### Payload encoding conventions (v1)

All service payloads use packed binary structs (little-endian). Strings are UTF‑8 with a leading `u8` length unless otherwise specified. Variable-length arrays are prefixed with a `u16 count`.

### System service (0x00)

Methods:

`0x00 GET_API_INFO`
Request payload: none.
Response payload:

```c
struct ApiInfo {
  uint8_t  api_major;
  uint8_t  api_minor;
  uint8_t  layout_ver;
  uint8_t  flags;
  uint32_t feature_bits;
  uint16_t max_frame;
  uint16_t reserved0;

  // Security posture (OTP-derived properties)
  uint32_t posture_props;        // bitfield, see GET_SECURITY_INFO or spec table
  uint8_t  boot_key_valid_mask;  // bits 0..3 map to OTP boot key slots 0..3
  uint8_t  reserved1[3];
};
```

`0x01 GET_DEVICE_ID`
Request payload:

```c
struct GetDeviceIdReq {
  uint8_t scope;   // 0=device, 1=collection-scoped, 2=publisher-scoped
  uint8_t reserved[3];
};
```

Response payload: 16 bytes of ID (opaque). The cartridge SHOULD avoid exposing a stable global ID to untrusted titles unless policy allows; collection-scoped IDs are recommended.

`0x02 GET_CAPS`
Request payload: none.
Response payload:

```c
struct CapsResp {
  uint16_t count;
  // repeated:
  // uint16_t cap_id;
  // uint16_t cap_flags;
  // uint32_t cap_param;
};
```

`cap_id` is a stable numeric mapping of capabilities defined by this spec. `cap_param` is capability-specific (e.g., RAM KB, cache KB).

`0x03 GET_RANDOM`
Request payload: `u16 nbytes` (max = `c2h_scratch_len`).
Response: random bytes in response scratch, with `scratch_len=nbytes`, `payload_len=0`.

`0x04 RESET_TO_MENU`
Request payload: none. Response: OK. The cart may reset the MSX and re-enter Menu after responding.


`0x05 GET_SECURITY_INFO`
Request payload: none.
Response payload:

```c
struct SecurityInfoResp {
  uint32_t posture_props;        // bitfield, see “Security posture properties and policy flags”
  uint8_t  boot_key_valid_mask;  // bits 0..3 map to OTP boot key slots 0..3
  uint8_t  reserved0[3];
};
```

`0x06 GET_POLICY_FLAGS`
Request payload: none.
Response payload:

```c
struct PolicyFlagsResp {
  uint32_t policy_flags;   // bitfield, see “Policy flags bit assignments (v1)”
  uint32_t policy_gen;     // increments on every successful policy update (monotonic in flash)
  uint8_t  policy_hash16[16]; // first 16 bytes of SHA-256(policy_canonical_bytes)
};
```

If the policy document is missing or invalid, the platform MUST return `policy_flags=0` and `policy_gen=0`.

### Storage service (0x01)

Storage methods operate on “save blobs” and “extended data blobs” addressed by ids.

Definitions:

- `blob_kind`: 0=save, 1=extended
- `blob_id`: 16-bit id assigned by the platform per Payload/profile

Methods:

`0x00 LIST_BLOBS`
Request payload:

```c
struct ListBlobsReq { uint8_t blob_kind; uint8_t reserved[3]; };
```

Response payload:

```c
struct ListBlobsResp {
  uint16_t count;
  // repeated entries:
  // uint16_t blob_id;
  // uint32_t size_bytes;
  // uint32_t max_bytes;
  // uint16_t flags;
  // uint16_t reserved;
};
```

`0x01 READ_BLOB`
Request payload: `{u16 blob_id, u32 offset, u16 len}`.
Response: bytes in response scratch (preferred). `len` MUST be ≤ `c2h_scratch_len`.

`0x02 WRITE_BLOB_BEGIN`
Request payload: `{u16 blob_id, u32 total_len, u16 flags}`.
Response payload: `{u16 handle, u16 chunk_hint}`.

`0x03 WRITE_BLOB_CHUNK`
Request payload: `{u16 handle, u32 offset, u16 len}` with data in request scratch.
Response: OK.

`0x04 WRITE_BLOB_COMMIT`
Request payload: `{u16 handle}`. Response: OK. Commit MUST be atomic.

`0x05 DELETE_BLOB`
Request payload: `{u16 blob_id}`. Response: OK.

### Network service (0x02) (optional)

The Network service exists for advanced titles and tools. Most games should use UserStats and other higher-level services so they do not need to implement HTTP/TLS.

If present, v1 defines a minimal HTTP client so Z80 code can do simple web calls without TLS knowledge.

`0x00 NET_STATUS`
Response payload: `{u8 connected, u8 rssi, u32 ipv4, u32 reserved}`.

`0x01 HTTP_REQUEST`
Request payload:

```c
struct HttpReq {
  uint8_t  verb;      // 0=GET,1=POST,2=PUT,3=DELETE
  uint8_t  flags;     // bit0=https, bit1=allow_redirects
  uint16_t url_len;   // bytes in request scratch (URL UTF-8)
  uint16_t body_len;  // bytes in request scratch after URL
  uint16_t timeout_ms; // low 16 bits
};
```

Request scratch layout: `[url bytes][body bytes]`.

Response payload:

```c
struct HttpResp {
  uint16_t http_status;
  uint16_t body_len;      // bytes returned in response scratch
};
```

Response scratch contains response body bytes (truncated to scratch size). Large bodies require chunking (out of scope for v1).

### Identity service (0x03)

`0x00 LIST_PROFILES`
Response: `{u16 count, entries...}` where each entry contains `{u16 profile_id, u8 name_len, name_bytes...}` (name data may be in response scratch if too large).

`0x01 SET_ACTIVE_PROFILE`
Request: `{u16 profile_id}`. Response: OK.

`0x02 GET_ACTIVE_PROFILE`
Response: `{u16 profile_id, u8 flags}`.

`0x03 GUEST_BEGIN` / `0x04 GUEST_END`
Begin/end a guest session. Guest policy is defined by platform settings.

### UserStats service (0x04)

This service provides achievements and leaderboards with integrity features. The cartridge is responsible for persistence and (if enabled) online sync.

`0x00 STAT_GET`
Request: `{u16 stat_id}`. Response: `{s32 value}`.

`0x01 STAT_SET`
Request: `{u16 stat_id, s32 value, u8 op}` where `op` is 0=set, 1=add, 2=max. Response: OK.

`0x02 ACH_UNLOCK`
Request: `{u16 ach_id}`. Response: OK.

`0x03 LEADER_RUN_BEGIN`
Request: `{u16 leaderboard_id}`.
Response: token bytes in response scratch and `{u16 token_len}` as payload.

`0x04 LEADER_SUBMIT`
Request payload:

```c
struct LeaderSubmitReq {
  uint16_t leaderboard_id;
  uint16_t token_len;     // token bytes provided in request scratch
  uint32_t score;
  uint8_t  proof_kind;    // 0=none, 1=input_log
  uint8_t  flags;
  uint16_t proof_len;     // bytes after token in request scratch
};
```

Request scratch layout: `[token bytes][proof bytes]`.

The cartridge MUST submit a signed record to the configured server if online sync is enabled; otherwise it MUST queue locally for later sync.

If the platform has no configured server, it MUST still store the score locally.

### Update service (0x05) (menu/tooling)

Update is typically used by the Menu, not by games.

`0x00 UPDATE_QUERY`
Response: available update info (implementation-defined format).

`0x01 UPDATE_INSTALL`
Request: install a staged bundle (implementation-defined). Response: OK/denied.

### Vendor service (0x7E)

Vendor/private services are allowed but MUST NOT collide with standardized service/method ids. Vendor services SHOULD advertise a vendor bit in `feature_bits` and SHOULD provide a string descriptor via a vendor-specific method.

### Conformance expectations

A conforming v1 implementation MUST:

- expose the header and regs exactly as specified,
- implement rings with correct wrap and length framing,
- implement System service methods `GET_API_INFO` and `GET_CAPS`,
- and implement at least one of Storage or UserStats (otherwise the API window is not useful).

A conformance test suite SHOULD include ring edge cases (wrap marker, exact-fit, full), scratch bounds checks, and error code correctness.

### 7.3 Stats, achievements, and leaderboards contract (v1)

**Goal:** make “Steam-like” user-facing features feasible on MSX titles without forcing each game to invent its own persistence, sync, or UI conventions.

This contract is inspired by the broad shape of Steam’s “Stats and Achievements” system: per-user persistent stats, achievements, and leaderboard submissions.

#### Schema ownership

A Payload (game) owns its own schema:

- **Stats schema**: named numeric values (int/float) and optional aggregation rules.
- **Achievements schema**: named booleans (locked/unlocked) with optional metadata (title, description, icon id).
- **Leaderboards schema**: named boards with score type, sort direction, and submission policy.

The schema SHOULD live in the Payload Manifest (so tooling can validate and UI can render without running the game).

#### Local persistence

- All stats/achievements are persisted per **User Profile**.
- Guest sessions MUST either:
  - persist into a dedicated “Guest” profile sandbox, or
  - be explicitly non-persistent for stats/achievements (policy choice; must be consistent).

#### Sync policy

When network is available, the system MAY sync:

- unlocked achievements,
- stat updates (with aggregation rules),
- leaderboard submissions.

Sync MUST be idempotent and resilient to intermittent connectivity.

#### Leaderboard integrity baseline

If a title enables online leaderboards, the minimum integrity baseline is:

- server-issued run token (single-use),
- device-signed submissions,
- replay protection on the server.

Higher integrity tiers (deterministic seed + replay verification) remain an opt-in mechanism.

#### UI conventions

The platform SHOULD provide a standard UI surface:

- “Achievements” viewer per Payload,
- “Stats” viewer per Payload,
- “Leaderboards” viewer and submission status.

Games MAY also implement their own UI, but should not be required to.

---

## 8. Menu host stub ABI

The Menu is the user-facing launcher and configuration UI. It runs as a cooperation between the cartridge firmware and a small Z80 program executed on the MSX host. This chapter defines the stable ABI between those components.

### Scope and roles

The Menu is a platform component that must run on every supported MSX host. During Menu mode the cartridge runs a small Z80 program on the MSX (“Menu Host Stub”) that performs VDP output and input sampling using the host’s own BIOS/VDP conventions. The RP2350-side Menu logic communicates with the stub through a shared mailbox.

This contract specifies the **stable ABI** between:

- the RP2350 firmware (producer of commands), and
- the Z80 stub (consumer of commands, producer of results).

The stub is only required in Menu mode and is not required during normal Payload runtime.

### Address space and mapping

In Menu mode the cartridge MUST expose a **16KB Menu Page** (read/write) in the MSX slot address space.

- The Menu Page MUST be mapped as a normal 16KB page (no wait-states beyond what the platform already requires for stable operation).
- The Menu Page SHOULD be mapped at **0x4000–0x7FFF** (page 1) so the stub can execute from it easily.
- If a different mapping is used, the cartridge MUST still ensure the stub can find the Menu Page (for example by booting directly into it, or by mapping it into a conventional page before launching the stub).

The Menu Page contains:

- a fixed header and mailbox at known offsets, and
- a shared data buffer used for strings and bulk transfers.

### Endianness and atomicity

All multi-byte fields in the Menu Page are **little-endian**.

Synchronization is done with two 16-bit sequence counters:

- the RP2350 MUST write command fields first and write `cmd_seq` last,
- the stub MUST write response fields first and write `resp_seq` last.

The stub MUST only act on a command when `cmd_seq != resp_seq`.

Sequence counters are 16-bit and wrap naturally; wrap MUST be treated as normal.

### Fixed layout within the 16KB Menu Page

Offsets are relative to the base of the Menu Page.

- `0x0000..0x003F`  MenuStubHeader (64 bytes)
- `0x0040..0x007F`  MenuMailbox (64 bytes)
- `0x0080..0x00FF`  Reserved for future expansion
- `0x0100..end`     Shared data buffer (`data_len` bytes)

### Header layout (MenuStubHeader)

```c
// at offset 0x0000, little-endian
struct MenuStubHeader {
  char     sig[4];        // "JLMN"
  uint8_t  abi_major;     // MUST be 1 for this spec
  uint8_t  abi_minor;     // backward compatible increments
  uint16_t header_len;    // MUST be 64
  uint16_t mailbox_ofs;   // MUST be 0x0040
  uint16_t data_ofs;      // MUST be 0x0100
  uint16_t data_len;      // bytes available for shared data
  uint16_t stub_entry;    // offset of stub entrypoint within this page
  uint32_t host_caps;     // bitfield (see below)
  uint32_t vdp_caps;      // bitfield (see below)
  uint16_t build_id;      // implementation-defined build identifier
  uint16_t reserved0;
  uint8_t  reserved[20];  // MUST be zero for v1
};
```

`host_caps` bits:

- bit 0: MSX1
- bit 1: MSX2
- bit 2: MSX2+
- bit 3: turboR
- bit 4: 80-column text supported
- bit 5: BIOS keyboard scan available (always expected on MSX)
- other bits reserved (MUST be zero in v1)

`vdp_caps` bits:

- bit 0: TMS9918/V9938 compatible VRAM port writes supported
- bit 1: palette programmable (MSX2+)
- bit 2: bitmap mode supported (stub-defined “menu bitmap mode”)
- other bits reserved

### Mailbox layout (MenuMailbox)

```c
// at offset 0x0040, little-endian
struct MenuMailbox {
  volatile uint16_t cmd_seq;   // written by RP2350, last
  volatile uint16_t resp_seq;  // written by stub, last
  volatile uint16_t cmd_id;    // command selector
  volatile uint16_t status;    // 0=OK, nonzero=error
  volatile uint32_t arg0;
  volatile uint32_t arg1;
  volatile uint32_t arg2;
  volatile uint32_t arg3;
  volatile uint16_t in_len;    // bytes provided in shared data buffer
  volatile uint16_t out_len;   // bytes written by stub to shared data buffer
};
```

The shared data buffer begins at `data_ofs` and has size `data_len`.

### Common argument packing

Unless otherwise stated:

- `arg0` packs `x` in bits 0..7 and `y` in bits 8..15.
- `arg1` packs `w` in bits 0..7 and `h` in bits 8..15.
- `arg2` and `arg3` are command-specific.

This keeps most Menu commands usable without writing 32-bit values from Z80 code.

### Command IDs and semantics

All commands are synchronous: the stub must complete the command before updating `resp_seq`.

`0x0000 NOP`
Does nothing. Always succeeds.

`0x0001 GET_HOST_INFO`
Writes a `HostInfo` struct into the shared data buffer and sets `out_len`.

`0x0002 SET_MODE`
Selects the Menu display mode. `arg0` low byte is `mode_id`.

Mode IDs (v1):

- `0`: TEXT_40  (SCREEN 0, 40 columns) MUST be supported
- `1`: TEXT_80  (SCREEN 0, 80 columns) MAY be supported
- `2`: BITMAP   (best-effort bitmap mode) MAY be supported

Rules:

- On MSX1, `BITMAP` SHOULD map to SCREEN 2 (256×192), if the stub implements bitmap mode at all.
- On MSX2+, `BITMAP` SHOULD map to SCREEN 5 (256×212, 16 colors), if implemented.
- If a requested mode is unsupported, stub returns `E_UNSUPPORTED`.

`0x0003 CLEAR`
Clears the screen. `arg0` low byte is `clear_kind`:

- `0`: clear text/bitmap to background
- `1`: clear text area only (text modes)
- `2`: clear bitmap plane only (bitmap mode)

`0x0004 PUT_TEXT`
Writes text at `(x,y)` in TEXT modes.

- `arg0`: packed x,y
- `in_len`: number of bytes of text in shared data buffer
- Encoding: UTF‑8 (see below)
- The stub MUST clip safely at the right edge; it MUST NOT wrap to the next line unless `arg2` bit 0 is set (“wrap”).

If called in bitmap mode, stub returns `E_BAD_STATE`.

`0x0005 READ_INPUT`
Writes an `InputSnapshot` struct into shared data buffer and sets `out_len`.

`0x0006 VDP_WRITE_REG`
Writes one VDP register. `arg0` low byte is `reg`, `arg0` high byte is `value`.

If unsupported, returns `E_UNSUPPORTED`.

`0x0007 VRAM_WRITE`
Writes raw bytes to VRAM.

- `arg0`: destination VRAM address (0..)
- `in_len`: bytes to write from shared buffer

The stub MUST implement this by setting the VDP VRAM write address and streaming bytes with auto-increment, so the semantics are consistent across VDPs.

`0x0008 VRAM_FILL`
Fills VRAM with a value.

- `arg0`: destination VRAM address
- `arg1`: length in bytes (low 16 bits)
- `arg2` low byte: fill value

`0x0009 BEEP`
Plays a short feedback sound (best-effort). `arg0` low byte is `kind` (0..N).

`0x000A IDLE`
Busy-waits (or BIOS-waits) for approximately `arg0` milliseconds (low 16 bits). This allows the RP2350 to pace animations without relying on host interrupts.

### UTF‑8 text encoding rules (PUT_TEXT)

The shared text buffer is UTF‑8. The stub MUST support ASCII (U+0020..U+007E) faithfully.

For non-ASCII:

- the stub MAY implement a mapping to MSX character sets (for example Japanese MSX BIOS fonts), but
- if no mapping exists, it MUST substitute `?` (0x3F) rather than emitting garbage or crashing.

### HostInfo structure

`GET_HOST_INFO` returns:

```c
struct HostInfo {
  uint8_t  msx_gen;      // 1=MSX1, 2=MSX2, 3=MSX2+, 4=turboR (best-effort)
  uint8_t  vram_kb;      // best-effort (16, 64, 128...)
  uint8_t  text_cols;    // 40 or 80 (current or best supported)
  uint8_t  reserved0;
  uint32_t host_caps;    // same as header
  uint32_t vdp_caps;     // same as header
};
```

### InputSnapshot structure

`READ_INPUT` returns:

```c
struct InputSnapshot {
  uint8_t kbd_rows[11];  // 11 rows, 8 bits each, 0=pressed, 1=released
  uint8_t joy1;          // bitfield, 0=pressed: b0=Up b1=Down b2=Left b3=Right b4=TrigA b5=TrigB
  uint8_t joy2;          // same mapping
  uint8_t reserved[3];
};
```

Keyboard rows MUST correspond to the standard MSX keyboard matrix row order used by BIOS scanning, so software that already understands the MSX matrix can interpret it.

Joystick bytes MUST use the mapping documented in this spec (Up/Down/Left/Right/TrigA/TrigB, active-low), so Menu logic can be shared across machines.

### Error codes (Menu mailbox status)

- `0x0000 OK`
- `0x0001 E_UNSUPPORTED` (unknown command or unsupported feature)
- `0x0002 E_BAD_ARG` (invalid argument or length)
- `0x0003 E_BAD_STATE` (command not valid in current mode)
- `0x0004 E_OVERFLOW` (in_len/out_len exceeds data_len)
- `0x0005 E_HW` (VDP/BIOS call failed; best-effort)

The stub MUST never crash the host for an invalid command; it MUST return an error.

### Conformance expectations

A conforming stub MUST at minimum implement: `GET_HOST_INFO`, `SET_MODE(TEXT_40)`, `CLEAR`, `PUT_TEXT`, `READ_INPUT`, `IDLE`.

All other commands are optional but SHOULD be implemented where feasible.

## 9. Peripheral framework and catalog

Peripherals are the mechanism by which JLPiCart provides MSX-visible devices (through classic port/memory mappings), passive observation/mirroring, or API-only services. This chapter is the single place where port mappings, legacy profiles, and device exposure modes are specified.

### 9.1 Peripheral framework

This section consolidates everything related to “devices” that JLPiCart can provide to the MSX (or use internally) and defines **how** each device is exposed. It is intentionally the single place where port mappings, memory/subslot mappings, and “legacy compatibility” details live.

### Declared / allowed / activated peripherals

JLPiCart does not “discover devices” by poking hardware. Instead, every peripheral participates in the **Declared → Allowed → Activated** pipeline defined in the Resource and capability model contract:

- **Declared** comes from the Board Definition (hw peripherals) or the firmware Build Descriptor (sw peripherals).
- **Allowed** is the result of signed policy masking (publisher/user restrictions) and permitted exposure interfaces.
- **Activated** is the result of either **verification** (hw, only if explicitly safe) or **allocation** (sw, only if resources/mappings fit for the current Payload).

This rule exists to prevent unsafe probing. A peripheral probe MUST only touch pins/buses dedicated to that peripheral, and MUST NEVER “toggle GPIOs to see what happens” on shared lines or anything connected to the MSX bus.

In practice, this means:

- physical peripherals are enabled only when the board definition says they are candidates, and then optionally verified;
- emulated peripherals are enabled only when requested by the Payload or policy defaults *and* the allocator can reserve resources without port/page collisions.


### Peripheral exposure interfaces

A peripheral MAY be exposed through one or more of these interfaces. The active interface is chosen by the Launch Plan based on Board Definition support, System Settings policy, and the Payload’s declared requirements.

**MSX emulation interface (active bus device)**
The cartridge implements a conventional MSX-visible device interface: reads and writes at expected I/O ports and/or mapped memory regions, optionally via subslots. This is used for classic devices (OPL4, disk controller, Nextor block device, UNAPI BIOS, etc.).

- This interface MUST declare the ports/memory ranges/subslots it occupies.
- When multiple standard mappings exist in the ecosystem, the interface MUST support one or more named **legacy mapping profiles** (and MAY allow limited overrides).
- The allocator MUST prevent port collisions: if two active emulation interfaces would overlap, the Launch Plan MUST fail unless one of them is disabled or remapped within its supported profiles.

**MSX monitor interface (passive bus observer)**
The cartridge observes MSX bus activity (I/O and/or memory writes) for a specified range, but does not drive the MSX data bus for that device. This is useful for “mirroring” or “sidecar” behavior (e.g., capturing VDP writes to drive an external display, logging input activity, instrumentation).

- A monitor interface MUST declare exactly what it observes (ports and/or address ranges) and MUST be safe to run alongside the real device.
- A monitor interface MUST NOT rely on being able to see internal MSX signals that are not present on the cartridge bus (unless explicitly stated as a board-specific feature).
- A monitor interface MUST be treated as consuming CPU/RAM resources, but MUST NOT consume MSX-visible port space.

**JLPiCart API-only interface (no MSX ports)**
The peripheral is accessible only via the JLPiCart API memory-mapped window and does not occupy global I/O ports. This is the preferred mode for new JLPiCart-aware software because it avoids collisions and enables richer semantics.

- API-only peripherals MUST register one or more API services with stable method IDs and versioning.
- API-only exposure MUST remain functional regardless of which legacy emulation devices are enabled.

A peripheral may expose **both** an API-only interface and an MSX emulation interface at the same time (e.g., Networking: JLPiCart API sockets plus UNAPI for legacy software). In such cases, the MSX emulation interface is optional and may be disabled without affecting the API surface.

### Peripheral configuration and selection

Peripheral selection is resolved deterministically:

- The Board Definition declares which peripherals exist and which interfaces/profiles they support.
- System Settings declare device-wide policy (e.g., “legacy compatibility enabled”, preferred legacy profiles, whether monitor peripherals are enabled).
- The Payload requirements select peripherals by ID and MAY request a specific interface and/or legacy profile.
- If the Payload requests an interface/profile that the board does not support, the Launch Plan MUST fail with a clear reason unless the request is marked optional.

Peripheral configuration MUST be expressed in a way that can be validated before launch. A Payload MUST NOT depend on “try a port and see if it works”.

### Standard peripheral catalog

The platform defines a standard set of peripherals. Boards may omit some, but the names and semantics are stable.

#### RAM expansion and mapper

**Primary purpose:** provide additional RAM and mapper behavior expected by MSX software.

- RAM expansion is an MSX emulation interface (memory mapping).
- RAM expansion MAY be implemented as a memory mapper in the primary cartridge slot or in a subslot.
- On MSX1 hosts with <64KB internal RAM, the cartridge MUST be capable of providing enough mapped RAM that software expecting a 64KB address space operates correctly.
- The Launch Plan MUST account for the RAM budget shared between: MSX-facing RAM expansion, caches, firmware, and emulated device state.

#### Audio devices

**PSG (AY-3-8910)**
- MUST support internal emulation and MAY support pass-through (using host PSG).
- Exposure is typically MSX emulation (ports), but selection is per Payload.

**SCC / SCC+**
- MUST support internal emulation and MAY support pass-through when an external SCC is present.
- Exposure is MSX emulation at the expected SCC port/memory mapping profile for the chosen mapper.

**OPL4 (FM + PCM wavetable)**
- When supported, exposure is MSX emulation using the standard OPL4 register/data ports (legacy profile).
- The platform MAY provide an API-only “high-level audio” service for JLPiCart-aware titles, but this does not replace OPL4 emulation for legacy software.
- Feasibility and performance constraints remain an Open Investigation; boards MAY omit OPL4.

**OPL4 (FM + PCM wavetable) — legacy port profile**
- Standard port assignment uses `0x7E–0x7F` (commonly used by MoonSound-class devices): `0x7E` register select, `0x7F` data (see MSX I/O ports list reference).
- This profile is the default for “OPL4 emulation interface” unless a board or system policy explicitly disables it to avoid collisions.


#### Video and display

**VDP upgrade / alternative VDP (future/optional)**
- If implemented, this is an MSX emulation interface presenting an enhanced VDP to an MSX1 host.
- Port compatibility MUST be clearly specified via a legacy profile.
- This remains an Open Investigation and is not required for the platform baseline.

**Port mapping references**
- A “second VDP port set” for external VDP upgrades is commonly mapped at `0x88–0x8B` in some MSX2 upgrade cartridges (separate from the internal VDP `0x98–0x9B` ports).
- Graphics9000 / V9990-class devices are commonly mapped at `0x60–0x6F`.

These ranges are included here to ground legacy profile naming and avoid accidental collisions (see MSX I/O ports list reference).


**External display mirroring (monitor peripheral)**
- A monitor interface MAY observe VDP I/O writes (e.g., 0x98–0x9B) and selected memory writes, and drive an external display pipeline (e.g., VGA, HDMI, e-ink).
- Because this is monitor-only, it MUST NOT consume additional MSX-visible ports and MUST be safe even when the host VDP remains the active renderer.
- The observed ranges and timing expectations MUST be stated in the peripheral descriptor.

#### Floppy disk controller

- Exposure is MSX emulation: a WD279x-compatible controller at a declared legacy profile.
- Floppy images are served from a Floppy Bundle Payload.
- Both read-only and read-write images MUST be supported.
- Multi-disk software requires a disk swap mechanism (Open Investigation); swap MUST be exposed via Menu and MAY be exposed via API for automation.
- Read-write changes MUST persist power-loss safely. If the Source is writable (USB stick), the platform MAY offer “write back to Source” but MUST default to internal persistence for safety.

#### Mass storage (Nextor / block device)

Mass storage is defined as a peripheral so that all “DOS/Nextor/SymbOS” discussion is localized here.

**API-only storage**
- The Storage service in the JLPiCart API provides file-like and/or block-like access for JLPiCart-aware software.
- This mode consumes no MSX I/O ports.

**MSX emulation storage (Nextor-compatible)**
- When enabled, the cartridge presents a Nextor-compatible block device interface to the MSX and supports MSX-DOS2/Nextor booting.
- The interface MUST be selectable by legacy profile and MUST declare its port usage.
- Backing stores MAY include internal flash partitions and attached USB mass storage, exposed as one or more volumes.

**Coexistence rules**
- If the cartridge exposes storage intended for MSX-DOS, it MUST either provide a Nextor-compatible driver story or clearly state that storage access is “JLPiCart-only” via API-only mode.
- SymbOS compatibility is optional; when targeted, the platform SHOULD provide either a native SymbOS driver or a compatibility profile that matches widely-supported SymbOS storage hardware.

**References (Nextor / MSX-DOS2 / SymbOS)**
- Nextor 2.0 Driver Development Guide (device driver interface and requirements): https://raw.githubusercontent.com/Konamiman/Nextor/v2.1/docs/Nextor%202.1%20Driver%20Development%20Guide.md
- SymbOS manual notes that MSX-DOS2 / Nextor is recommended when using large storage devices on MSX: https://www.symbos.org/download/20170830-V30/symbos-manual.pdf


#### Networking (WiFi via ESP32)

Networking is defined as a peripheral with both API-first and legacy interfaces.

**API-only networking (preferred for new titles)**
- The JLPiCart API provides network primitives (sockets, DNS, time fetch, service endpoints for leaderboards/profile sync) subject to Security Contracts.
- Titles using the JLPiCart API assume the presence of JLPiCart networking hardware and MUST NOT assume interoperability with other network cartridges.

**Legacy networking (optional)**
When “legacy networking compatibility” is enabled, the cartridge MAY expose MSX-UNAPI implementations:

- Ethernet UNAPI (link-layer) and/or
- TCP/IP UNAPI (full stack).

If only Ethernet UNAPI is exposed, classic MSX setups often load a resident TCP/IP stack under MSX-DOS (e.g., InterNestor Lite). The platform SHOULD prefer exposing TCP/IP UNAPI directly (or a close equivalent) so users do not need a resident stack for common tools.

SymbOS historically supports a limited set of MSX network hardware; when SymbOS networking is a goal, the platform SHOULD provide a SymbOS driver or emulate a supported compatibility profile.



**Legacy profile decision**
- When legacy networking is enabled, JLPiCart SHOULD expose **TCP/IP UNAPI v1.1** directly (full stack), implemented by the cartridge firmware, so classic MSX-DOS networking tools can run without loading a resident stack.
- Optionally, JLPiCart MAY also expose **Ethernet UNAPI v1.1** (link-layer) for compatibility with setups that expect an Ethernet device + resident TCP/IP stack (for example, InterNestor Lite).

**References (UNAPI & InterNestor Lite)**
- MSX-UNAPI core and TCP/IP/Ethernet specifications: https://github.com/Konamiman/MSX-UNAPI-specification
- InterNestor Lite overview (expects Ethernet UNAPI hardware in Ethernet mode): https://sourceforge.net/projects/internestor/
- Historical context: MSX UNAPI + InterNestor Lite for Ethernet UNAPI: https://www.msx.org/news/software/en/msx-unapi-10-internestor-lite-11-and-obsonet-bios-11
#### Developer-defined peripherals (examples)

The peripheral framework is intended to scale to add-ons such as:

- RS232 / UART devices (MSX emulation via a known serial interface profile, and/or API-only serial service),
- auxiliary displays (API-only UI surface, and/or monitor mirroring),
- GPIO-driven accessories (API-only).

The contract for adding peripherals is defined below.

### Peripheral extension contract (v1)

#### Purpose

This contract defines how third-party developers add new peripherals (hardware or emulated) to JLPiCart in a way that integrates cleanly with:

- capability reporting,
- resource allocation and Launch Plan determinism,
- MSX-visible mapping (memory pages, subslots, I/O ports),
- optional exposure via the JLPiCart API,
- and Menu configuration/UI.

The goal is that adding “RS232”, “extra audio chip”, or “aux display” feels like adding a module with a descriptor, not like patching random code paths.

#### Peripheral classes

A peripheral is either:

1. **MSX-visible**: it exposes an address/port-level interface that MSX software can talk to directly (e.g., disk ROM, mapper, serial interface, sound chip).
2. **Cartridge-local**: it is used by the cartridge and only optionally exposed to MSX software via the JLPiCart API (e.g., e-ink display, LEDs, sensors).

MSX-visible peripherals MUST declare mapping needs. Cartridge-local peripherals MUST NOT consume MSX global resources unless explicitly exposed.


#### Peripheral exposure interfaces

A peripheral MAY support multiple **interfaces**. An interface defines *how* the peripheral is exposed:

- `msx_emulation`: the cartridge actively implements an MSX-visible device (I/O ports and/or mapped memory, optionally subslots).
- `msx_monitor`: the cartridge passively observes a declared set of bus writes (and optionally reads) without driving the MSX data bus for that interface.
- `api_only`: the peripheral is accessed only via the JLPiCart API window and consumes no global MSX I/O ports.

Descriptors MAY declare interfaces explicitly via an `interfaces[]` array. If `interfaces[]` is omitted, the descriptor’s existing `mapping_requests` describe a single `msx_emulation` interface and `api_services` describe optional API exposure.

An `msx_emulation` interface MAY declare one or more **legacy mapping profiles** (e.g., “unapi_tcpip”, “opl4_standard”, “nextor_default”). The platform MUST treat port/memory selection as a profile choice, not as ad-hoc user-defined port numbers.

The Launch Plan MUST select a compatible interface/profile combination or fail with a clear reason.

#### Peripheral descriptor (jlpicart.peripheral.v1)

A peripheral MUST be described by a descriptor object. The descriptor is part of the firmware build (or a board pack).

Normative shape:

```json
{
  "schema": "jlpicart.peripheral.v1",
  "id": "jlpicart.periph.rs232",
  "version": "1.0.0",
  "class": "msx_visible",
  "provides_capabilities": [
    {"id": "rs232", "param": 1}
  ],
  "consumes_resources": [
    {"id": "uart", "amount": 1, "exclusive": true},
    {"id": "pio_sm", "amount": 1, "exclusive": true}
  ],
  "mapping_requests": {
    "io_ports": [{"kind":"range", "len": 8, "preferred_base": null}],
    "memory_pages": [],
    "subslot": {"needs": false}
  },
  "api_services": [],
  "menu_settings": {
    "items": [
      {"key": "baud", "type":"enum", "values":[9600,19200,38400,57600], "default": 38400}
    ]
  }
}
```

#### Resource ids and mapping requests

The platform maintains a registry of resource ids and their semantics. Examples:

- `sram_kib`, `flash_cache_kib`
- `uart`, `spi`, `i2c`
- `pio_sm`, `dma_ch`
- `io_port_range`
- `msx_page_16k`
- `subslot_id`

A peripheral’s descriptor MUST specify:

- which resources it consumes,
- whether a resource is exclusive,
- and how much it consumes.

Mapping requests:

- `io_ports` may request one or more ranges.
- `memory_pages` may request one or more 16KB pages, optionally with preferences.
- `subslot.needs=true` indicates the peripheral requires subslot decoding (rare; discouraged unless truly needed).

#### Allocation rules (normative)

The allocator MUST:

- treat peripheral descriptors as inputs,
- compute a deterministic mapping,
- and refuse launch/build if exclusive resource requests conflict.

Determinism requirements:

- given the same board definition and enabled peripherals, allocation results MUST be identical.
- if multiple placements are possible, the allocator MUST use a stable tie-break rule (e.g., sort by peripheral id, then allocate from lowest available).

#### MSX-visible mapping output (Launch Plan)

For MSX-visible peripherals the Launch Plan MUST include:

- which slot/subslot the peripheral is exposed in,
- which 16KB pages (if any) it occupies,
- which I/O port ranges it decodes,
- and which compatibility layers are enabled (if relevant).

This output is what publishers rely on when writing software.

#### API exposure rules

A peripheral MAY expose a JLPiCart API service.

Rules:

- standardized services use standardized ids,
- peripheral services MUST use the Vendor service range unless they are adopted into the core spec,
- services MUST be versioned and advertised via `feature_bits` and/or a descriptor method.

If a peripheral is both MSX-visible and API-exposed, the API should be considered an “enhanced control plane”, not the primary compatibility plane.

#### Menu configuration integration

A peripheral MAY define `menu_settings`:

- the Menu must present these settings in a stable way,
- settings values are stored in System Settings or Profile Overrides depending on scope,
- changing settings MUST not silently break sealed-bundle assumptions; settings that affect signed behavior must be declared as “policy” or must require a new bundle.

#### Developer workflow for adding a peripheral (normative steps)

A developer adds a peripheral by providing:

1. a peripheral descriptor (`jlpicart.peripheral.v1`)
2. firmware implementation that can be enabled/disabled and reports resource use
3. optional Z80-facing driver or docs if MSX-visible
4. optional API service implementation if exposed via JLPiCart API
5. optional menu settings UI hints

Board integration happens via the Board Definition:

- wiring (which UART/SPI pins, etc.) is board-specific
- preferred mappings and port reservations are board-specific

#### Examples

RS232:

- MSX-visible mode: emulate a known MSX RS232 interface so existing tools can use it, at the cost of port decoding and driver quirks.
- API-only mode: expose a simple byte stream service via the JLPiCart API for titles that target JLPiCart.

E-ink display:

- cartridge-local peripheral, usually Menu-driven
- optional API service `AuxDisplay` for games that want to draw status/graphics.

#### Conformance expectations

A conforming peripheral implementation MUST be possible to:

- enable/disable without changing unrelated mappings,
- allocate deterministically,
- and report conflicts as structured errors.

### 9.2 Legacy compatibility contract (v1)

**Goal:** support important legacy MSX software ecosystems without conflating them with the JLPiCart API.

Legacy compatibility is implemented through specific **peripherals** and their **MSX emulation interfaces** (UNAPI networking, Nextor mass storage, SymbOS drivers/profiles, etc.). The platform MUST treat these as optional interface modes that can be enabled/disabled and remapped by profile, while keeping API-only access stable.

See Peripherals → Networking and Peripherals → Mass storage for the normative legacy behavior and configuration.

## 10. Security, licensing, and privacy

This section defines **Security Contracts v1** for JLPiCart. These contracts are written to be implementable and testable, and to stay stable even if internal implementations change.

The security model is designed around an honest constraint: once a title is executed and ROM bytes are being served on the MSX bus, **runtime extraction is possible** with dedicated hardware (bus sniffing). Therefore, platform security focuses on:
- protecting secrets and content **at rest** and **in transit** (downloads, updates, storage)
- ensuring **authentic firmware** and **authentic content bundles**
- enabling **revocation** and **anti-rollback**
- providing **tamper-evident / cheat-resistant** workflows (especially for online leaderboards)
- keeping “sealed” behavior optional and reversible during development, while making it strong and predictable in production

### Threat model and non-goals

**In scope**
- casual and moderate attackers dumping external flash, copying SD/USB contents, replaying network requests, or tampering with configuration
- malicious clients attempting to submit forged online scores or achievements
- firmware-level attempts to run unauthorized code on a production cartridge
- accidental corruption (power loss during writes, partial updates)

**Out of scope (explicit non-goals)**
- preventing a determined hardware attacker from extracting runtime ROM data via MSX bus sniffing
- preventing invasive lab attacks against the MCU silicon (fault injection, FIB, decap). The platform aims to raise cost, provide detection signals where feasible, and contain compromise via per-device keys + revocation.

### Security building blocks and assumptions

JLPiCart is built on RP2350 security features as documented by Raspberry Pi:
- 8KB antifuse OTP with 128-byte locking granularity (hard/soft locks)
- signed boot chain and secure boot configuration via OTP
- optional encrypted-boot flows
- TrustZone-M separation (Secure/Non-secure worlds)
- hardware TRNG and SHA acceleration

Security features have had public scrutiny and real demonstrated attacks. The platform therefore:
- treats security posture as a set of OTP-derived properties plus an authenticated policy document (flash)
- requires per-device identities and server-side revocation to contain compromise
- requires update workflows that remain safe even when some units are compromised


### RP2350 secure-boot posture (normative)

JLPiCart uses **signed boot** as its primary security boundary: the RP2350 boot ROM MUST verify a firmware image signature against one of the enrolled boot keys in OTP before executing any user code. JLPiCart does not maintain parallel “unsigned dev” vs “sealed” firmware paths; experimentation and openness are expressed via **policy flags** evaluated by trusted firmware.

#### Boot key slots (OTP)

The RP2350 OTP provides up to **four** boot-key fingerprint slots. JLPiCart defines a simple convention:

- **Slot 0** is reserved for the **JLPiCart Platform Boot Key** (used for official/community builds).
- **Slots 1–3** MAY be enrolled as additional boot signers (publisher keys, owner keys, lab keys).

Enrollment rules:

- Adding a key to an unused slot MUST be performed by **already-trusted firmware** (i.e., firmware that booted through secure boot).
- Enrollment MUST require explicit user confirmation (or a manufacturing tool step) because OTP writes are irreversible.
- Firmware MUST expose the current key-slot validity mask via the API and UI.

Revocation rules:

- A boot key slot MAY be permanently revoked (invalidated) when compromise is suspected.
- Unused slots SHOULD NOT be permanently invalidated if future enrollment is desired.

#### Optional lockdown properties (OTP)

Signed boot is sufficient to prevent “reflash and run custom firmware” attacks in normal operation. Additional OTP properties MAY be enabled to reduce attack surface:

- **Disable SWD/debug** access.
- **Disable unused boot options** (e.g., USB/UART boot modes) to reduce external entry points.
- **Anti-rollback counters** (monotonic) for firmware version gating.
- **Encrypted boot** for code-at-rest protection (keys in OTP, soft-locked after use).

These are independent properties. The platform MUST treat them as self-describing capabilities and MUST NOT introduce extra abstract “states” to represent them.

### Security posture properties and policy flags (normative)

JLPiCart distinguishes:

- **Posture properties**: read-only facts derived from OTP and the boot ROM configuration.
- **Policy flags**: mutable operational rules stored in flash but authenticated (signed and/or MACed) so they cannot be silently modified.

#### Posture properties (OTP-derived)

Posture properties MUST be computable at boot and exposed to the system UI and API:

- `secure_boot_enabled` — boot ROM signature checks are enforced.
- `otp_device_secret_present` — a device secret seed exists (used for deriving storage encryption keys and device identity).
- `boot_key_valid_mask` — bitmask of enrolled boot-key slots (bits 0–3).
- `debug_disabled` — SWD/debug is disabled by OTP configuration.
- `usb_boot_disabled` — USB BOOTSEL/picoboot is disabled as a boot option.
- `uart_boot_disabled` — UART boot is disabled as a boot option.
- `anti_rollback_enabled` — monotonic firmware rollback protection is enabled.
- `encrypted_boot_enabled` — encrypted boot flow is enabled for firmware images.

#### Policy flags (authenticated flash configuration)

Policy flags are stored in the System Settings namespace in flash and MUST be authenticated. If the policy document is missing or invalid, the platform MUST fall back to a safe default (no unsigned installs; no key enrollment).

Policy flags SHOULD be explicit and self-explanatory. At minimum:

- `allow_usb_collection_install` — allow installing Collections from USB media.
- `allow_network_collection_install` — allow installing Collections from network sources.
- `allow_unsigned_collections` — allow installing Collections without a valid publisher signature.
- `allow_user_replace_collections` — allow users to remove/replace installed Collections.
- `require_publisher_signature` — require a valid publisher signature chain for installs/updates.
- `allow_boot_key_enrollment` — allow enrolling additional boot keys into OTP slots 1–3.
- `allow_boot_key_revocation` — allow invalidating an enrolled key slot.
- `expose_stable_device_id` — allow exposing a stable device identifier to titles (otherwise prefer scoped IDs).


##### Policy flags bit assignments (v1)

For API exposure (`PolicyFlagsResp.policy_flags`), the following bit assignments are normative:

- bit 0: `allow_usb_collection_install`
- bit 1: `allow_network_collection_install`
- bit 2: `allow_unsigned_collections`
- bit 3: `allow_user_replace_collections`
- bit 4: `require_publisher_signature`
- bit 5: `allow_boot_key_enrollment`
- bit 6: `allow_boot_key_revocation`
- bit 7: `expose_stable_device_id`
- bits 8..31: reserved (MUST be zero in v1)

Authentication of policy:

- The policy document MUST be signed by an authorized signer. By default, any firmware signer that is valid for secure boot MAY be treated as authorized for policy, unless the firmware embeds a stricter rule (e.g., platform-only).
- The policy signature MUST cover a canonicalized representation of the policy document (see the configuration canonicalization contract).


### Key hierarchy and roles

Security is based on a strict key-role model. Keys must not be reused across roles.

**Boot Root Key(s) (BRK)**
- purpose: authorize bootloader/firmware images
- storage: public key fingerprint(s) in OTP
- rotation: multiple fingerprints may be provisioned, with explicit revocation rules
- usage: used only for signature verification during boot

**Firmware Signing Key(s) (FSK)**
- purpose: sign firmware images and system services
- storage: offline (publisher/manufacturer)
- revocation: via OTP fingerprint invalidation and/or server policy

**Publisher Root Key(s) (PRK)**
- purpose: certify Publisher Identity Certificates (PICs) and authorize publisher Content Signing Keys under a lock policy
- storage: pinned by the platform (in firmware and/or OTP depending on policy)
- rotation: supported; requires revocation distribution strategy (offline list + optional online checks)

**Publisher Identity Certificate (PIC)**
- purpose: bind a stable Publisher ID and metadata to one or more public keys and permitted usages (e.g., content signing)
- distribution: carried inside Provisioning/Install bundles and may be cached locally
- validation: verified against PRK and checked against revocation rules

**Content Signing Key(s) (CSK)**
- purpose: sign Collections / Payloads / Manifests
- storage: offline (publisher) or managed service
- separation: CSK must never be able to sign firmware

**Device Identity Key (DIK)**
- purpose: uniquely identify a cartridge online and authenticate messages (scores, purchases, device-to-device sessions)
- type: asymmetric keypair (recommended); private key never leaves device
- storage: secure storage (OTP-backed or derived and protected by secure boot + TrustZone)
- rotation: allowed, but must preserve continuity via server-side enrollment flow

**Storage Master Key (SMK)**
- purpose: encrypt user data and local content at rest
- type: symmetric
- storage: derived from OTP secret(s) and/or provisioned secret; never readable outside Secure world
- lifecycle: generated at provisioning; used to derive per-namespace keys via HKDF

**Session Keys**
- purpose: encrypt network sessions and device-to-device sessions
- derived via authenticated key agreement (see below)
- short-lived; stored only in RAM; zeroized after use

### Provisioning contract

Provisioning is a controlled operation that prepares a device for use and (optionally) hardens it. It covers:

- establishing a **Device Identity Key (DIK)** and **Device ID**
- initializing persistent namespaces in flash
- writing an initial **authenticated policy document**
- optionally enrolling boot keys and other OTP properties

Provisioning actions are grouped by what they touch:

**Flash-only provisioning (reversible)**
- initializes the flash layout and namespaces
- installs a default Collection (optional)
- writes the initial policy document (authenticated)
- records install receipts/logs

**OTP provisioning (irreversible)**
- enrolls one or more boot key fingerprints into OTP slots
- enables secure boot enforcement
- programs a device secret seed (if used for storage encryption key derivation)
- enables anti-rollback counters (optional)
- disables debug/boot options (optional)

The firmware MUST expose a read-only **Security Status** record to both Menu and tools consisting of:
- posture properties (OTP-derived)
- a digest of the current policy document
- the boot-key validity mask (slots 0–3)


### Secure boot and anti-rollback contract

**Boot verification**
- On every boot, the device MUST verify the next-stage image signature against BRK fingerprints stored in OTP.
- If verification fails, the device MUST enter BOOTSEL recovery and refuse to boot unsigned code.

**A/B images**
- Firmware MUST be stored in at least two slots (A/B) with independent validity markers.
- Boot selection MUST prefer the newest valid slot that satisfies anti-rollback constraints.

**Anti-rollback**
- If anti-rollback is enabled, the device MUST maintain an OTP-backed monotonic version counter (or equivalent) for firmware.
- If anti-rollback is enabled, installing a firmware with version < current MUST be rejected.
- Whether anti-rollback is enabled MUST be visible via posture properties.

### Storage encryption contract

Storage encryption is defined as **at-rest** protection and integrity, not DRM against runtime bus sniffing.

Namespaces:
- `SYSTEM`: platform configuration, device capability record, network configuration
- `COLLECTIONS`: encrypted cached content and metadata
- `PROFILES`: per-user data, settings, entitlements
- `SAVES`: per-game saves and state
- `GUEST`: ephemeral guest data (may be unencrypted if explicitly configured, but defaults to encrypted)

Rules:
- Each namespace MUST have an independent derived key from SMK (HKDF with namespace label + version).
- Records MUST be authenticated (AEAD or encrypt-then-MAC).
- Writes MUST be power-loss safe via atomic commit (journal, copy-on-write, or double-buffer with checksum + sequence number).
- Guest sessions MUST NOT be able to read data from other namespaces even if encryption is disabled for guest cache.

### End-to-end network security contract

The ESP32 is treated as an **untrusted transport**. The RP2350 must assume:
- the RP↔ESP link may be monitored or modified
- ESP firmware may be swapped or compromised
- therefore, application confidentiality/integrity must not depend on TLS terminating on the ESP32

**Mandatory rule**
- All sensitive traffic to servers (identity, entitlements, scores, updates) MUST be end-to-end encrypted and authenticated with cryptography that terminates on the RP2350.

Recommended approaches:
- RP2350 runs TLS directly over a raw TCP socket provided by ESP-AT.
- If full TLS is too heavy, an application-layer secure channel MAY be used: Noise-style handshake or ECDH + AEAD with certificate pinning / server public key pinning.

Certificate validation:
- The platform MUST support a pinning mode (server public key or SPKI hash) to avoid reliance on device RTC and a CA bundle.
- If CA validation is used, time must be obtained from a trusted source (e.g., server-provided signed time) and pinned endpoints must still be supported as a fallback.

### Device-to-device secure channel contract

JLPiCart supports secure cartridge-to-cartridge sessions (e.g., local multiplayer, trading, ghost exchange) as an optional feature.

Goals:
- mutual authentication (know which cartridge you are talking to)
- confidentiality and integrity of exchanged data
- replay protection

Protocol contract (v1):
- Each cartridge authenticates with its DIK.
- Session keys are established via authenticated key agreement (e.g., ECDH with signatures or a Noise handshake pattern).
- A session has a unique `session_id` and monotonic message counters.
- Payloads are AEAD-protected and include counters as associated data.
- Sessions expire on inactivity and MUST be rekeyed after a bounded amount of data.

Transport:
- May be routed via server relay, direct WiFi, or any future link, but the security properties must be independent of transport.

### Leaderboards, achievements, and anti-cheat contract

Baseline contract:
- A score submission MUST include a server-issued `run_token` and MUST be signed by the device DIK.
- `run_token` is single-use and expires quickly (minutes).
- The server rejects submissions without a valid signature or with a reused/expired token.

Optional “verified run” contract:
- The server issues a deterministic `seed` bound to the run token.
- The title records an input log (and optional checkpoints).
- Submission includes the input log and any required metadata; the server recomputes score via an emulator/verifier.

The platform provides APIs for:
- requesting run tokens
- signing submissions
- submitting input logs in chunks
- retrieving verification status and leaderboard results

### Update bundle security contract

Firmware updates:
- MUST be signed by FSK and verified on-device before install.
- MUST be installed into the inactive A/B slot, then boot-tested, then committed.
- If anti-rollback is enabled, the device MUST enforce anti-rollback version checks.
- The update process MUST be resumable and power-loss safe.

Collection updates:
- Collections and payloads MUST be signed by CSK (or by a delegated signing key whose chain is trusted by CSK).
- Signed manifests MUST be canonicalized prior to signature (JSON Canonicalization Scheme per RFC 8785 is recommended).
- A device MAY cache decrypted content in RAM during execution, but at-rest caches remain encrypted/authenticated under SMK.

Revocation:
- The platform MUST support a revocation list for:
  - compromised device identities (DIK)
  - compromised signing keys (FSK/CSK) or specific collection versions
- Revocation is enforced server-side and MAY be enforced locally when online. Offline behavior must be defined per product policy.

### Privacy contract

Default principle: collect the minimum needed for the feature.

- Network credentials are stored encrypted under `SYSTEM` namespace.
- Telemetry is opt-in unless required for a security incident response.
- Leaderboard submissions include device identity but should not include personal identifiers unless user opted in.
- The platform MUST provide a “delete local data” operation that wipes `PROFILES`, `SAVES`, and `GUEST` namespaces (cryptographic erase preferred by discarding keys).

### Security diagnostics and user-facing status

The cartridge MUST expose a read-only **Security Status Record** to both Menu and tools:
- posture properties (OTP-derived)
- secure boot enabled? anti-rollback enabled?
- usb boot disabled? debug disabled?
- device identity enrolled?
- last update status and rollback reason (if any)

This record is used to make support and recovery practical without leaking secrets.

## 11. Updates and provisioning

This chapter defines how devices are initialized, provisioned for publishers and users, and updated safely over their lifetime. It includes both content updates (Collections) and firmware updates, and it defines behavior for sealed production devices versus developer units.

### 11.1 Update, initialization, and provisioning workflow contract (v1)

**Goal:** Collections and firmware can be installed and updated safely (including on devices with stricter OTP lockdown properties), without bricking devices, and without accepting unauthenticated code.

This contract covers three concerns:

- **Authenticity**: only approved signers can install code/content on a sealed unit.
- **Recoverability**: power loss during install/update must not brick the unit.
- **Policy**: the platform can be distributed in multiple trust postures (open, publisher-locked, service-locked) with explicit recovery rules.

#### Artifact types

- **Collection Update Bundle**: installs/updates one Collection (and its Payloads/assets).
- **Firmware Update Bundle**: updates the cartridge firmware (bootable image(s) + migrations).
- **Provisioning Bundle**: initializes a blank unit and MAY also install a default Collection and/or set a lock policy.

All bundles MUST include:

- a manifest describing contents, versions, target hardware constraints, and expected persistent-data migrations,
- signatures covering both the manifest and all payload bytes,
- an explicit rollback policy section: “allowed”, “only within major”, or “forbidden” (device with stricter OTP lockdown propertiess SHOULD forbid firmware rollback).

#### Security posture and policy model

Install and update decisions are governed by:

- **Boot trust**: only firmware that passes RP2350 secure boot may execute.
- **Posture properties** (OTP-derived): e.g., whether secure boot is enabled, whether debug/USB boot is disabled, which boot keys are enrolled.
- **Policy flags** (authenticated flash configuration): e.g., whether unsigned Collections are allowed, whether users may replace Collections, whether boot-key enrollment is permitted.

The device MUST reject installs/updates that violate policy. The device MUST treat missing/invalid policy as “safe default” (no unsigned installs; no key enrollment).

#### Trust anchors and signer roles (install-time)

Install and update decisions rely on distinct trust anchors:

- **Boot trust** (firmware images): Boot Root Key fingerprints in OTP (BRK). Only firmware authorized by an enrolled boot key fingerprint is accepted.
- **Publisher trust** (content bundles): either platform-approved content keys *or* publisher keys certified by a Publisher Root Key (PRK), depending on lock policy.
- **Service trust** (optional): a pinned service identity for online challenge/activation flows, if used.

A Provisioning Bundle MAY install/enable a specific lock policy (open / publisher-only / platform-only / platform+publisher) but MUST NOT weaken an existing restrictive policy.

#### Initialization and provisioning (first-time setup)

**Initialization** prepares a blank or wiped device for use by creating persistent namespaces and writing initial authenticated configuration.

During initialization, the device MUST:

- establish a **Device Identity Key (DIK)** (private key never leaves the device),
- initialize persistent namespaces (System Settings, Profiles, Saves, Caches, Logs) and monotonic counters,
- record an immutable **Device ID**,
- set an initial lock policy (default: open, unless a Provisioning Bundle specifies otherwise).

Initialization SHOULD avoid OTP writes unless explicitly performing boot-key enrollment or enabling secure boot as part of a controlled provisioning procedure. OTP writes are irreversible and must be treated as a manufacturing/service operation.

**Provisioning** is installation of one or more Collections and (optionally) a lock policy and/or firmware, using a Provisioning Bundle on removable media.

##### USB provisioning media layout

A removable media root is treated as “provisioning-capable” if it contains:

`/JLPICART/INSTALL/<install_id>/`

Each `<install_id>` directory is an **Install Intent**. An Install Intent MUST contain:

- `install.json` — metadata and declared targets (Publisher ID, Collection ID/version, required capabilities, install mode).
- `publisher.cert` — Publisher Identity Certificate (and certificate chain if needed).
- `collection.bundle` — the Collection bytes (manifests + payloads + assets) or a pointer manifest for staged install.
- `collection.sig` — signature covering the canonical digest of `collection.bundle` and `install.json`.

An Install Intent MAY also include:

- `firmware.bundle` + `firmware.sig` — a BRK-signed Firmware Update Bundle (never publisher-signed-only).
- `revocations.json` — optional offline revocation list updates (platform-provided).
- `content.keys` — optional encrypted content key envelopes (see “Encrypted content” below).

##### Provisioning acceptance rules

When provisioning media is inserted, the device MUST:

- enumerate Install Intents deterministically (stable ordering),
- verify publisher identity and bundle signatures before any installation,
- apply capability validation (resource allocator) before committing an install.

A device with stricter OTP lockdown properties MUST reject any Install Intent that is not permitted by its lock policy and MUST reject firmware that is not authorized by Boot trust.

If policy allows unsigned installs, the device MUST warn clearly and MUST record that the install was not authenticated.

##### Install receipts (audit trail)

For each Install Intent, the device MUST append an **Install Receipt** to a local, append-only log containing:

- timestamp (server time if available, else monotonic counter),
- bundle digests, Publisher ID, Collection ID/version,
- decision: installed/rejected and a stable reason code.

Receipts are diagnostic data and MUST NOT be required to boot.

##### Encrypted content (optional)

If Collections are distributed encrypted-at-rest on removable media, key delivery MUST use one of these models:

- **Device-wrapped keys (offline-friendly):** content keys are encrypted to the device DIK public key.
- **Service-wrapped keys (retail-friendly):** the device authenticates online using DIK and retrieves content keys after verifying the install intent.

The device MUST NOT accept raw content keys in plaintext from removable media.

#### Atomicity and recovery (firmware)

Firmware updates MUST be robust against power loss. The recommended model is A/B firmware slots:

- the update is written to the inactive slot and verified,
- the slot is marked **pending**,
- on next boot, the pending slot runs in **trial** mode,
- the running firmware MUST explicitly **commit** once it reaches a healthy state; otherwise the boot process MUST roll back to the last committed slot.

This MUST work even if the user removes power during any stage.

#### Atomicity and recovery (collections)

Collection installs/updates MUST be atomic with respect to the “active Collection”:

- the cartridge MUST stage updates into a new directory/partition namespace (or a new content-addressed set),
- the system MUST flip a single “active pointer” only after all content and manifests verify,
- if power is lost, the device MUST either continue using the old Collection or complete the new one after verification; it MUST NOT be left in a half-installed state.

#### Anti-rollback policy

If anti-rollback is enabled, the device SHOULD prevent downgrade attacks (e.g., monotonic counters or version floors):

- the unit MUST reject firmware bundles older than the last committed version,
- and MAY reject Collection bundles older than the last committed Collection version for that Collection ID.

Anti-rollback MUST be configured so that publisher support and recovery policies remain possible (e.g., “emergency signer can still ship a higher version”).

#### USB boot and physical update vectors (sealed units)

If a device with stricter OTP lockdown properties is expected to resist casual physical attacks, the project MUST explicitly decide whether USB-boot entry points remain enabled after sealing.

Publicly documented RP2350 attack results include extraction attacks that rely on the device running non-secure code while USB boot interfaces are available; disabling USB boot interfaces via OTP flags is cited as a mitigation, but it removes those update paths. Therefore, device with stricter OTP lockdown propertiess SHOULD either:

- disable USB boot interfaces and rely on signed A/B updates, or
- keep USB boot interfaces enabled but treat the unit as “sealed but serviceable” with a weaker physical threat posture.

This choice MUST be visible in the lock policy.

#### Key lifecycle (rotation, revocation, recovery)

The trust policy MUST support:

- adding additional trusted signing keys (planned rotation),
- revoking compromised keys (denylist),
- a recovery story if a publisher key is lost:
  - either “platform can recover via an emergency signer”, or
  - “units are stranded” must be stated explicitly (discouraged for general distribution).

Normal Collection updates MUST NOT require OTP writes (OTP writes are manufacturing/sealing operations only).

## 12. Online services

This chapter specifies the workflow-level contracts for interacting with network services. The key rule is that the ESP32 is treated as **untrusted transport**: confidentiality and integrity MUST be enforced end-to-end by the RP2350 when required.

### 12.1 Online service workflow contract (v1)

This contract governs optional online features. Offline behavior is always defined.

**Secure transport**

The ESP32 is treated as an untrusted transport. Secrets MUST be protected end-to-end between RP2350 and server.

**Identity**

- The cartridge MUST have a device identity used for authenticating to the service.
- User identities are optional and exist only when the user opts in.

**Leaderboards**

A leaderboard submission MUST include:

- a fresh server-issued run token,
- a device signature binding token + submission payload,
- optional replay proof data when the title opts into server verification.

The server MUST reject replays, expired tokens, and unsigned submissions.



### 12.2 Service API (HTTP) contract (v1)

This section defines a concrete, minimal REST-like protocol so a third party can implement a compatible server.

#### Transport and trust

- The device MUST establish a TLS connection directly from the RP2350 to the service endpoint. The ESP32 MUST be treated as a byte-forwarding modem only.
- The device MUST perform server authentication by **pinning** either:
  - the service public key (preferred), or
  - the service certificate SPKI hash.
- The device SHOULD use TLS 1.2+ and SHOULD disable insecure ciphersuites.

#### Base URL and versioning

All endpoints are under a single service origin, for example `https://api.jlpicart.dev/`.

Paths are versioned: `/v1/...`.

#### Common request headers

Requests are JSON unless otherwise specified.

- `Content-Type: application/json`
- `X-JLP-DeviceId: <device_id>`
- `X-JLP-Session: <session_token>` (if authenticated)
- `X-JLP-Nonce: <client_nonce>` (random per request; 16 bytes hex)

#### Common response envelope

All responses MUST conform to this envelope:

```json
{
  "ok": true,
  "data": { }
}
```

On error:

```json
{
  "ok": false,
  "error": {
    "code": "ERR_INVALID_TOKEN",
    "message": "Human readable message",
    "retryable": false
  }
}
```

Error `code` is stable and machine-readable.

#### Endpoint: device hello / session

`POST /v1/device/hello`

Purpose: establish a short-lived session token and obtain server parameters.

Request:

```json
{
  "fw_version": "1.2.3",
  "device_pubkey": "<base64>",
  "caps": {
    "wifi": true,
    "storage": true,
    "opl4": false
  }
}
```

Response (`ok=true`):

```json
{
  "ok": true,
  "data": {
    "session_token": "<opaque>",
    "server_time": 1767225600,
    "policy": {
      "max_payload_bytes": 1048576,
      "rate_limit": { "rpm": 30 }
    }
  }
}
```

The server MUST treat `device_pubkey` as the long-term device identity key (DIK) public key. The cartridge MUST sign any privileged requests with the DIK private key and the server MUST verify signatures.

#### Endpoint: run token issuance (leaderboards)

`POST /v1/leaderboards/run/start`

Request:

```json
{
  "game_id": "publisher.game",
  "board_id": "default",
  "ruleset": "v1",
  "mode": "ranked"
}
```

Response:

```json
{
  "ok": true,
  "data": {
    "run_token": "<opaque>",
    "seed": "<base64>",
    "expires_in_s": 300
  }
}
```

The server MUST generate `run_token` as single-use, expiring, unpredictable data.

#### Endpoint: submit run result

`POST /v1/leaderboards/run/submit`

Request:

```json
{
  "run_token": "<opaque>",
  "game_id": "publisher.game",
  "board_id": "default",
  "score": 123456,
  "duration_ms": 98765,
  "proof": {
    "type": "none"
  },
  "sig": {
    "alg": "ed25519",
    "signed": "<base64>", 
    "signature": "<base64>"
  }
}
```

The `signed` field is the canonical JSON (RFC 8785) of the object without `sig`. The server MUST verify the signature with the enrolled device key.

Optional replay verification proof:

```json
"proof": {
  "type": "input_replay_v1",
  "seed": "<base64>",
  "input_log": "<base64>",
  "checkpoints": "<base64>"
}
```

The server MAY validate by deterministic replay. If replay verification is enabled for a board, the server MUST ignore the submitted `score` and compute it.

#### Endpoint: achievements and stats

`POST /v1/stats/set`

Purpose: update per-user stats and unlock achievements. This is inspired by “stats and achievements” systems such as Steamworks, but is intentionally simpler.

Request:

```json
{
  "user_id": "<opaque-user-or-guest>",
  "game_id": "publisher.game",
  "stats": { "kills": 10, "deaths": 2 },
  "achievements": ["FIRST_WIN"]
}
```

Response contains authoritative server state.

**Reference (inspiration)**

- Steamworks Stats and Achievements overview: https://partner.steamgames.com/doc/features/achievements

#### Endpoint: revocation lists

`GET /v1/revocation/publishers`
`GET /v1/revocation/devices`

Returns a signed JSON blob listing revoked publisher certificates and revoked device IDs. The cartridge MUST enforce revocation for sealed units.

#### Endpoint: update metadata (firmware)

`GET /v1/updates/firmware/latest?board_id=<id>`

Returns a signed update descriptor (version, bundle hash, download URL). Actual update delivery MAY still be via USB stick (offline-first), but this endpoint enables “check for updates”.

### 12.3 Rate limits and abuse controls

The server SHOULD apply rate limits per device ID and per IP. The device MUST implement exponential backoff on retryable errors.

### 12.4 References (security + transport)

- RP2350 security features overview (secure boot, encrypted boot, OTP, TrustZone): https://pip-assets.raspberrypi.com/categories/1260-security/documents/RP-009377-WP-1-Understanding%20RP2350_s%20security%20features.pdf
## 13. Diagnostics and conformance

Diagnostics are not an afterthought: the platform must be able to explain why a launch failed, why a device combination is invalid, and why an install or update was rejected. This chapter defines the minimum diagnostic surface and how conformance can be validated.

### 13.1 Diagnostics contract (v1)

To reduce “it doesn’t work” ambiguity, the platform MUST provide:

- a structured error code model for preflight/launch failures,
- a minimal diagnostic log accessible from the Menu,
- a way for publishers to obtain logs without exposing user secrets (sanitized export).


### 13.2 Reference toolchain for Z80 and emulator-driven tests

To keep the Z80 toolchain and emulator behavior stable over time, the project uses pinned tools that are obtained by the build (and CI) and **must not** rely on system installations:

- **openMSX 21.x**: built from the `third_party/openMSX` git submodule and executed via `tools/openmsx/bin/openmsx`.
- **SDCC 4.5.x**: downloaded as a prebuilt tarball (URL + SHA256 pinned in `tools/lock.yml`) and executed via `tools/sdcc/bin/sdcc`.

All emulator-driven tests in this repository are defined against **Linux amd64 + openMSX 21.x + SDCC 4.5.x**. If other versions are used locally, they are not considered normative for conformance.

## 14. Open investigations and references

### 14.1 Open investigations

The following requirements are confirmed but their implementation approach requires dedicated research before specification. Security investigations are highest priority as they have architectural implications and involve irreversible hardware operations.

### Security (remaining open items)

- **Seal level policy**: decide the default sealed mitigations for your chosen RP2350 boot ROM revision (e.g., whether to disable USB PICOBOOT/MSD interfaces by default) and document the user-visible trade-off with recovery/update UX.
- **Key rotation and revocation UX**: define how key compromise is communicated to users, and what offline behavior is acceptable when a revocation list cannot be fetched.
- **TLS / secure channel implementation choice**: choose the concrete library/handshake format for RP2350→server end-to-end encryption and define memory/time budgets on the non-bus core.
- **Verified run tooling**: decide whether you will provide a reference emulator/verifier for deterministic replay validation, or only baseline signed-token submissions.
- **Key enrollment safety**: define safeguards to prevent accidental OTP writes (boot keys, debug disable) during development and manufacturing.

### Networking and Connectivity

- **ESP32 AP+STA mode**: confirm whether the esp-at firmware version pinned in the repo supports simultaneous access point and station mode. Determine bandwidth sharing characteristics, maximum number of AP clients, and stability under load.
- **Local device discovery**: how do two JLPiCart units find each other on a local network without a router? Evaluate mDNS, UDP broadcast, and AP+STA topology (one unit becomes the host AP). Define the discovery protocol.
- **Offline direct multiplayer**: two cartridges, no internet, no router. One acts as AP, the other connects as STA. Define the session setup flow via the JLPiCart API so games do not need to manage this themselves.
- **Mesh networking**: for tournament or multi-player sessions with more than two units. Investigate whether the ESP32 supports multi-hop mesh (e.g., ESP-MESH or similar). Define maximum node count and latency characteristics.
- **Legacy networking compatibility strategy**: decide whether to implement MSX-UNAPI (Ethernet + TCP/IP) in ROM, and whether to support the TCP/IP UNAPI v1.1 optional TLS routines. Define discovery and call-gate behavior and how it coexists with the JLPiCart API.
- **Cloud service API**: define the protocol between cartridge and cloud service for profile sync, high scores, ghost data, guest sessions, and online Collection updates. Requires coordination with the service operator.
- **Network latency budget for MSX software**: determine the maximum acceptable round-trip latency for JLPiCart API network calls (and, if enabled, MSX-UNAPI socket calls), given the constraints of Z80 timing and game loop expectations.

### Device Emulation

- **OPL4 full emulation**: FM synthesis (OPL3, 36 operators) + PCM wavetable layer on RP2350 at MSX bus speeds. Performance budget analysis required. Reference: Nuked-OPL3 and similar open source implementations. Determine whether cycle-accurate emulation is feasible or whether approximations are acceptable.
- **VDP emulation scope**: V9938/V9958 emulation on RP2350. Determine cycle accuracy requirements, VRAM size, performance budget, and which video modes are required versus optional.
- **Floppy disk swap mechanism**: research how existing cartridges (Carnivore2, MegaFlashROM, FlashJacks) handle virtual disk swapping for multi-disk software. Define the JLPiCart mechanism based on findings.
- **Disc image format**: define the standard format for optical disc images within a Collection. Evaluate ISO 9660, raw sector dumps, and any MSX-specific CD formats against real MSX CD game requirements.
- **SymbOS compatibility**: confirm the minimum requirements (MSX2+, memory mapper size, MSX-DOS2/Nextor presence). Decide how JLPiCart provides mass storage and networking to SymbOS: by writing dedicated SymbOS drivers or by emulating existing supported hardware.
- **MSX host RAM detection**: how the cartridge reliably detects available host RAM at boot across all MSX generations, to determine how much additional RAM the cartridge must provide.
- **Resource budget model**: define the RP2350 resource budget (RAM, CPU cycles, DMA channels, PIO state machines) and the validation algorithm the cartridge uses to accept or reject a device combination declared in a Manifest.

### 14.2 References and inspiration sources

These links are included to ground implementation choices and to make future investigations faster.

### MSX platform fundamentals

- MSX2 Technical Handbook (Slots & cartridges chapter): https://konamiman.github.io/MSX2-Technical-Handbook/md/Chapter5b.html
- MSX I/O ports + slot/subslot selection overview: https://map.grauw.nl/resources/msx_io_ports.php
- MSX Cartridge slot + primary/secondary slot notes: https://www.msx.org/wiki/MSX_Cartridge_slot
- Switchable I/O ports concept (port 40h): https://www.msx.org/wiki/Switchable_I/O_ports
- Discussion: why MSX I/O port space collides (8-bit decoding quirks): https://www.msx.org/forum/msx-talk/openmsx/io-ports-are-quite-messy-detect-openmsx?page=4
- MSX-DOS 2 program interface specification (environment): https://map.grauw.nl/resources/dos2_environment.php
- MSX2 Technical Handbook appendix I/O map: https://konamiman.github.io/MSX2-Technical-Handbook/md/Appendix6.html
- MSX I/O map (Portar): https://www.msx.org/wiki/Portar

### MSX networking standards (UNAPI / InterNestor Lite)

- MSX-UNAPI overview: https://github.com/Konamiman/MSX-UNAPI-specification/blob/master/docs/Introduction%20to%20MSX-UNAPI.md
- TCP/IP UNAPI specification: https://github.com/Konamiman/MSX-UNAPI-specification/blob/master/docs/TCP-IP%20UNAPI%20specification.md
- UNAPI repository + TCP/IP UNAPI v1.1 notes (incl. TLS routine mention, InterNestor Lite mapper support note): https://www.msx.org/news/en/unapi-repository-tcpiunapi-11-internestor-21-msrcom-obsonet-bios-13-from-konamiman
- Discussion: Ethernet UNAPI vs TCP/IP UNAPI and which devices provide which: https://www.msx.org/forum/msx-talk/hardware/obsonet-vs-denyonet-vs-gr8net
- InterNestor Lite (project overview, feature list): https://sourceforge.net/projects/internestor/
- InterNestor Lite control program docs (pause/resume, behavior): https://github.com/Konamiman/MSX/blob/master/SRC/INL/DOCS/control-program.md
- ObsoNET user manual note (INL2.COM usage for TCP/IP via InterNestor Lite): https://www.konamiman.com/msx/obsonet/onetm-e.txt

### SymbOS (MSX) drivers and expectations

- SymbOS mass storage driver sources (MSX): https://github.com/Prodatron/symdrv-msx-massstorage
- SymbOS 3.0 announcement (network daemon, supported network hardware): https://www.msx.org/news/en/the-final-version-of-symbos-30-is-ready-for-download
- SymbOS 3.0 beta announcement (DenYoNet/GR8NET networking mention): https://www.msx.org/news/en/symbos-30-beta-release-0
- Forum note on SymbOS networking assumptions (hardware TCP stack; network daemon sources): https://www.msx.org/forum/msx-talk/development/looking-for-symbos-quigs-documentation?page=6
- Forum note on MSX-DOS2/Nextor + mapper expectations for SymbOS setup: https://www.msx.org/forum/msx-talk/emulation/msx-dos-and-symbos-how-to-get-it-work

### Steamworks inspiration (stats/achievements/leaderboards)

- Steamworks: Stats and Achievements overview: https://partner.steamgames.com/doc/features/achievements
- Steamworks API: ISteamUserStats interface (stats/achievements/leaderboards): https://partner.steamgames.com/doc/api/isteamuserstats

### Nextor / MSX-DOS2 boot and storage

- Nextor 2.1 user manual: https://github.com/Konamiman/Nextor/blob/v2.1/docs/Nextor%202.1%20User%20Manual.md

## Appendix A. Writing and maintenance conventions

Yes, this will turn into a book if you let it. The trick is that the “book” should be organized as a small number of normative specs plus reference guides. The product definition stays narrative and conceptual; the contracts become the authoritative ABIs/formats; and everything else (tutorials, examples, code snippets) lives as reference docs.



### Security references

- RP2350 Datasheet (Boot ROM and Security chapters): https://pip.raspberrypi.com/documents/RP-008373-DS-2-rp2350-datasheet.pdf
- RP2350 Product Brief (security feature summary): https://datasheets.raspberrypi.com/rp2350/rp2350-product-brief.pdf
- Understanding RP2350’s security features (whitepaper): https://pip-assets.raspberrypi.com/categories/1260-security/documents/RP-009377-WP-1-Understanding%20RP2350_s%20security%20features.pdf
- Raspberry Pi: RP2350 Hacking Challenge results (errata and mitigations): https://www.raspberrypi.com/news/security-through-transparency-rp2350-hacking-challenge-results-are-in/
- Raspberry Pi: RP2350 A4 / RP2354 and boot ROM security errata fixes: https://www.raspberrypi.com/news/rp2350-a4-rp2354-and-a-new-hacking-challenge/
- USENIX WOOT paper “Tales from the RP2350 Hacking Challenge”: https://www.usenix.org/system/files/woot25-muench.pdf

### Canonicalization and signing references

- RFC 8785 JSON Canonicalization Scheme (JCS): https://www.rfc-editor.org/rfc/rfc8785

### ESP-AT references

- ESP-AT TCP/IP AT Commands (TCP/SSL, notes on passthrough constraints): https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/TCP-IP_AT_Commands.html

## Appendix B. Reference repository layout and module map

This appendix is **non-normative**. It exists to make the contracts in this book easier to implement and to keep the codebase maintainable as the project grows.

The core idea is to keep a small number of “spine” modules stable over time, and to hang everything else off those modules via descriptors and contracts rather than cross-cutting conditionals.

### B.1 Repository layout

The repository is expected to contain:

- `spec.md` — this specification book (normative).
- `bootstrapping.md` — staged implementation plan (non-normative, but operationally important).
- `old_src/` — a snapshot of legacy firmware sources kept only as reference (not built).
- `fw/` — firmware build system and new firmware sources.
- `doc/` — hardware documentation.

The `old_src/` directory exists to preserve prior work while ensuring the new design is not constrained by old module boundaries.

### B.2 Firmware module map

The recommended new firmware tree under `fw/src/` is:

- `spine/`
  - `security_posture.*` — reads OTP once at boot and exposes **security posture properties** (secure boot enforced, enrolled boot keys, debug disabled, OTP locks, monotonic counters).
  - `policy_store.*` — loads and verifies the **signed policy/config**, exposes policy flags and a policy digest.
  - `capability_registry.*` — owns the **Declared → Allowed → Activated** pipeline and is the single query surface for “what exists and is permitted”.
  - `board_descriptor.*` — provides the board’s **hardware declared candidates** (and safe verification rules).
  - `driver_descriptors.*` — provides the firmware’s **software declared candidates** (compiled-in descriptor table; may be generated at build time).
  - `activation.*` — activation helpers: hardware verification (when safe) and software instantiation hooks.
- `diag/`
  - `diag.*` — diagnostic codes + structured error reporting used by Menu and API.
- `log/`
  - `log.*` — bounded, non-blocking logging usable outside the bus loop.
- `msx/`
  - `bus_loop.*` — timing-critical MSX bus loop (kept minimal).
  - `slot_map.*` — deterministic mapping of memory pages / ports for Activated peripherals.
  - `api_window.*` — JLPiCart API memory-mapped window implementation (ring buffers, doorbell, services).
  - `menu_host_abi.*` — Menu mailbox page and host-side implementation.
- `storage/`
  - `flash_layout.*` — partitions / namespaces for system state, policy/config, collections, profiles, logs.
  - `kv_store.*` — small atomic KV store used for system settings and indexes.
  - `append_log.*` — append-only log used for install receipts and auditable events.
- `content/`
  - `collection_format.*` — collection bundle format (layout, hashing, signature envelope).
  - `manifest_parser.*` — manifest/config parsing + merge semantics.
  - `launch_plan.*` — turns a payload request into a launch plan (requested capabilities + resource needs).
  - `installer_usb.*` — reads USB install bundles and commits them atomically into storage.
- `peripherals/`
  - `peripheral_manager.*` — coordinates activation, verification, allocation, and MSX-visible mapping.
  - `descriptors/` — each peripheral adds a descriptor file (no scattered `#defines`).
- `net/`
  - `transport_esp_at.*` — ESP32-AT “dumb pipe” transport (TCP sockets).
  - `crypto_channel.*` — end-to-end crypto/TLS owned by RP2350 (ESP32 treated as untrusted).
- `update/`
  - `firmware_update.*` — signed firmware install workflow (if implemented).
  - `provisioning.*` — initialization/provisioning bundle handling (publisher certs, receipts).
- `tests/`
  - host tests for policy verification, canonicalization, ring framing, determinism.

This map is intentionally opinionated: it is designed to keep “what the device can do” (posture), “what it is allowed to do” (policy), and “what it will do for this run” (activation) separate and easy to reason about.

### B.3 The spine rule

All high-level features (menu, installs, peripherals, network services) MUST be implemented by querying:

- `SecurityPosture` (OTP facts),
- `Policy` (signed policy flags),
- `CapabilityRegistry` (declared/allowed/activated).

No other module should read OTP directly, parse policy directly, or probe hardware directly. Hardware probing is restricted to “verification” and only runs for capabilities that are both declared and allowed and explicitly requested, and only when the board descriptor marks probing as safe.

