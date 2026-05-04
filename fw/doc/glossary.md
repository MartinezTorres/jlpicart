# Glossary

**Collection** — A named bundle containing a Manifest, zero or more Payloads, and optional Assets. Only one Collection is active at a time.

**Payload** — A single emulatable media item within a Collection, consisting of its Content, a Manifest, and optional Assets. A Collection may have multiple Payloads active simultaneously (e.g., a ROM plus a floppy bundle plus a mass storage image).

**Content** — The primary data of a Payload: a ROM image, a mass storage image, or a floppy bundle (one or more floppy disk images).

**Manifest** — Structured textual metadata describing a Collection or Payload. Includes identity (name, author, version), hardware requirements (mapper, RAM, audio devices, VDP, network access), licensing terms, and boot defaults. Hard requirements cannot be overridden by the user; soft defaults can.

**Assets** — Optional non-textual media associated with a Collection or Payload, used by the Menu: images, animations, music.

**Source** — Where a Collection is loaded from. Sources are: internal flash, USB stick, optical drive (CD/DVD), or network. External sources take priority over internal flash.

**System Settings** — Cartridge-wide configuration that persists across Collections: WiFi credentials, video output mode, language, firmware settings.

**User Profile** — A named identity stored on the cartridge, optionally synced to the cloud. Contains language preference, save data, and high scores across all Collections. Multiple profiles may exist on one cartridge.

**Guest Session** — A temporary profile tied to a licensed user's Collection. Allows a user without a copy of a Collection to play it, subject to publisher-defined limits (time, sessions, features).

**Persistent Storage** — Flash storage that survives Collection changes: System Settings, User Profiles, save data, high scores, cached network payloads.

**JLPiCart API** — The stable, versioned interface exposed by the cartridge to MSX software. Provides games and applications access to: user identity, save/load operations, high score submission, network sockets, peripheral queries, and multiplayer session management.

**Menu** — The cartridge's own software, not part of any Collection. Handles Collection selection, System Settings, User Profile management, and publisher tools.

**Provisioning Bundle** — A signed installation bundle used to initialize a blank device and optionally install a default Collection and lock policy.

**Install Intent** — A single install directory on provisioning media (`/JLPICART/INSTALL/<install_id>/`) containing metadata, publisher identity, bundle bytes, and signatures.

**Install Receipt** — An append-only local record describing an installation decision (installed/rejected), including bundle digests and stable reason codes.

**Publisher Identity Certificate (PIC)** — A certificate binding a stable Publisher ID to public keys and allowed usages, validated against a Publisher Root Key (PRK).

**Lock Policy** — The device's trust posture defining which signers are accepted for firmware and content and what recovery/rotation is permitted.
