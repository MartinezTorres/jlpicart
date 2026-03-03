# JLPiCart Requirements

## 1. Vision

JLPiCart is an open collection of tools (PCB design, firmware, and specifications) built around the RP2350B microcontroller for anyone who wants to publish software for the MSX ecosystem.

It is not meant as a flash cartridge for end-users. Carnivore2, MegaFlashROM, FlashJacks, and Pico+ already cover that space well. JLPiCart is for publishers. It provides everything needed to design and distribute inexpensive, purpose-built MSX cartridges with wide hardware support and a coherent end-user experience, without building any of that infrastructure from scratch. Mappers, RAM expansion, audio emulation, floppy and mass storage, networking, user identity, save management, and high score sync are all provided by the platform. A publisher focuses on the software. JLPiCart handles the rest.

The platform is fully open source. Publishers who need capabilities beyond the standard feature set can modify the firmware and PCB design freely. Anything built on JLPiCart can be published and shared. Everything works offline. Network features are always additive and never required.

---

## 2. Glossary

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

---

## 3. Use Cases

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
A publisher releases a game with network features: online high scores, head-to-head multiplayer, and ghost data sync. The game uses the JLPiCart API for all network interactions. The cartridge manages user identity and WiFi credentials. The game never handles network configuration directly. INL2 compatibility ensures the game also works with other MSX network cartridges.

**Guest session / demo**
A publisher wants to let users try a game before purchasing. A licensed user invites a friend to play via the JLPiCart API. The friend's cartridge downloads a temporary Collection over the network, plays the game within publisher-defined limits, and the session expires automatically. No physical media required.

**Firmware and platform customization**
A developer wants to add a device emulator not currently supported by the platform (e.g., a custom sound chip or a specific MSX peripheral). They fork the firmware, implement the new device following the existing peripheral device pattern, and distribute their modified board definition and firmware. Other users can adopt their work by flashing it.

---

## 4. Hardware Platform

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

- **OLED display** (e.g., SSD1306, 128×32) — status and debugging aid.
- **Video output connector** — analog video output (CRT and/or VGA) via PIO. The reference design uses a single shared connector; nothing prevents using two separate connectors.
- **Stereo audio output** — analog audio output for emulated audio devices.
- **GPIO with ADC** — general purpose I/O, temperature sensing, battery voltage monitoring, or custom hardware extensions.

### Reference Design

The JLPiCart reference design targets the **RP2350B** with 16MB external flash and includes all core and situational peripherals listed above. The default firmware build targets this configuration.

---

## 5. System Architecture

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

## 6. Requirements

### 6.1 Collections and Payloads

- A Collection consists of a Manifest, zero or more Payloads, and optional Assets (artwork, music). Text descriptions are part of the Manifest.
- The Collection format is source-agnostic: the same format is valid on internal flash, USB stick, optical disc, or downloaded from the network.
- Only one Collection may be active at a time.
- A Collection may contain Payloads of mixed types simultaneously (e.g., a ROM + a floppy bundle + a mass storage image all active at once).
- Each Payload has a Manifest specifying required and optional emulated hardware. Hard requirements in a Manifest cannot be overridden by the user; soft defaults can.
- The Manifest has an authored (read-only) section and a user data section (mutable, stored in Persistent Storage).
- Assets included in a Collection are consumed by the Menu but do not affect runtime behavior.
- A Collection declares its boot behavior in its Manifest:
  - **Direct boot**: boot directly into the specified Payload. Used for single-game cartridges or software that manages its own UI.
  - **Menu first**: always boot into the Menu before launching any Payload. Used for game collections where selection is the primary experience.
- A Collection may declare that it handles its own first-run configuration (e.g., WiFi setup, profile creation) via the JLPiCart API, suppressing automatic Menu intervention.

### 6.2 Payload Sources

The MSX bus operates in real time — the RP2350 has nanoseconds to respond to memory reads. Direct mapping from USB, CD, or network is not possible in the general case. Instead, the cartridge uses a **64KB RAM sliding window cache** as the default runtime mechanism: Payload content is loaded on demand into the cache, and the MSX WAIT line is asserted on a cache miss while the next region is fetched from the source. This works for all source types and requires no upfront copy before the MSX starts.

**Storage modes** (declared in the Payload's Manifest, cartridge decides based on available resources):
- **RAM cache (default)**: 64KB sliding window, filled on demand from source. WAIT line asserted on cache miss. Works for all sources. Network sources may have noticeable latency on misses.
- **RAM (full copy)**: entire Payload copied to RAM upfront if it fits. No cache misses during play. Volatile — lost on power off.
- **Flash (cache)**: Payload written to flash cache before running. Persistent across power cycles, no WAIT line during play. Managed automatically; evicted when space is needed.
- **Flash (permanent)**: user-installed Collections stored in flash. Explicit user or publisher action required to install or remove.

**Sources:**
- **Internal flash**: the default runtime source. Holds permanently installed Collections and the flash cache area.
- **USB stick**: detected at boot via USB host. Contains exactly one Collection. May also contain a full cartridge initializer (firmware + security settings + Collection) for publisher provisioning of blank cartridges.
- **Optical drive (CD/DVD)**: identical to USB stick in structure and behavior, read-only. Detected at boot via USB host. Contains exactly one Collection, optionally with a cartridge initializer.
- **Network**: Collections downloaded and stored in flash cache or RAM before running. A Payload always arrives as part of a Collection — a network-delivered Guest Session or temporary Collection is still a complete Collection with a Manifest, even if minimal.

When multiple sources are present simultaneously, priority order is user-configurable in System Settings. A board or Collection Manifest may lock the priority order.

### 6.3 ROM Payload Properties

A ROM Payload declares its mapper type in its Manifest. The mapper is not a device — it is a property of how the ROM is addressed on the MSX bus. The following mapper types must be supported:

- Linear (no mapper)
- Konami (4-bank, 8KB pages)
- Konami SCC (Konami with SCC audio)
- ASCII8 (8KB banking)
- ASCII16 (16KB banking)
- Additional mapper types must be addable without architectural changes.

Slot configuration (which subslots carry ROM, RAM, devices) must be fully described in the Payload Manifest and configurable.

### 6.4 Peripheral Devices

The cartridge emulates MSX peripheral devices. Each active device consumes RP2350 resources (RAM, CPU time, DMA channels, PIO state machines). Not all devices can be active simultaneously. A Payload's Manifest declares which devices it requires; the cartridge validates at load time that the requested combination fits within available resources. If it does not, the cartridge reports an error rather than silently misbehaving.

Not all board designs need to support all devices. Device support is a function of the board definition and available hardware peripherals.

**RAM Expansion**
- The cartridge provides additional RAM to the MSX, declared per Payload in its Manifest.
- RAM is mappable into any subslot.
- On MSX1 machines with less than 64KB internal RAM, the cartridge must be capable of providing the full 64KB working RAM so that software expecting 64KB operates correctly.
- The RAM budget is shared with the cartridge's own runtime needs (firmware, caches, device emulation). Resource validation must account for this.

**Audio**
- PSG (AY-3-8910): internal emulation, selectable per Payload as internal or pass-through to system PSG.
- SCC / SCC+: internal emulation, selectable per Payload.
- OPL4: full emulation (FM synthesis + PCM wavetable layer). Feasibility study required — see Open Investigations.
- Stereo audio output requires appropriate board hardware.

**Video**
- Video output mode (CRT vs VGA) is a System Setting. The reference design uses a single shared connector; other board designs may use separate connectors.
- VDP upgrade emulation: the cartridge may present an enhanced VDP (V9938, V9958) to MSX1 host machines. Declared per Collection. Feasibility and scope require dedicated investigation — see Open Investigations.

**Floppy Controller**
- Emulates a standard MSX floppy disk controller (WD279x-compatible).
- Serves floppy images from a Floppy Bundle Payload.
- Supports read-only and read-write images.
- Virtual disk swap mechanism required for multi-disk software. Implementation approach TBD — see Open Investigations.
- Read-write floppy images are persisted to internal flash. If the Source is a USB stick, changes may optionally be written back to it.

**Mass Storage / Nextor**
- Presents a Nextor-compatible block device interface.
- Serves Mass Storage Payload images (read-only or read-write).
- Internal flash may be partitioned to serve as a Nextor volume.
- USB sticks connected via USB host may be exposed as additional Nextor volumes.
- Enables MSX-DOS 2 / Nextor booting from cartridge.

**Network**
- Exposed as a standard MSX peripheral via IO ports.
- INL2-compatible IO port layout — see section 6.8.

### 6.5 Runtime Behavior

- Boot behavior is driven by the active Collection's Manifest (direct boot or menu first).
- The Menu appears automatically only when something requires user attention (e.g., first boot with no profile, missing required configuration) and no Collection or game has declared it will handle that itself.
- The user may always enter the Menu via a defined action at boot (TBD: key combination, ESC, hardware button).
- If the cartridge has no Collection loaded, the Menu appears automatically.
- CD/optical boot: if an optical drive is connected and a disc is present, a loading screen is shown while the disc is read, then the Collection boots according to its declared boot behavior.
- If a Collection has only one Payload and declares direct boot, the Payload launches immediately. The Menu remains accessible for configuration via a user action.
- On reset, the cartridge reinitializes the active Collection's device configuration and relaunches according to its boot behavior.

### 6.6 Menu

- Runs on the MSX screen using the host machine's VDP and keyboard.
- Adaptive rendering based on detected MSX generation and VDP:
  - MSX1 / TMS9918: text mode
  - MSX2 / V9938: enhanced graphics
  - MSX2+ / V9958: best quality
- Functions: Collection selection, Payload selection within a Collection, System Settings, User Profile management, publisher tools (signing, locking).
- Accessible from a running Collection via a defined key combination or hardware action.
- The Menu is not part of any Collection. It is always available regardless of what Collection is loaded or whether any Collection is loaded.

### 6.7 User Profiles and Identity

- Multiple User Profiles may exist on one cartridge.
- Each profile contains: display name, language preference, save data (per Payload), high scores (per Payload), and extended game-specific data (ghosts, replays, etc.).
- Profiles are system-level: they persist across Collection changes.
- Each profile has a local identity and an optional cloud identity (username + password/token) for network sync.
- **Guest Sessions**: a User Profile holder may invite another JLPiCart user to play a Collection they do not own. The guest receives a temporary Collection via network, subject to publisher-defined limits (session count, time, feature restrictions).
- The cartridge acts as **identity provider** for games. Via the JLPiCart API, a game queries the cartridge for active player identities and receives opaque tokens. The game never manages usernames, passwords, or sync logic.
- Multi-player identity: the cartridge supports multiple simultaneous active profiles (for local multiplayer).
- **Consent model**: users have explicit, granular control over what data leaves the device. Sync of saves, high scores, ghosts, and profile data is individually opt-in per profile and per data type.
- All profile data is stored locally. Network sync is optional and additive. The cartridge is fully functional offline.

### 6.8 Connectivity

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
- Guest Session delivery (temporary Collection download).
- Online Collection updates (publisher pushes updated Collection to cartridge).
- Network multiplayer: two or more JLPiCart units may connect for head-to-head play. Each cartridge manages its own user identity; games use the JLPiCart API for session setup rather than implementing networking themselves.

**Cloud Service**
- An external web service (operated by a community partner) stores user profiles, high scores, ghosts/replays, and Collection metadata.
- The cartridge communicates with this service via TCP over WiFi.
- The service and its API are an external dependency and require coordination with the service operator.

### 6.9 USB

- The USB port operates in host mode at all times during normal operation.
- USB HID gamepad/joystick support, mapped to MSX joystick ports (System Setting: controller mapping).
- USB keyboard support, mapped to MSX keyboard matrix (System Setting).
- USB stick support: additional Payload Source and Nextor volume.
- USB optical drive support (CD/DVD as Payload Source).
- Collections are loaded onto the cartridge via USB stick or network. USB device mode is not a user-facing feature; it is only used during initial firmware flashing via the RP2350B bootrom.
- TinyUSB is the USB stack. The existing patch enabling optical drive (bulk-only transport) support must be maintained.

### 6.10 Security and Licensing

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
- Guest Collection delivery is authenticated via the cloud service.

**Firmware Updates**
- The cartridge firmware is updatable.
- Updates may be delivered via USB stick or via network (online update, user-initiated).
- Signed firmware updates are a requirement for locked cartridges.
- Update integrity must be verified before flashing.

### 6.11 System Configuration

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
- Storage mode preference

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

## 7. Open Investigations

The following requirements are confirmed but their implementation approach requires dedicated research before specification. Security investigations are highest priority as they have architectural implications and involve irreversible hardware operations.

### 7.1 Security

- **RP2350 OTP and secure boot**: detailed investigation of the RP2350B OTP memory, TrustZone, and signed boot chain. Determine: what can be stored in OTP, what is the signing workflow, what happens if a signing key is lost, and whether secure boot can be enabled/disabled per cartridge unit.
- **Content encryption key lifecycle**: when is the encryption key generated? By whom — publisher, user, or cartridge? How is it provisioned onto the cartridge? If stored in OTP, how is it shared for Guest Sessions? What is the key rotation strategy?
- **Publisher provisioning security**: a USB stick can initialize a blank cartridge including security settings. What prevents a malicious USB stick from hijacking an unprovisioned cartridge? Is there a provisioning window that can be closed?
- **Guest session key delivery**: if Payloads are encrypted, a guest cartridge needs a decryption key for the session. How is this key delivered securely over the network? How is it time-limited or session-limited? Investigate whether the cloud service can act as a key escrow.
- **Firmware update security for locked cartridges**: a locked cartridge only accepts signed firmware. Define the signing infrastructure. How does a publisher distribute a firmware update? Can a publisher lock their cartridge to only accept their own firmware updates, or also platform-wide updates?
- **Threat model definition**: define precisely what "locked" means against which adversaries. Physical attacker with flash reader? Network attacker? Malicious USB stick? This shapes which security features are worth implementing.
- **Locking workflow UX**: the publisher copies a signed folder via USB stick to lock a cartridge. Define the exact format of that folder, what the cartridge does when it detects it, and what happens if the process is interrupted.

### 7.2 Networking and Connectivity

- **ESP32 AP+STA mode**: confirm whether the esp-at firmware version pinned in the repo supports simultaneous access point and station mode. Determine bandwidth sharing characteristics, maximum number of AP clients, and stability under load.
- **Local device discovery**: how do two JLPiCart units find each other on a local network without a router? Evaluate mDNS, UDP broadcast, and AP+STA topology (one unit becomes the host AP). Define the discovery protocol.
- **Offline direct multiplayer**: two cartridges, no internet, no router. One acts as AP, the other connects as STA. Define the session setup flow via the JLPiCart API so games do not need to manage this themselves.
- **Mesh networking**: for tournament or multi-player sessions with more than two units. Investigate whether the ESP32 supports multi-hop mesh (e.g., ESP-MESH or similar). Define maximum node count and latency characteristics.
- **INL2 IO port layout**: confirm INL2 v2 port assignments and ensure the network device IO port layout is compatible from initial design. Retrofitting is not acceptable.
- **Cloud service API**: define the protocol between cartridge and cloud service for profile sync, high scores, ghost data, guest sessions, and online Collection updates. Requires coordination with the service operator.
- **Network latency budget for MSX software**: determine the maximum acceptable round-trip latency for INL2 operations and JLPiCart API network calls, given the constraints of Z80 timing and game loop expectations.

### 7.3 Device Emulation

- **OPL4 full emulation**: FM synthesis (OPL3, 36 operators) + PCM wavetable layer on RP2350 at MSX bus speeds. Performance budget analysis required. Reference: Nuked-OPL3 and similar open source implementations. Determine whether cycle-accurate emulation is feasible or whether approximations are acceptable.
- **VDP emulation scope**: V9938/V9958 emulation on RP2350. Determine cycle accuracy requirements, VRAM size, performance budget, and which video modes are required versus optional.
- **Floppy disk swap mechanism**: research how existing cartridges (Carnivore2, MegaFlashROM, FlashJacks) handle virtual disk swapping for multi-disk software. Define the JLPiCart mechanism based on findings.
- **Disc image format**: define the standard format for optical disc images within a Collection. Evaluate ISO 9660, raw sector dumps, and any MSX-specific CD formats against real MSX CD game requirements.
- **SymbOS full compatibility**: determine the precise set of devices and configuration required for SymbOS to boot on an MSX2 with 64KB internal RAM. Requires hands-on testing with a real SymbOS installation.
- **MSX host RAM detection**: how the cartridge reliably detects available host RAM at boot across all MSX generations, to determine how much additional RAM the cartridge must provide.
- **Resource budget model**: define the RP2350 resource budget (RAM, CPU cycles, DMA channels, PIO state machines) and the validation algorithm the cartridge uses to accept or reject a device combination declared in a Manifest.
