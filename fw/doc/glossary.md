# Glossary

**Collection** — A named bundle containing a `manifest.json`, one or more Payloads, and
an optional `bundle.sig`. Installed to `1:/collections/`. Only one Collection is active
at a time, tracked via `1:/collections/active.txt`.

**Payload** — A single emulatable media item within a Collection. Consists of ROM data
stored in flash and a `PayloadRecord` (372 bytes) that describes mapper type, subslot,
device requirements, and flash offset. A Collection can have multiple Payloads; the
active one is set via the API before launch.

**Content** — The primary data of a Payload: a ROM image written to the FAT partition
of external flash and mapped into MSX memory space via the bus emulator.

**Manifest** — `manifest.json`: structured metadata describing a Collection and its
Payloads. Includes identity (collection_id, title, version, publisher_id), per-payload
details (mapper type, subslot, device entries, required/optional capabilities), and
boot defaults (boot_mode, default_payload_id). Parsed by `ManifestParser` at install time.

**Bundle Signature** — `bundle.sig`: an optional JSON file alongside `manifest.json`
containing an ed25519 signature over the manifest's SHA-256 digest, algorithm identifier
(`"ed25519"`), key ID, file hashes, and raw 64-byte signature.

**Source** — Where a Collection is loaded from. The implemented source is USB mass
storage (via tinyusb host MSC). The `SystemSettings.source_priority` array lists
preferred sources: flash, USB, optical, network — but only USB install is currently
implemented via `UsbInstallScanner`.

**System Settings** — Cartridge-wide configuration stored at `1:/system/settings.bin`.
192-byte flat blob: WiFi SSID/password, language, video output mode, network enabled,
source priority array.

**Persistent Storage** — Flash storage that survives Collection changes: System Settings,
user save data, Device Identity Key, publisher trust anchor. Backed by a 14 MB FAT
partition on external W25Q080 flash.

**Save Data** — Per-game save blobs stored at `1:/saves/{id:04x}.sav`. Indexed by
`1:/saves/index.bin` (registry of blob_id, flags, size). Maximum 512 bytes per blob,
up to 85 entries. Managed by `UserDataStore`.

**JLPiCart API** — The stable, versioned interface exposed by the cartridge to MSX
software via the API Window (ring-based IPC). Services: SYSTEM (0x00) for build info,
capabilities, device ID; STORAGE (0x01) for settings and save operations; NETWORK
(0x02) for ESP32 HTTP client.

**Menu** — The cartridge's own software, not part of any Collection. Runs as a
non-blocking state machine on the RP2350 and communicates with a Z80 menu stub via a
16 KB SRAM mailbox protocol. Handles Collection selection, settings display, and launch.

**Install Intent** — A single install directory on a USB mass storage device
(`/JLPICART/INSTALL/<install_id>/`) containing `manifest.json`, optional `bundle.sig`,
and payload data files. Scanned by `FatFsInstallDirSource` and installed by `Installer`.

**Publisher Trust Anchor** — A raw 32-byte ed25519 public key stored at
`1:/system/pub_anchor.bin`. Used to verify collection bundle signatures at install time.
Loaded by `policy_get_publisher_anchor()`.

**Lock Policy** — The device's trust posture, encoded as a 64-bit bitmask in a binary
`PolicyDocument` (48 bytes) stored in flash at offset `0x1FF000`. Authenticated with
HMAC-SHA256. Controls USB/network install permissions, unsigned collection acceptance,
publisher signature requirement, and device ID exposure.
