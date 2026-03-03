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

- **R_COLL_STRUCTURE**: A Collection consists of a Manifest, zero or more Payloads, and optional Assets.
- **R_COLL_TEXT_IN_MANIFEST**: Text descriptions of a Collection are part of its Manifest, not Assets.
- **R_COLL_SOURCE_AGNOSTIC**: The Collection format is source-agnostic: the same format is valid on internal flash, USB stick, optical disc, or downloaded from the network.
- **R_COLL_SINGLE_ACTIVE**: Only one Collection may be active at a time.
- **R_COLL_MIXED_PAYLOADS**: A Collection may contain Payloads of mixed types active simultaneously (e.g., a ROM + a floppy bundle + a mass storage image).
- **R_PAYLOAD_HAS_MANIFEST**: Each Payload has a Manifest specifying required and optional emulated hardware.
- **R_MANIFEST_HARD_REQUIREMENTS**: Hard requirements declared in a Manifest cannot be overridden by the user.
- **R_MANIFEST_SOFT_DEFAULTS**: Soft defaults declared in a Manifest may be overridden by the user.
- **R_MANIFEST_AUTHORED_SECTION**: The Manifest has an authored, read-only section provided by the publisher.
- **R_MANIFEST_USER_SECTION**: The Manifest has a mutable user data section stored in Persistent Storage.
- **R_ASSETS_MENU_ONLY**: Assets included in a Collection or Payload are consumed by the Menu and do not affect runtime behavior.
- **R_COLL_BOOT_DIRECT**: A Collection may declare direct boot behavior in its Manifest, causing the cartridge to boot directly into the specified Payload without showing the Menu.
- **R_COLL_BOOT_MENU_FIRST**: A Collection may declare menu-first boot behavior in its Manifest, causing the Menu to always appear before any Payload launches.
- **R_COLL_SELFCONFIG**: A Collection may declare in its Manifest that it handles its own first-run configuration via the JLPiCart API, suppressing automatic Menu intervention.

### 6.2 Payload Sources

The MSX bus operates in real time — the RP2350 has nanoseconds to respond to memory reads. Direct mapping from USB, CD, or network is not possible in the general case. The cartridge uses a RAM sliding window cache as the default runtime mechanism: Payload content is loaded on demand, and the MSX WAIT line is asserted on a cache miss while the next region is fetched from the source.

- **R_SOURCE_CACHE_DEFAULT**: The default storage mode is a 64KB RAM sliding window cache, filled on demand from the Source.
- **R_SOURCE_CACHE_WAIT**: The MSX WAIT line is asserted on a cache miss while the cache is refilled from the Source.
- **R_SOURCE_CACHE_NO_UPFRONT_COPY**: The RAM cache mode requires no upfront copy before the MSX starts.
- **R_SOURCE_MODE_RAM_FULL**: A Payload Manifest may request full RAM copy mode: the entire Payload is copied to RAM before the MSX starts. No cache misses occur during play. Content is volatile and lost on power off.
- **R_SOURCE_MODE_FLASH_CACHE**: A Payload Manifest may request flash cache mode: the Payload is written to a flash cache area before the MSX starts. Persistent across power cycles. Managed automatically; evicted when space is needed.
- **R_SOURCE_MODE_FLASH_PERMANENT**: Permanently installed Collections are stored in flash. Explicit user or publisher action is required to install or remove them.
- **R_SOURCE_MODE_CARTRIDGE_DECIDES**: The cartridge decides the final storage mode based on available resources, using the Manifest preference as a hint.
- **R_SOURCE_FLASH_DEFAULT**: Internal flash is the default Source. It holds permanently installed Collections and the flash cache area.
- **R_SOURCE_USB_ONE_COLLECTION**: A USB stick contains exactly one Collection.
- **R_SOURCE_USB_INITIALIZER**: A USB stick may contain a full cartridge initializer (firmware + security settings + Collection) for publisher provisioning of blank cartridges.
- **R_SOURCE_CD_ONE_COLLECTION**: An optical drive (CD/DVD) contains exactly one Collection.
- **R_SOURCE_CD_READONLY**: An optical drive Source is read-only.
- **R_SOURCE_CD_INITIALIZER**: An optical drive may contain a cartridge initializer, identical in function to a USB stick initializer.
- **R_SOURCE_NET_COLLECTION_COMPLETE**: A network-delivered Collection is always a complete Collection with a Manifest, even if minimal.
- **R_SOURCE_NET_CACHE_BEFORE_RUN**: Network-delivered Collections are stored in flash cache or RAM before the MSX starts.
- **R_SOURCE_PRIORITY_CONFIGURABLE**: When multiple Sources are present simultaneously, priority order is user-configurable in System Settings.
- **R_SOURCE_PRIORITY_LOCKABLE**: A board definition or Collection Manifest may lock the Source priority order, preventing user modification.

### 6.3 ROM Payload Properties

A ROM Payload declares its mapper type in its Manifest. The mapper is not a device — it is a property of how the ROM is addressed on the MSX bus.

- **R_ROM_MAPPER_IN_MANIFEST**: A ROM Payload declares its mapper type in its Manifest.
- **R_ROM_MAPPER_LINEAR**: The linear mapper (no banking) must be supported.
- **R_ROM_MAPPER_KONAMI**: The Konami mapper (4-bank, 8KB pages) must be supported.
- **R_ROM_MAPPER_KONAMI_SCC**: The Konami SCC mapper (Konami with SCC audio) must be supported.
- **R_ROM_MAPPER_ASCII8**: The ASCII8 mapper (8KB banking) must be supported.
- **R_ROM_MAPPER_ASCII16**: The ASCII16 mapper (16KB banking) must be supported.
- **R_ROM_MAPPER_EXTENSIBLE**: Additional mapper types must be addable without architectural changes.
- **R_ROM_SLOT_CONFIG_IN_MANIFEST**: Slot configuration — which subslots carry ROM, RAM, and devices — must be fully described in the Payload Manifest.
- **R_ROM_SLOT_CONFIG_FLEXIBLE**: Slot configuration must be configurable per Payload, not hardcoded.

### 6.4 Peripheral Devices

- **R_DEV_RESOURCE_CONSUMPTION**: Each active emulated device consumes RP2350 resources (RAM, CPU time, DMA channels, PIO state machines).
- **R_DEV_NOT_ALL_SIMULTANEOUS**: Not all devices can be active simultaneously.
- **R_DEV_MANIFEST_DECLARES**: A Payload's Manifest declares which devices it requires.
- **R_DEV_VALIDATE_AT_LOAD**: The cartridge validates at load time that the requested device combination fits within available resources.
- **R_DEV_ERROR_ON_OVERCOMMIT**: If the requested device combination exceeds available resources, the cartridge reports a clear error rather than silently misbehaving.
- **R_DEV_BOARD_DEPENDENT**: Not all board designs need to support all devices. Device support is a function of the board definition and available hardware peripherals.

**RAM Expansion**

- **R_RAM_DECLARED_PER_PAYLOAD**: The amount of additional RAM the cartridge provides to the MSX is declared per Payload in its Manifest.
- **R_RAM_ANY_SUBSLOT**: RAM expansion is mappable into any subslot.
- **R_RAM_MSX1_FULL_64K**: On MSX1 machines with less than 64KB internal RAM, the cartridge must be capable of providing the full 64KB working RAM so that software expecting 64KB operates correctly.
- **R_RAM_BUDGET_SHARED**: The RAM budget is shared between the Payload's RAM expansion, the firmware, caches, and device emulation state. Resource validation must account for all consumers.

**Audio**

- **R_AUDIO_PSG_EMULATION**: PSG (AY-3-8910) internal emulation must be supported.
- **R_AUDIO_PSG_SELECTABLE**: PSG may be selected per Payload as internal emulation or pass-through to the system PSG.
- **R_AUDIO_SCC_EMULATION**: SCC and SCC+ internal emulation must be supported.
- **R_AUDIO_SCC_SELECTABLE**: SCC may be selected per Payload as internal emulation or pass-through to an external SCC if present.
- **R_AUDIO_OPL4_EMULATION**: Full OPL4 emulation (FM synthesis + PCM wavetable layer) must be supported. Feasibility study required — see Open Investigations.
- **R_AUDIO_STEREO_REQUIRES_HARDWARE**: Stereo audio output requires appropriate board hardware (stereo audio output connector).

**Video**

- **R_VIDEO_OUTPUT_MODE_SETTING**: Video output mode (CRT vs VGA) is a System Setting.
- **R_VIDEO_CONNECTOR_BOARD_DEPENDENT**: The video connector layout is board-dependent. The reference design uses a single shared connector; other designs may use separate connectors.
- **R_VIDEO_VDP_UPGRADE_EMULATION**: The cartridge may present an enhanced VDP (V9938, V9958) to MSX1 host machines. This is declared per Collection in the Manifest. Feasibility and scope require dedicated investigation — see Open Investigations.

**Floppy Controller**

- **R_FLOPPY_WD279X_COMPATIBLE**: The cartridge emulates a standard MSX floppy disk controller, WD279x-compatible.
- **R_FLOPPY_SERVES_BUNDLE**: The floppy controller serves floppy images from a Floppy Bundle Payload.
- **R_FLOPPY_READONLY**: Read-only floppy images must be supported.
- **R_FLOPPY_READWRITE**: Read-write floppy images must be supported.
- **R_FLOPPY_VIRTUAL_SWAP**: A virtual disk swap mechanism is required for multi-disk software. Implementation approach TBD — see Open Investigations.
- **R_FLOPPY_PERSIST_TO_FLASH**: Read-write floppy image changes are persisted to internal flash.
- **R_FLOPPY_WRITEBACK_USB**: If the Source is a USB stick, floppy image changes may optionally be written back to the USB stick.

**Mass Storage / Nextor**

- **R_NEXTOR_BLOCK_DEVICE**: The cartridge presents a Nextor-compatible block device interface to the MSX.
- **R_NEXTOR_SERVES_PAYLOAD**: The Nextor device serves Mass Storage Payload images (read-only or read-write).
- **R_NEXTOR_FLASH_VOLUME**: Internal flash may be partitioned to serve as a Nextor volume.
- **R_NEXTOR_USB_VOLUME**: USB sticks connected via USB host may be exposed as additional Nextor volumes.
- **R_NEXTOR_BOOT**: The Nextor device enables MSX-DOS 2 / Nextor booting from the cartridge.

**Network Device**

- **R_NET_DEV_IO_PERIPHERAL**: The network device is exposed to MSX software as a standard peripheral via IO ports.
- **R_NET_DEV_INL2_PORTLAYOUT**: The IO port layout of the network device must be INL2-compatible from initial design. Retrofitting is not acceptable.

### 6.5 Runtime Behavior

- **R_RUNTIME_BOOT_DRIVEN_BY_MANIFEST**: Boot behavior is driven by the active Collection's Manifest (direct boot or menu first).
- **R_RUNTIME_MENU_ON_ATTENTION**: The Menu appears automatically only when something requires user attention and no Collection or game has declared it will handle that itself.
- **R_RUNTIME_MENU_ON_NO_COLLECTION**: If the cartridge has no Collection loaded, the Menu appears automatically.
- **R_RUNTIME_MENU_ALWAYS_ACCESSIBLE**: The user may always enter the Menu via a defined action at boot, regardless of the Collection's declared boot behavior.
- **R_RUNTIME_CD_LOADING_SCREEN**: If an optical drive is connected and a disc is present at boot, a loading screen is shown while the disc is read before the Collection boots.
- **R_RUNTIME_SINGLE_PAYLOAD_DIRECT**: If a Collection has only one Payload and declares direct boot, the Payload launches immediately without any intermediate screen.
- **R_RUNTIME_MENU_AFTER_DIRECT_BOOT**: After a direct boot launch, the Menu remains accessible via the defined user action.
- **R_RUNTIME_RESET_REINIT**: On MSX reset, the cartridge reinitializes the active Collection's device configuration and relaunches according to its boot behavior.

### 6.6 Menu

- **R_MENU_RUNS_ON_MSX**: The Menu runs on the MSX screen using the host machine's VDP and keyboard.
- **R_MENU_ADAPTIVE_MSX1**: On MSX1 / TMS9918, the Menu renders in text mode.
- **R_MENU_ADAPTIVE_MSX2**: On MSX2 / V9938, the Menu renders with enhanced graphics.
- **R_MENU_ADAPTIVE_MSX2PLUS**: On MSX2+ / V9958, the Menu renders at best available quality.
- **R_MENU_COLLECTION_SELECTION**: The Menu provides Collection selection.
- **R_MENU_PAYLOAD_SELECTION**: The Menu provides Payload selection within a Collection.
- **R_MENU_SYSTEM_SETTINGS**: The Menu provides access to System Settings.
- **R_MENU_PROFILE_MANAGEMENT**: The Menu provides User Profile management.
- **R_MENU_PUBLISHER_TOOLS**: The Menu provides publisher tools (signing, locking).
- **R_MENU_ACCESSIBLE_FROM_COLLECTION**: The Menu is accessible from a running Collection via a defined key combination or hardware action.
- **R_MENU_ALWAYS_AVAILABLE**: The Menu is always available regardless of what Collection is loaded or whether any Collection is loaded.
- **R_MENU_NOT_A_COLLECTION**: The Menu is not part of any Collection and cannot be replaced or modified by a Collection.

### 6.7 User Profiles and Identity

- **R_PROFILE_MULTIPLE**: Multiple User Profiles may exist on one cartridge.
- **R_PROFILE_DISPLAY_NAME**: Each profile has a display name.
- **R_PROFILE_LANGUAGE**: Each profile stores a language preference.
- **R_PROFILE_SAVE_DATA**: Each profile stores save data per Payload.
- **R_PROFILE_HIGH_SCORES**: Each profile stores high scores per Payload.
- **R_PROFILE_EXTENDED_DATA**: Each profile stores extended game-specific data per Payload (ghosts, replays, etc.).
- **R_PROFILE_SYSTEM_LEVEL**: Profiles are system-level and persist across Collection changes.
- **R_PROFILE_LOCAL_IDENTITY**: Each profile has a local identity stored on the cartridge.
- **R_PROFILE_CLOUD_IDENTITY**: Each profile may optionally have a cloud identity (username + password/token) for network sync.
- **R_PROFILE_GUEST_INVITE**: A User Profile holder may invite another JLPiCart user to play a Collection they do not own.
- **R_PROFILE_GUEST_TEMP_COLLECTION**: The guest receives a temporary Collection via network for the duration of the Guest Session.
- **R_PROFILE_GUEST_SESSION_LIMIT**: Guest Sessions are subject to publisher-defined session count limits.
- **R_PROFILE_GUEST_TIME_LIMIT**: Guest Sessions are subject to publisher-defined time limits.
- **R_PROFILE_GUEST_FEATURE_LIMIT**: Guest Sessions are subject to publisher-defined feature restrictions.
- **R_IDENTITY_CARTRIDGE_IS_PROVIDER**: The cartridge acts as identity provider for games and applications.
- **R_IDENTITY_OPAQUE_TOKENS**: Via the JLPiCart API, a game receives opaque identity tokens for active players.
- **R_IDENTITY_GAME_NO_CREDENTIALS**: A game never manages usernames, passwords, or sync logic directly.
- **R_IDENTITY_MULTIPLAYER_PROFILES**: The cartridge supports multiple simultaneously active profiles for local multiplayer.
- **R_CONSENT_GRANULAR**: Users have explicit, granular control over what data leaves the device.
- **R_CONSENT_PER_PROFILE**: Sync consent is configurable per profile.
- **R_CONSENT_PER_DATA_TYPE**: Sync consent is configurable per data type (saves, high scores, ghosts, profile data).
- **R_PROFILE_LOCAL_PRIMARY**: All profile data is stored locally on the cartridge.
- **R_PROFILE_SYNC_OPTIONAL**: Network sync is optional and additive. The cartridge is fully functional offline.

### 6.8 Connectivity

**WiFi**

- **R_WIFI_ESP32_PROVIDER**: WiFi connectivity is provided by the ESP32 co-processor via AT command interface.
- **R_WIFI_CREDENTIALS_SYSTEM_SETTING**: WiFi credentials are stored in System Settings and are cartridge-wide.
- **R_WIFI_ACCESS_IN_MANIFEST**: Per-Collection network access requirements are declared in the Collection Manifest.
- **R_WIFI_USER_RESTRICT_PER_COLLECTION**: The user may restrict network access per Collection.
- **R_WIFI_USER_DISABLE_GLOBAL**: The user may disable network access globally.

**InterNestorLite (INL2)**

- **R_INL2_FULL_SUPPORT**: Full support for the InterNestorLite v2 protocol is a hard requirement.
- **R_INL2_PORT_LAYOUT_FIXED**: The IO port layout of the network device must be INL2-compatible from initial design.
- **R_INL2_EXISTING_SOFTWARE**: INL2 compatibility enables existing MSX networked software to work without modification.

**JLPiCart API — Network Features**

- **R_API_NET_TCP_SOCKET**: The JLPiCart API exposes a TCP socket interface to MSX software.
- **R_API_NET_HIGHSCORE_SUBMIT**: The JLPiCart API supports high score submission to the cloud service.
- **R_API_NET_HIGHSCORE_RETRIEVE**: The JLPiCart API supports high score retrieval from the cloud service.
- **R_API_NET_PROFILE_SYNC**: The JLPiCart API supports user profile sync, opt-in per data type.
- **R_API_NET_GUEST_DELIVERY**: The JLPiCart API supports Guest Session delivery (temporary Collection download).
- **R_API_NET_COLLECTION_UPDATE**: The JLPiCart API supports online Collection updates pushed by the publisher.
- **R_API_NET_MULTIPLAYER_SESSION**: Two or more JLPiCart units may connect for network multiplayer via the JLPiCart API.
- **R_API_NET_GAME_NO_NETCONFIG**: Games use the JLPiCart API for multiplayer session setup and never manage network configuration themselves.

**Cloud Service**

- **R_CLOUD_STORES_PROFILES**: The cloud service stores user profiles.
- **R_CLOUD_STORES_SCORES**: The cloud service stores high scores and ghost/replay data.
- **R_CLOUD_STORES_METADATA**: The cloud service stores Collection metadata.
- **R_CLOUD_COMMS_TCP**: The cartridge communicates with the cloud service via TCP over WiFi.
- **R_CLOUD_EXTERNAL_DEPENDENCY**: The cloud service and its API are an external dependency requiring coordination with the service operator.

### 6.9 USB

- **R_USB_HOST_ALWAYS**: The USB port operates in host mode at all times during normal operation.
- **R_USB_HID_GAMEPAD**: USB HID gamepad and joystick devices are supported and mapped to MSX joystick ports.
- **R_USB_GAMEPAD_MAPPING_SETTING**: Controller mapping (USB HID to MSX joystick) is a System Setting.
- **R_USB_HID_KEYBOARD**: USB HID keyboard devices are supported and mapped to the MSX keyboard matrix.
- **R_USB_KEYBOARD_MAPPING_SETTING**: Keyboard mapping is a System Setting.
- **R_USB_STICK_SOURCE**: USB sticks are a Payload Source, detected at boot via USB host.
- **R_USB_STICK_NEXTOR**: USB sticks may be exposed as Nextor volumes.
- **R_USB_OPTICAL_SOURCE**: USB optical drives (CD/DVD) are a Payload Source, detected at boot via USB host.
- **R_USB_CONTENT_VIA_STICK_OR_NET**: Collections are loaded onto the cartridge via USB stick or network only.
- **R_USB_DEVICE_MODE_FIRMWARE_ONLY**: USB device mode is used only during initial firmware flashing via the RP2350B bootrom. It is not a user-facing feature.
- **R_USB_TINYUSB_STACK**: TinyUSB is the USB stack.
- **R_USB_OPTICAL_PATCH_MAINTAINED**: The existing TinyUSB patch enabling optical drive (bulk-only transport) support must be maintained.

### 6.10 Security and Licensing

**Publisher Locking**

- **R_SEC_LOCK_COLLECTION**: A publisher may lock a Collection so its contents cannot be modified or extracted.
- **R_SEC_LOCK_VIA_USB_STICK**: Locking is performed by copying a signed folder to the cartridge via USB stick. No custom tooling is required.
- **R_SEC_LOCK_MENU_OPTION**: The Menu provides a locking and signing option for publishers and developers.
- **R_SEC_LOCK_LEVEL_UI**: UI lock prevents content modification via Menu or USB interface. Provides friendly/casual protection only; physically bypassable.
- **R_SEC_LOCK_LEVEL_ENCRYPTION**: Content encryption stores Payloads encrypted with key management via RP2350 OTP. Resistant to physical flash extraction.
- **R_SEC_LOCK_LEVEL_FIRMWARE**: Firmware lock uses a signed boot chain via RP2350 secure boot, preventing firmware replacement without the publisher's private key.
- **R_SEC_OTP_INVESTIGATION_REQUIRED**: Detailed feasibility investigation of RP2350B OTP and TrustZone is required before implementing content encryption or firmware lock — see Open Investigations.
- **R_SEC_OTP_NO_DEV_WRITES**: OTP operations are irreversible. The architecture must support locking as a later addition without requiring any OTP writes during development or normal use.
- **R_SEC_ARCH_LOCK_ADDABLE_LATER**: The architecture must be designed so that all locking levels can be added after initial deployment without breaking existing unlocked cartridges.

**Guest Licensing**

- **R_GUEST_POLICY_SESSION_COUNT**: Publishers define a maximum guest session count per Collection.
- **R_GUEST_POLICY_TIME_LIMIT**: Publishers define a maximum guest session duration per Collection.
- **R_GUEST_POLICY_FEATURE_RESTRICT**: Publishers define feature restrictions for guest sessions per Collection.
- **R_GUEST_DELIVERY_AUTHENTICATED**: Guest Collection delivery is authenticated via the cloud service.

**Firmware Updates**

- **R_UPDATE_SUPPORTED**: The cartridge firmware is updatable after deployment.
- **R_UPDATE_VIA_USB_STICK**: Firmware updates may be delivered via USB stick.
- **R_UPDATE_VIA_NETWORK**: Firmware updates may be delivered via network (user-initiated).
- **R_UPDATE_SIGNED_FOR_LOCKED**: Signed firmware updates are required for locked cartridges.
- **R_UPDATE_INTEGRITY_VERIFIED**: Update integrity must be verified before flashing.

### 6.11 System Configuration

**System Settings** (cartridge-wide, user-owned):

- **R_CFG_SYS_WIFI_CREDENTIALS**: System Settings store WiFi network credentials.
- **R_CFG_SYS_VIDEO_MODE**: System Settings store the video output mode (CRT / VGA).
- **R_CFG_SYS_LANGUAGE**: System Settings store language and locale.
- **R_CFG_SYS_CONTROLLER_MAPPING**: System Settings store controller mapping (USB HID to MSX joystick/keyboard).
- **R_CFG_SYS_NET_SYNC_ENABLE**: System Settings store the global network sync enable/disable flag.
- **R_CFG_SYS_SOURCE_PRIORITY**: System Settings store the Payload Source priority order.
- **R_CFG_SYS_UPDATE_PREFS**: System Settings store firmware update preferences.

**Payload Manifest** (per Payload, authored by publisher):

- **R_CFG_MANIFEST_MAPPER**: The Manifest declares the required mapper type.
- **R_CFG_MANIFEST_RAM**: The Manifest declares the required RAM amount and mapping.
- **R_CFG_MANIFEST_AUDIO**: The Manifest declares the required audio devices (PSG, SCC, OPL4).
- **R_CFG_MANIFEST_IO_DEVICES**: The Manifest declares the required IO devices (floppy controller, Nextor, network).
- **R_CFG_MANIFEST_VDP**: The Manifest declares VDP requirements.
- **R_CFG_MANIFEST_NET_ACCESS**: The Manifest declares network access requirements.
- **R_CFG_MANIFEST_STORAGE_MODE**: The Manifest declares a preferred storage mode.

**User Overrides** (per Payload, per User Profile):

- **R_CFG_OVERRIDE_AUDIO**: The user may override audio device selection (internal emulation vs. system hardware) for soft Manifest defaults.
- **R_CFG_OVERRIDE_NET_ACCESS**: The user may override network access enable/disable for a specific Payload.
- **R_CFG_OVERRIDE_VIDEO_MODE**: The user may override the video output mode for a specific Payload.

**Configuration Layering**:

- **R_CFG_LAYER_SYSTEM_FIRST**: System Settings provide the base defaults.
- **R_CFG_LAYER_MANIFEST_SECOND**: Payload Manifest requirements are applied on top of System Settings. Hard Manifest requirements cannot be overridden.
- **R_CFG_LAYER_OVERRIDE_LAST**: User Overrides are applied last, on top of Manifest soft defaults only.

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
