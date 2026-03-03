# JLPiCart Requirements

## 1. Vision

JLPiCart is an open collection of tools (PCB design, firmware, and specifications) built around the RP2350B microcontroller for anyone who wants to publish software for the MSX ecosystem.

It is not meant as a flash cartridge for end-users. Carnivore2, MegaFlashROM, FlashJacks, and Pico+ already cover that space well. JLPiCart is for publishers. It provides everything needed to design and distribute inexpensive, purpose-built MSX cartridges with wide hardware support and a coherent end-user experience, without building any of that infrastructure from scratch. Mappers, RAM expansion, audio emulation, floppy and mass storage, networking, user identity, save management, and high score sync are all provided by the platform. A publisher focuses on the software. JLPiCart handles the rest.

The platform is fully open source. Publishers who need capabilities beyond the standard feature set can modify the firmware and PCB design freely. Anything built on JLPiCart can be published and shared. Everything works offline. Network features are always additive and never required.

## 2. Glossary

**Collection** — A named bundle containing a Manifest, one or more Payloads, and optional Assets. Only one Collection is active at a time.

**Payload** — A single emulatable media item within a Collection, consisting of its Content, a Manifest, and optional Assets. A Collection may have multiple Payloads active simultaneously (e.g., a ROM plus a floppy bundle plus a mass storage image).

**Content** — The primary data of a Payload: a ROM image, a mass storage image, or a floppy bundle (one or more floppy disk images).

**Manifest** — Structured textual metadata describing a Collection or Payload. Includes identity (name, author, version), hardware requirements (mapper, RAM, audio devices, VDP, network access), licensing terms, and boot defaults. Machine-readable. Hard requirements in a Manifest cannot be overridden by the user; soft defaults can.

**Assets** — Optional non-textual media associated with a Collection or Payload, used by the Menu: images, animations, music.

**Source** — Where a Collection is loaded from. Sources are: internal flash, USB stick, optical drive (CD/DVD), or network. External sources take priority over internal flash.

**System Settings** — Cartridge-wide configuration that persists across Collections: WiFi credentials, video output mode, USB behavior, language, firmware settings.

**User Profile** — A named identity stored on the cartridge, optionally synced to the cloud. Contains language preference, save data, and high scores across all Collections. Multiple profiles may exist on one cartridge. A profile has a local ID and an optional cloud identity (username/password) for sync.

**Guest Session** — A temporary profile tied to a licensed user's Collection. Allows a user without a copy of a Collection to play it, subject to publisher-defined limits (time, sessions, features).

**Persistent Storage** — Flash storage that survives Collection changes: System Settings, User Profiles, save data, high scores, cached network payloads.

**JLPiCart API** — The stable, versioned interface exposed by the cartridge to MSX software. Provides games and applications access to: user identity, save/load operations, high score submission, network sockets, peripheral queries, and multiplayer session management. Designed to be usable from Z80 assembly without pain.

**Menu** — The cartridge's own software, not part of any Collection. Runs via the Z80 stub. Handles Collection selection, System Settings, User Profile management, and publisher tools. Adapts its rendering to the detected MSX generation (text-only on MSX1/TMS9918, richer graphics on MSX2/V9938, best quality on MSX2+/V9958).

---

## 3. Hardware Platform

JLPiCart is a reference design, not a fixed product. Anyone can build their own JLPiCart-compatible board with any combination of peripherals. Capabilities are a function of the board design. The firmware is configured per board at build time via a board definition file.

### Minimum Requirements

The only hard requirement for a JLPiCart-compatible board is:

- **RP2354B** — RP2350B with 2MB integrated flash. Provides dual-core ARM Cortex-M33, 520KB SRAM, and USB.
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

- **OLED display** (e.g., SSD1306, 128×32) — status display.
- **Video output connector** — analog video output (CRT and/or VGA) via PIO. The reference design uses a single shared connector; nothing prevents using two separate connectors.
- **Stereo audio output** — analog audio output for emulated audio devices.
- **GPIO with ADC** — general purpose I/O, temperature sensing, battery voltage monitoring, or custom hardware extensions.

### Reference Design

The JLPiCart reference design targets the **RP2350B** with 16MB external flash and includes all core and situational peripherals listed above. The default firmware build targets this configuration.

---

## 4. System Architecture

### Core Assignment
- **Core 0**: MSX bus loop. Never-returning tight loop handling all memory and IO bus cycles. Latency-critical. Runs from scratch RAM.
- **Core 1**: Task scheduler. Handles peripheral drivers, display updates, WiFi communication, USB, and all non-bus work.

### Bus Abstraction
The bus loop operates on an array of **Cartridge slots**, each with:
- 8 × 8KB memory segments, each with optional direct memory pointer and/or read/write callback
- 256 IO port callbacks (read and write)
- Lifecycle callbacks (init, deinit)

Up to 4 subslots per MSX page are supported via the standard MSX slot expander register at 0xFFFF.

### Cartridge Control Mode
When the cartridge needs to present its own UI — Menu, loading screens — it does so by mapping a program into the MSX address space that runs on the Z80 and uses the host MSX peripherals (VDP, keyboard) for display and input. The implementation mechanism is not prescribed. The requirement is that the cartridge can present a UI and respond to user input using whatever MSX hardware is available on the host machine, across all supported MSX generations.

### JLPiCart API
From the MSX software perspective, the JLPiCart API is a standard peripheral: it is accessed via IO ports, an optional memory-mapped window, and optionally via interrupts. No cartridge control mode is needed. A game or application uses the API the same way it uses any other MSX peripheral. The exact IO port layout and protocol are to be defined, with INL2 compatibility as a hard constraint.

### Payload Source Priority
Payload source priority is configurable as part of System Settings. A board or Collection may lock the priority order — for example, a CD-focused cartridge may fix the optical drive as the highest priority source regardless of user preference.

### USB
The USB port operates in host mode at all times during normal operation. It connects controllers, USB sticks, and optical drives. Content loading (Collections) is done by plugging a USB stick into the host port or via WiFi — never by presenting the cartridge as a USB device to a PC.

USB device mode is only used during initial firmware flashing, handled transparently by the RP2350B bootrom. This is a production/setup operation, not a user-facing feature.

---

## 5. Requirements

### 5.1 Collections and Payloads

- A Collection consists of a Manifest, zero or more Payloads, and optional Assets (artwork, music). Text descriptions are part of the Manifest.
- The Collection format is source-agnostic: the same format is valid on internal flash, USB stick, optical disc, or downloaded from the network.
- Only one Collection may be active at a time.
- A Collection may contain Payloads of mixed types simultaneously (e.g., a ROM + a floppy bundle + a mass storage image all active at once).
- Each Payload has a Manifest specifying required and optional emulated hardware. Hard requirements in a Manifest cannot be overridden by the user; soft defaults can.
- The Manifest has an authored (read-only) section and a user data section (mutable, stored in Persistent Storage).
- Assets included in a Collection are consumed by the Menu but do not affect runtime behavior.
- A Collection may declare a default boot Payload. The user may override this preference per-profile.
- The cartridge boots directly into the last active Collection and Payload, bypassing the Menu. Entering the Menu requires a defined user action (e.g., a key combination or button at boot).

### 5.2 Payload Sources

- **Internal flash**: default source. Collections are loaded onto flash via USB stick or network.
- **USB stick**: detected at boot via USB host. Contains one or more Collections in the standard format.
- **Optical drive (CD/DVD)**: detected at boot via USB host. A disc is treated as a read-only Source containing a Collection.
- **Network**: a Collection or individual Payloads may be downloaded and cached to flash. Guest Sessions deliver temporary Payloads via network. Network boot (cartridge fetches and runs a Collection entirely from network) is a supported mode.
- When multiple sources are present simultaneously, priority order is user-configurable in System Settings. A board or Collection may lock the priority order.
- The Collection format is identical regardless of source.

### 5.3 Device Emulation

The following devices must be emulatable by the cartridge, assignable per Payload via its Manifest:

**Memory Mappers**
- Linear (no mapper)
- Konami (4-bank, 8KB pages)
- Konami SCC (Konami with SCC audio)
- ASCII8 (8KB banking)
- ASCII16 (16KB banking)
- Additional mappers must be addable without architectural changes

**RAM**
- The cartridge provides additional RAM to the MSX, configurable per Collection
- Minimum: 64KB additional RAM (to bring 64KB MSX1 machines to 128KB for SymbOS)
- RAM is mappable into any subslot
- On MSX1 machines with less than 64KB internal RAM, the cartridge must be capable of providing the full 64KB working RAM so that software expecting 64KB operates correctly
- Slot configuration (which subslots carry ROM, RAM, devices) must be fully described in the Manifest and configurable

**Audio**
- PSG (AY-3-8910): internal emulation, selectable per Payload as internal or pass-through to system PSG
- SCC / SCC+: internal emulation, selectable per Payload
- OPL4: full emulation (FM synthesis + PCM wavetable layer). Feasibility study required for RP2350 performance budget.

**Video**
- Video output: CRT and VGA modes share one physical connector. Output mode is a System Setting.
- VDP upgrade emulation: the cartridge may present an enhanced VDP to the MSX (V9938, V9958) for MSX1 host machines, configurable per Collection.
- VDP emulation feasibility and scope require dedicated investigation.

**Floppy Controller**
- Emulates a standard MSX floppy disk controller (WD279x-compatible)
- Serves floppy images from a Floppy Bundle Payload
- Supports read-only and read-write images
- Virtual disk swap mechanism required for multi-disk software. Implementation approach TBD (investigate existing solutions: Carnivore2, MegaFlashROM, FlashJacks).
- Read-write floppy images are persisted to the Payload Source they came from where possible, or to internal flash otherwise.

**Mass Storage / Nextor**
- Presents a Nextor-compatible block device interface
- Serves Mass Storage Payload images (read-only or read-write)
- Internal flash may be partitioned to serve as a Nextor volume (e.g., for SymbOS installation)
- USB sticks connected in host mode may be exposed as additional Nextor volumes
- Enables MSX-DOS 2 / Nextor booting from cartridge

**SymbOS Support**
- The cartridge must be capable of providing everything SymbOS requires to run on any MSX2 machine with 64KB internal RAM:
  - At least 64KB additional RAM (total 128KB minimum)
  - Mass storage device (Nextor block device serving internal flash or USB stick)
  - Appropriate mapper
- Full SymbOS compatibility requires dedicated investigation and a specific Collection configuration.

### 5.4 User Interface

**Menu**
- Runs on the MSX screen. Adaptive rendering based on detected MSX generation and VDP:
  - MSX1 / TMS9918: text mode
  - MSX2 / V9938: enhanced graphics
  - MSX2+ / V9958: best quality
- Functions: Collection selection, Payload selection within a Collection, System Settings, User Profile management, publisher tools (signing, locking).
- Accessing the Menu from a running Collection requires a defined key combination or hardware action.

**OLED Display**
- 128×32 SSD1306 display used for status information: active Collection name, active User Profile, WiFi status, temperature, battery.
- Not used as a primary UI surface (too small for menus).

**Boot Behavior**
- Cartridge boots directly into last active Collection/Payload.
- A defined user action at power-on (TBD: key combination, ESC, hardware button) enters the Menu instead.
- CD boot: if an optical drive is connected and a disc is present, a loading screen is shown while the disc is read, then the Collection boots automatically.

### 5.5 User Profiles and Identity

- Multiple User Profiles may exist on one cartridge.
- Each profile contains: display name, language preference, save data (per Payload), high scores (per Payload), and extended game-specific data (ghosts, replays, etc.).
- Profiles are system-level: they persist across Collection changes.
- Each profile has a local identity and an optional cloud identity (username + password/token) for network sync.
- **Guest Sessions**: a User Profile holder may invite another JLPiCart user to play a Collection they do not own. The guest receives a temporary Payload via network, subject to publisher-defined limits (session count, time, feature restrictions).
- The cartridge acts as **identity provider** for games. Via the JLPiCart API, a game queries the cartridge for active player identities and receives opaque tokens. The game never manages usernames, passwords, or sync logic.
- Multi-player identity: the cartridge supports multiple simultaneous active profiles (for local multiplayer).
- **Consent model**: users have explicit, granular control over what data leaves the device. Sync of saves, high scores, ghosts, and profile data is individually opt-in per profile and per data type.
- All profile data is stored locally. Network sync is optional and additive. The cartridge is fully functional offline.

### 5.6 Connectivity

**WiFi**
- ESP32 co-processor provides WiFi connectivity via AT command interface.
- WiFi credentials are System Settings (global).
- Per-Collection network access is declared in the Manifest and may be restricted by the user.
- The user may disable network access globally or per Collection.

**InterNestorLite (INL2)**
- Full support for the InterNestorLite v2 protocol is a hard requirement.
- The IO port layout of the network device must be INL2-compatible from initial design. Retrofitting is not acceptable.
- INL2 compatibility enables existing MSX networked software to work without modification.

**JLPiCart API — Network Features**
- TCP socket interface exposed to MSX software via the JLPiCart API.
- High score submission and retrieval (to/from cloud service).
- User profile sync (opt-in, per data type).
- Guest Session delivery (temporary Payload download).
- Online Collection updates (publisher pushes updated Collection to cartridge).
- Network multiplayer: two or more JLPiCart units may connect for head-to-head play. Each cartridge manages its own user identity; games use the JLPiCart API for session setup rather than implementing networking themselves.

**Cloud Service**
- An external web service (operated by a community partner) stores user profiles, high scores, ghosts/replays, and Collection metadata.
- The cartridge communicates with this service via INL2 / TCP over WiFi.
- The service and its API are an external dependency and require coordination with the service operator.

### 5.7 USB

- The USB port operates in host mode at all times during normal operation.
- USB HID gamepad/joystick support, mapped to MSX joystick ports (System Setting: controller mapping).
- USB keyboard support, mapped to MSX keyboard matrix (System Setting).
- USB stick support: additional Payload Source and Nextor volume.
- USB optical drive support (CD/DVD as Payload Source).
- Collections are loaded onto the cartridge via USB stick or network. USB device mode is not a user-facing feature; it is only used during initial firmware flashing via the RP2350B bootrom.
- TinyUSB is the USB stack. The existing patch enabling optical drive (bulk-only transport) support must be maintained.

### 5.8 Security and Licensing

**Publisher Locking**
- A publisher may lock a Collection so its contents cannot be modified or extracted.
- Locking is performed by copying a signed folder to the cartridge via USB stick, no custom tooling required.
- The Menu provides a locking/signing option for publishers and developers.
- Locking levels (to be investigated and specified):
  - **UI lock**: content not modifiable via Menu or USB interface. Trivially bypassed physically; suitable for friendly/casual protection.
  - **Content encryption**: Payloads stored encrypted. Key management via RP2350 OTP. Resistant to physical flash extraction.
  - **Firmware lock**: signed boot chain via RP2350 secure boot. Prevents firmware replacement without the publisher's private key.
- The RP2350B OTP and TrustZone features are the intended implementation mechanism. Detailed feasibility investigation required before implementation.
- **One-way door warning**: OTP operations are irreversible. The architecture must support locking as a later addition without requiring OTP writes during development or normal use.

**Guest Licensing**
- Publishers define per-Collection guest policies: how many guest sessions are allowed, for how long, with what feature restrictions.
- Guest Payload delivery is authenticated via the cloud service.

**Firmware Updates**
- The cartridge firmware is updatable.
- Updates may be delivered via USB (user copies update file) or via network (online update, user-initiated).
- Signed firmware updates are a requirement for locked cartridges.
- Update integrity must be verified before flashing.

### 5.9 System Configuration

**System Settings** (cartridge-wide, user-owned):
- WiFi network credentials
- Video output mode (CRT / VGA)
- Language / locale
- Controller mapping (USB HID to MSX joystick/keyboard)
- Network sync global enable/disable
- Payload Source priority order (when multiple sources present)
- Firmware update preferences

**Payload Manifest** (per Payload, authored by publisher):
- Required mapper type
- Required RAM amount and mapping
- Required audio devices (PSG, SCC, OPL4)
- Required IO devices (floppy controller, Nextor, network)
- VDP requirements
- Network access requirements

**User Overrides** (per Payload, per User Profile):
- Audio device selection (internal emulation vs. system hardware)
- Network access enable/disable for this Payload
- Video output mode override
- Any soft Manifest defaults

**Configuration Layering** (in order of precedence, later wins):
1. System Settings (defaults)
2. Payload Manifest (authored requirements — hard requirements cannot be overridden)
3. User Overrides (per profile, per payload)

---

## 6. Open Investigations

The following requirements are confirmed but their implementation approach requires dedicated research before specification:

- **OPL4 full emulation**: FM synthesis (OPL3) + PCM wavetable on RP2350 at MSX bus speeds. Performance budget analysis required. Reference: Nuked-OPL3 and similar open source implementations.
- **SymbOS full compatibility**: precise list of devices and configuration required for SymbOS to boot on MSX2 with 64KB RAM. Requires hands-on testing.
- **VDP emulation scope**: V9938/V9958 emulation on RP2350. Cycle accuracy requirements, performance budget, VRAM size.
- **Floppy disk swap mechanism**: how existing cartridges (Carnivore2, MegaFlashROM, FlashJacks) handle virtual disk swapping. Define the JLPiCart mechanism based on findings.
- **Disc image format**: define the standard format for optical disc images within a Collection. Evaluate existing formats (ISO 9660, raw sector dump) against MSX CD game requirements.
- **RP2350 security features**: detailed investigation of OTP, TrustZone, and secure boot for content protection implementation.
- **INL2 IO port layout**: confirm INL2 v2 port assignments and ensure the network device implementation is compatible from initial design.
- **Cloud service API**: define the protocol between cartridge and cloud service for profile sync, high scores, guest sessions, and online updates. Requires coordination with service operator.
- **USB OTG role switching**: define exact switching conditions and any constraints imposed by the RP2350 USB hardware.
- **MSX host RAM detection**: how the Z80 stub reliably detects available host RAM at boot across all MSX generations, to determine how much RAM the cartridge must provide.

---

## 7. Out of Scope (for now)

- **e-ink display**: deferred. To be revisited after OLED integration is complete.
- **Z80 co-processor**: a second Z80 chip as a co-processor was considered. Deferred indefinitely in favor of the Z80 stub approach.
- Any requirements not listed in this document.
