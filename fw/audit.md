# JLPiCart Firmware — Code vs Documentation Audit

## src/bus

**Missing from docs:**
- **KONAMI_SCC mapper** — `mappers.h:22` declares, `mappers.cc:190/203` has string mapping, but no `mapper_setup_konami_scc()` implementation. Not in `capabilities.md:125`.
- **KONAMI_Z mapper** — Fully implemented (`mappers.cc:115-127`, `mappers.h:79-81`), not in `capabilities.md`.
- **ROM_32K_MIRRORED mapper** — `mappers.cc:84-93`, `mappers.h:65-67`, not documented.
- **RAM mapper** — `MapperType::RAM` (`mappers.h:26`, `mappers.cc:167-177`), not in `capabilities.md`.
- **Subslot architecture** — 16 subslots defined (`bus.h:20`), 0-3 memory, 4-15 IO (`bus.h:22-24`, `cartridge.h:4-6`). Not documented.
- **Expansion register at 0xFFFF** — `bus.cc:79-84,116-119`, complement encoding. Not documented.
- **RAMFUNC/SRAM execution model** — `cartridge.h:22-32`, `.time_critical` section for Core 0 XIP stalls. Not documented.
- **Dual callback mechanism** — `cartridge.h:48-54`, both direct pointers AND callback arrays per segment. Not documented.
- **Core 0 / Core 1 split** — `bus.h:4-6`, bus loop on Core 0, all other firmware on Core 1. Not documented.

**Stale in docs:**
- `overview.md:54` references `publisher.md` — file does not exist.
- `overview.md:61` references `collection-author.md` — file does not exist.

**Contradictions:**
- `KONAMI_SCC` declared in `mappers.h:22` with string conversion in `mappers.cc:190/203`, but no setup function exists. A `MapperType::KONAMI_SCC` would silently not wire up.

---

## src/content

**Missing from docs:**
- `pub_anchor.bin` as publisher trust anchor at `1:/system/pub_anchor.bin` (`bundle_sig_verify.cc:8`, `installer.h:79`). Docs describe PRK/PIC chain, but code reads a raw 32-byte ed25519 public key file.
- `InstallReader` abstract interface (`installer.h:25-46`). Not documented.
- `.installing/` staging directory and `clear_installing_dir()` for atomic install via rename (`installer.cc:28,62-77`). Docs mention atomicity but not mechanism.
- Per-payload `required_capabilities` / `optional_capabilities` arrays (`manifest.h:29-32`, `manifest_parser.cc:355-383`). `capabilities.md` describes platform capabilities, not payload-level requirements.
- `PAYLOAD_CAPS_MAX = 4` hard limit (`manifest.h:10`).
- OPL4 `wave_payload_id` param in device entries (`manifest_parser.cc:316-318`).

**Stale in docs:**
- PIC / PRK / CSK verification chain (`overview.md:193-198`, `key-reference.md:221-295`). Code has no PIC parsing, no PRK verification, no CSK concept — only raw ed25519 anchor verification.
- Install Receipt as "append-only local record" (`glossary.md:31-32`). `installer.cc:7` comment mentions "Append receipt to event log (best-effort)" but code never writes one.
- Sources: optical drive, network (`glossary.md:13-14`). Code only has `InstallReader` abstraction.
- References to `publisher.md` and `collection-author.md` (`overview.md:54,61`) — files not present.
- Leaderboard integrity, device-to-device sessions, DIK scoped IDs (`overview.md:33-35,210-227`) — no code in src/content/ touches these.

**Contradictions:**
- **Signature algorithm**: `collection_format.h:43` comment says `"ecdsa_secp256k1_sha256"`, `signature[144]` buffer sized for ECDSA DER, but `bundle_sig_verify.cc:19-22` only accepts `"ed25519"` with exactly 64-byte signatures. Struct layout and verification code disagree.
- **Manifest `schema` field**: `manifest.h:44` defines `schema` ("jlpicart.collection.v1"), but `manifest_parser.cc:481-483` parses it as optional — not in required-field checks at `manifest_parser.cc:510-513`, contradicting `manifest_parser.h:15` which lists required fields.
- **Policy flags**: Code uses `POLICY_REQUIRE_PUBLISHER_SIGNATURE` and `POLICY_ALLOW_UNSIGNED_COLLECTIONS` (`installer.cc:123-124`). `pcb-builder.md:213-223` documents JSON policy fields like `allow_usb_collection_install`, `allow_network_collection_install` — none appear in the code's policy interface.
- **Active Collections**: `glossary.md:3` says "Only one Collection is active at a time." `glossary.md:5` says "multiple Payloads active simultaneously." Code tracks one active collection via `active.txt` (`content_store.h:5`) but `PayloadRecord` has no "active" flag.

---

## src/crypto

**Missing from docs:**
- **CRC32** — `crc32.h` implements software CRC32 (IEEE 802.3 / zlib reflected polynomial). Not mentioned in any doc.
- **Monocypher library** — Vendored at `crypto/monocypher/`. Provides ed25519, Argon2, BLAKE2b, X25519, ChaCha20, Poly1305, Elligator2. Docs mention "ed25519" and "Argon2" but not the full surface of crypto primitives available.
- **HMAC-SHA256** — `sha256.h:26` implements HMAC-SHA256, used for HKDF extract. Docs describe "HKDF-SHA256" but not the HMAC component explicitly.

**Stale in docs:**
- `key-reference.md` describes a PRK → PIC → CSK chain for content signing. The crypto module has no ECDSA or certificate parsing code — only ed25519 via Monocypher. The docs over-promise on crypto capabilities not implemented.

**Contradictions:**
- None found within src/crypto specifically. However, `collection_format.h:43` (src/content) declares `ecdsa_secp256k1_sha256` as signature algorithm, but the only signature verification code uses ed25519. The crypto module never implements secp256k1.

---

## src/diag

**Missing from docs:**
- **DiagCode enum** — `diag.h:11-42` defines 20+ stable diagnostic codes across categories (OTP, Policy, Storage, Content, Network). Not documented anywhere.
- **DiagStatus struct** — `diag.h:44-51`, pairs DiagCode with uint32 detail. Used throughout codebase as return type.
- **Ring-buffer logger** — `log.h:5-42`, 64-entry ring buffer, 120-byte messages, LogLevel enum. Not documented.
- **log_flush_uart()** — `log.h:35`, flushes to UART on firmware, stdout on host tests. Not documented.

**Stale in docs:**
- None. The diag subsystem has no corresponding documentation, so nothing is stale per se.

**Contradictions:**
- None found.

---

## src/filesystem

**Missing from docs:**
- **Flash partition map** — `flash_device.h:18-35`: 16 MB flash, 2 MB firmware + 14 MB FAT. Not documented.
- **FlashDevice abstraction** — Dual implementation: RP2350 hardware (`flash_device.cc:72-150`) and RAM-backed host test with power-loss injection (`flash_device.cc:11-66`). Not documented.
- **FatFs drive mapping** — `diskio.cc:4-8`: pdrv 0 = USB host MSC, pdrv 1 = internal flash FAT. Not documented.
- **USB drives read-only** — `diskio_tuh.cc:58-61`: USB drives return `RES_WRPRT` on write. Not documented.
- **FatVolume directory scaffold** — `fat_volume.h:8-12`: auto-creates `1:/collections/`, `1:/profiles/`, `1:/saves/`, `1:/system/` on first format. Partially in glossary but not the auto-format behavior.
- **fat_util.h** — Inline helpers `fat_read_file`, `fat_write_file`, `fat_file_exists`, `fat_delete_file`, `fat_ensure_dir`, `fat_append_bytes`, `fat_file_size`. Not documented.
- **Read-modify-write flash writes** — `diskio_flash.cc:48-85`: 4 KB erase blocks, stack buffer, read from XIP, patch, erase, write. Not documented.

**Stale in docs:**
- `glossary.md:13-14` mentions "optical drive (CD/DVD)" as a Source. No code in filesystem for optical media.
- `glossary.md:21` describes "Persistent Storage" including "cached network payloads". No code for network payload caching in filesystem.

**Contradictions:**
- `diskio.cc:16` has duplicate `#include "filesystem/diskio.h"`. (Code quality issue, not doc contradiction.)
- `diskio_flash.cc:16` also has duplicate `#include "filesystem/flash_device.h"`.
- `fat_volume.h:8-12` comment says directory layout includes `1:/system/` for "device key, policy, event log", but `glossary.md:21` says persistent storage contains "System Settings, User Profiles, save data, high scores, cached network payloads" — the event log is mentioned in code comments but not in glossary.

---

*Remaining directories (menu, msx/api, net, peripherals, platform, spine, store, usb) pending.*

---

## src/msx/menu

**Missing from docs:**
- **Menu Host ABI** — 16 KB SRAM mailbox protocol between RP2350 and Z80 stub. `MenuMailbox` class with `send_command()`, `pending()`, `tick()`. Commands: `MENU_CMD_PUT_TEXT`, `MENU_CMD_CLEAR`, `MENU_CMD_SET_MODE`, `MENU_CMD_READ_INPUT`, `MENU_CMD_GET_HOST_INFO`, `MENU_CMD_LAUNCH`, `MENU_CMD_SET_MODE`. Not documented.
- **Z80 menu stub** — `src/msx/menu/stub/` compiled with SDCC, embedded as `kMenuStubBin` via `menu_stub_bin.h`. Build target `build_menu_stub`. Not documented.
- **MenuApp state machine** — `menu_app.h`: Non-blocking tick() with per-screen step counters. Screens: BOOT, BOOT_INFO, MAIN, COLLECTIONS, SETTINGS, LAUNCH, RUNNING. Each screen steps through mailbox commands one per tick.
- **InputSnapshot** — `input_decoder.h`: Decoded Z80 input with `key_down()`, `key_up()`, `key_return()`, `key_esc()`, `key_any()`, `joy1_down()`, `joy1_up()`, `joy1_trig()` helper functions.
- **Auto-launch logic** — `menu_app.cc:140-149`: If `HostInfo` received and collection `boot_mode == 1`, skips menu and launches directly.

**Stale in docs:**
- None specific to menu.

**Contradictions:**
- `capabilities.md` lists `sw.menu` (ID 0x1020) as a software capability. No driver descriptor for `sw.menu` exists in `driver_descriptor_table.cc`. The menu is a built-in firmware component, not a registered capability.

---

## src/msx/api

**Missing from docs:**
- **API Window ring protocol** — `api_window.h`: H2C and C2H message rings with scratch areas for large data transfer. `ApiWindow` owns ring buffers and scratch memory in the shared 16 KB mailbox region.
- **Core service (0x00)** — `core_service.cc`: Handles `SYS_GET_BUILD_INFO` (returns `FW_BUILD_ID`), `SYS_GET_CAPABILITIES` (lists registered capabilities), `SYS_GET_DEVICE_ID` (returns DIK scoped ID for "dev" label).
- **Storage service (0x01)** — `storage_service.cc`: Handles `STORAGE_SETTINGS_GET`, `STORAGE_SETTINGS_SET`, `STORAGE_SAVE_LIST`, `STORAGE_SAVE_READ`, `STORAGE_SAVE_WRITE_BEGIN`, `STORAGE_SAVE_WRITE_CHUNK`, `STORAGE_SAVE_COMMIT`, `STORAGE_SAVE_DELETE`, `STORAGE_WIPE_USER`, `STORAGE_FULL_WIPE`. Delegates to `UserDataStore`.
- **Network service (0x02)** — `network_service.cc` (in src/net, registered as API service): `NET_STATUS`, `NET_HTTP_REQUEST`. Uses `TransportEspAt` for AT command transport.

**Stale in docs:**
- None specific to API.

**Contradictions:**
- None found within src/msx/api.

---

## src/net

**Missing from docs:**
- **ESP32 AT transport** — `transport_esp_at.h/cc`: UART0 on GPIO 44/45, 115200 baud. EN/BOOT pins NOT connected (hardcoded disconnected). Uses `AT+HTTPCLIENT` for HTTP requests. Key limitations documented in code: HTTP status code not returned (always 0), POST body encoding issues, ~1 KB command length limit, HTTPS terminates on ESP32 (not RP2350). ESP32 is an UNTRUSTED transport.
- **Network service** — `network_service.cc`: API service 0x02. `NET_STATUS` (method 0x00) returns connection state. `NET_HTTP_REQUEST` (method 0x01) sends HTTP via ESP32, writes response to C2H scratch area.
- **HTTP response transport** — Response body written directly to C2H scratch area via `ring_push_msg()` with `scratch_ofs`/`scratch_len`, not via ring buffer.

**Stale in docs:**
- `glossary.md:13-14` lists "network" as a Source. Code implements only HTTP client via ESP32 AT, not general network payload fetching. No network install path exists—USB is the only install source implemented.
- `overview.md:210-227` describes device-to-device sessions and leaderboard integrity. No code in src/net touches these.

**Contradictions:**
- `network_service.cc:3-4` has duplicate `#include "net/transport_esp_at.h"`.

---

## src/peripherals

**Missing from docs:**
- **Peripheral descriptor registry** — `peripheral_descriptor.cc`: PSG (type_id=1, 128B SRAM, IO 0xA0/3), SCC (type_id=2, 256B, memory-mapped), OPL4 (type_id=3, 5120B, IO 0x7C/4), V9990 (type_id=4, 2048B, memory-mapped), Sunrise IDE (type_id=5, 1024B, memory-mapped). V9990 and Sunrise IDE registered but have no emulation implementations.
- **PeripheralManager** — `peripheral_manager.h/cc`: Owns `PsgState`, `Opl4State`, `SccState`. `service_all()` order: SCC → OPL4 → PSG (PSG mixes). `map_menu_page()` maps 16 KB at MSX page 1 (0x4000-0x7FFF). `map_api_window()` maps 16 KB at page 2 (0x8000-0xBFFF) RO.
- **PSG emulation** — `psg.h/cc`: AY-3-8910, 16 registers, Q16 fixed-point tone counters, 17-bit LFSR noise, envelope generator. Self-rate-limited to ~44100 Hz.
- **SCC emulation** — `scc.h/cc`: Konami 5-channel wavetable, 4×32-byte waveform tables, runs at full MSX clock. Contains `mapper_setup_konami_scc()` — the function the bus audit said was missing.
- **OPL4 emulation** — `opl4.h/cc`: YMF278B, 24-channel PCM + OPL3 FM (18 channels). Wave ROM loaded from XIP flash via ContentStore. `wave_payload_id` from manifest device entries.
- **OPL3 FM synthesis** — `opl3fm.h/cc`: 36 operators, 18 channels. Per-operator envelope/phase state, per-channel routing. ENV_MAX=511.
- **Sunrise IDE emulation** — `sunrise_ide.h/cc`: ATA-IDE with Nextor ROM banking (512 KB, 32×16 KB banks). Supports DIAGNOSTIC, IDENTIFY, READ SECTOR, WRITE SECTOR (read-only). 512-byte sector buffer.

**Stale in docs:**
- None specific to peripherals.

**Contradictions:**
- **KONAMI_SCC mapper** — The bus audit claimed `mapper_setup_konami_scc()` is missing. It DOES exist in `scc.cc`. The function is declared in `scc.h:123` and implemented in `scc.cc`. `peripheral_manager.cc:68-74` calls it in `apply_mapping()` for `KONAMI_SCC` mapper type. The bus audit's contradiction section is wrong.
- `peripheral_manager.cc:8-9` has duplicate `#include "bus/bus.h"`.
- V9990 and Sunrise IDE are registered in the peripheral descriptor table but have no corresponding emulation state in `PeripheralManager` — they are described but not serviceable.

---

## src/platform

**Missing from docs:**
- **GPIO pin mapping** — `gpio_defs.h`: GPIO 0-15 address bus, 16-23 data bus, 24-31 control signals (WR/IORQ/MERQ/RD/BUSDIR/INT/WAIT/SLTSL), 32-47 high pins (CLK/SND/RESET/M1/RFSH/OLED/VIDEO/UART/VUSB/BATSENS). Bitmask enums for `sio_hw` register access.
- **Linker script** — `memmap_jlpicart.ld`: FLASH 1 MB at 0x10000000, FLASH1M 15 MB at 0x10100000, RAM 256 KB at 0x20000000, SCRATCH_X/Y 4 KB each. `.time_critical*` section placed in RAM (.data) for Core 0 XIP stall avoidance.
- **Board descriptor** — `board_descriptor_jlpicart.cc`: 8 declared capabilities: bus.msx, storage.ext_flash, net.wifi, io.usb_host, ui.oled, video.crt, audio.out, io.adc. Board ID: "jlpicart_v1".
- **jlpicart_board.h** — Pico SDK board header. Defines `PICO_RP2350A=0` (this is RP2350B). W25Q080 flash. Commented-out 4 MB flash size.
- **Platform::start()** — `platform_rp2350.cc:34-50`: Launches service function on Core 1 via `multicore_launch_core1()`, wires `BUS::reset_callback`, enters `BUS::start()` on Core 0.
- **MSX clock detection** — `platform_rp2350.cc:52-62`: Counts BIT64_CLK_HI transitions over 2 ms, threshold 10. Used to decide host vs device USB mode.

**Stale in docs:**
- None specific to platform.

**Contradictions:**
- `jlpicart_board.h` defines `PICO_RP2350A 0`, meaning it tells Pico SDK this is an RP2350A. The board uses RP2350B (dual-core). This could cause SDK to omit RP2350B-specific features.

---

## src/spine

**Missing from docs:**
- **Security posture** — `security_posture.h/cc`: Reads CRIT1 (0x040), BOOT_FLAGS0 (0x048), BOOT_FLAGS1 (0x04B) from OTP. `encrypted_boot_enabled` always false. `otp_device_secret_present` proxied from `boot_key_valid_mask != 0`.
- **OTP reader** — `otp_reader.h/cc`: Abstract interface with byte/uint8/uint24 access. OTP_BYTE_SIZE=12288 (4096 rows × 3 bytes). Hardware implementation reads from `OTP_DATA_GUARDED_BASE`. FakeOtpReader for host tests.
- **Policy system** — `policy.h/cc`: Binary PolicyDocument (48 bytes: version[4] + flags[8] + reserved[4] + hmac_tag[32]). Stored at 0x1FF000 (last 4 KB of 2 MB firmware partition). Dev HMAC key: `"JLPCart-dev-policy-hmac-key-v001"`. 9 policy flags defined. `POLICY_DEV_DEFAULTS` enables USB install, unsigned collections, user replace, boot key enrollment, stable device ID.
- **Device Identity Key** — `device_identity.h/cc`: ed25519 keypair, XChaCha20-Poly1305 encrypted storage. File format: pub[32] | nonce[24] | mac[16] | priv_ct[64] | flags[1] = 137 bytes. Path: 1:/system/dik.bin. Dual crypto backend: OpenSSL for host tests, Monocypher for hardware. Scoped ID: SHA-256(pub_key || label || 0x00) → first 16 bytes. Labels: "dev", "col", "pub".
- **Capability registry** — `capability_registry.h/cc`: Three-stage pipeline: Declared → Allowed → Activated. CAPABILITY_REGISTRY_MAX=32. Policy masking only gates "net.esp32" on `POLICY_ALLOW_NETWORK_COLLECTION_INSTALL`.
- **Allocator** — `allocator.h/cc`: Launch plan computer. REQUESTED_CAPS_MAX=16, LAUNCH_MAX_ACTIVATED=32. Sorts candidates alphabetically. HW capabilities probed if safe_verify=true.
- **Resource model** — `resource_model.h/cc`: RP2350 totals: 520 KB SRAM, 12 PIO SMs, 16 DMA channels. System reserves: 128 KB SRAM, 4 PIO SMs, 2 DMA. Available: 392 KB SRAM, 8 PIO SMs, 14 DMA.
- **Hardware probes** — `hw_probe.cc`: net.wifi = UART0 AT ping (1500 ms), ui.oled = I2C0 scan 0x3C/0x3D (deinit after). io.usb_host and io.adc always true (RP2350 silicon).
- **Driver descriptors** — `driver_descriptor_table.cc`: Three software capabilities: api.core (16384 SRAM), sw.mapper (0 resources), net.esp32 (0 resources).
- **Device compatibility checker** — `device_check.h/cc`: Checks IO port overlap, memory-mapped subslot conflicts, cumulative SRAM budget. DEVICE_CHECK_MAX=8.

**Stale in docs:**
- `key-reference.md:221-295` describes PRK → PIC → CSK chain. No code implements PRK, PIC, or CSK. The spine module only has ed25519 keypair generation (DIK) and OTP-based key derivation (SMK).
- `key-reference.md` describes SMK namespaces: sys, prof, saves, guest, dik.priv. Code in `smk.cc` only derives "dik.priv" and "policy" namespaces. "prof" and "guest" are not in the code.
- `pcb-builder.md:213-223` describes a JSON policy format with fields like `allow_usb_collection_install`, `allow_network_collection_install`. Code uses a binary 48-byte PolicyDocument with bitflags. No JSON parsing exists.
- `pcb-builder.md` describes policy recovery matrix and production checklist referencing concepts (CSK, PIC) not in code.

**Contradictions:**
- **Policy format**: `pcb-builder.md` documents JSON policy with named boolean fields. Code uses a compact binary format (version LE + flags LE + reserved + HMAC-SHA256 tag). The two are completely incompatible.
- **SMK namespaces**: `key-reference.md` lists five SMK namespaces (sys, prof, saves, guest, dik.priv). Code only implements two: "dik.priv" and "policy".
- **Device ID**: `key-reference.md` describes DIK scoped IDs via HKDF-SHA256. Code uses SHA-256(pub_key || label || 0x00) → first 16 bytes. These are different derivation functions.

---

## src/store

**Missing from docs:**
- **UserDataStore** — `user_data_store.h/cc`: Single-device, no profiles/no sync/no cloud. SystemSettings (192 bytes) stored at 1:/system/settings.bin. Save blobs at 1:/saves/{id:04x}.sav with index at 1:/saves/index.bin.
- **SystemSettings** — WiFi SSID/password, language, video mode (auto/crt/vga), network enabled, source priority array [FLASH, USB, OPTICAL, NETWORK].
- **Save system** — 4 concurrent write handles, 512-byte max blob size. Registry entries are 6 bytes each (blob_id + flags + size), max 85 entries. Staging buffer for atomic writes.
- **Factory reset** — `wipe_user_data()` deletes 1:/saves/ contents and directory. `full_wipe()` also deletes settings.bin and resets to defaults.

**Stale in docs:**
- `glossary.md:21` describes "Persistent Storage" including "User Profiles, guest sessions, cached network payloads." Code has no per-profile data, no guest sessions, no network payload caching.
- `glossary.md:7-8` describes "User Profile" as a named, persistent configuration. Code has one implicit user—no profile concept.

**Contradictions:**
- `SystemSettings.source_priority` includes `SYS_SRC_OPTICAL = 2u` and `SYS_SRC_NETWORK = 3u`. No code path installs collections from optical or network sources—only USB is implemented via `UsbInstallScanner`. The defaults list sources that don't exist.
- `glossary.md:3` says "Only one Collection is active at a time." `glossary.md:5-6` describes "User Profile" with multiple profiles per device. Code has zero profile support—one implicit user.

---

## src/usb

**Missing from docs:**
- **USB host mode** — `usb_host.h/cc`: Initializes tinyusb host stack with `tuh_init(BOARD_TUH_RHPORT)`. Tracks MSC mount state. `on_mount()`/`on_umount()` delegates from tinyusb callbacks.
- **USB device mode** — `usb_device.cc`: VID 0x1209 (pid.codes registered), PID 0x4A4C ("JL"), serial "000001" fixed. Exposes internal flash FAT partition as 14 MB MSC drive. `run()` is [[noreturn]].
- **tinyusb config** — `tusb_config.h`: OPT_MCU_RP2040 (same USB core as RP2350). Both host and device stacks compiled. Host: MSC only, no HUB/CDC/HID. Device: MSC only, EP0 64 bytes, MSC EP 512 bytes. Cooperative mode (no RTOS).
- **InstallReader USB backend** — `usb_install_reader.cc`: FatFs over USB MSC, 512-byte chunk reads. `hash_file()` streams through Sha256Ctx. `copy_to_fat()` chunked copy USB→flash.
- **Install scanner** — `usb_install_scanner.cc`: Scans 0:/JLPICART/INSTALL/ for collection directories. Policy-gated on POLICY_ALLOW_USB_COLLECTION_INSTALL. MSC-mount-gated—only runs when USB drive is present.

**Stale in docs:**
- None specific to USB.

**Contradictions:**
- `usb_device.cc` and `usb_host.cc` can both be compiled and linked (both in CMakeLists.txt), but they are mutually exclusive at runtime—`Platform::msx_clock_present()` decides which mode to initialize. The tinyusb library doesn't support simultaneous host+device on RP2350. This works because only one path is taken, but it wastes ~10 KB of flash including the unused stack.

---

## Cross-cutting findings

**Capabilities catalog mismatch** — `capabilities.md` lists capabilities with no code implementation:
- `net.eth` (0x0202) — No Ethernet driver, hardware probe, or board capability.
- `io.rs232` (0x0303) — No serial/RS-232 driver.
- `ui.eink` (0x0402) — No e-ink display driver.
- `storage.mass` (0x0102) — No mass storage capability registered (USB MSC exists but not as a capability).
- `sw.menu` (0x1020) — Menu is built-in firmware, not a registered driver capability.

**Capability flag encoding** — `capabilities.md:174-177` describes cap_flags as bit 0=activated, bit 1=hw-backed, bit 2=probe-succeeded. Code's `CapabilityEntry` uses separate `bool` fields: `is_hw`, `allowed`, `activated`. No bitwise flag packing exists.

**Monocypher surface** — The crypto module vendors Monocypher with ed25519, Argon2, BLAKE2b, X25519, ChaCha20, Poly1305, and Elligator2. Docs mention only "ed25519" and "Argon2". The full primitive surface is not documented.

**CRC32** — `crypto/crc32.h` implements software CRC32 (IEEE 802.3 / zlib reflected polynomial). Not mentioned in any documentation.

**Diag subsystem** — `diag.h` defines 20+ stable diagnostic codes (OTP, Policy, Storage, Content, Network categories), `DiagStatus` struct, ring-buffer logger (64 entries, 120-byte messages), and `log_flush_uart()`. None of this is documented.

**Duplicate includes** — Found in `network_service.cc:3-4`, `peripheral_manager.cc:8-9`, `diskio.cc:16`, `diskio_flash.cc:16`.

---

*Audit complete — all src/ subdirectories reviewed.*
