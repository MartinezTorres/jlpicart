# JLPiCart Security Overview

This document explains the mental model behind JLPiCart's security system: what it
protects, what it deliberately does not protect, who the actors are, and how trust
flows through the platform.

Read this before reading any of the persona-specific guides. The goal is to give you
a complete map before you need to navigate any part of it.

## What the platform protects (and what it does not)

JLPiCart is an MSX cartridge. When it is running a ROM, the ROM bytes are being driven
on the MSX address bus in real time. A determined attacker with a logic analyzer and
some patience can capture those bytes. **This is a known non-goal.** The platform does
not attempt to prevent bus-level extraction because it cannot: the MSX bus is by design
an open, shared bus.

What the platform *does* protect:

- **Secrets at rest.** Private keys, WiFi credentials, and user save data stored in
  flash are encrypted under a device-specific key derived from the RP2350 OTP. Someone
  who removes the flash chip cannot read the data without also extracting the OTP secret,
  which requires invasive lab techniques.

- **Authentic firmware.** The RP2350 secure boot mechanism verifies every firmware image
  against enrolled public key fingerprints in OTP before executing any code. A tampered
  or forged firmware will not run on a sealed unit.

- **Authentic collections.** Publishers can sign their content bundles with their own
  Content Signing Key. The platform verifies the signature before installing. On devices
  configured to require publisher signatures, unsigned content is rejected.

- **Leaderboard integrity.** Score submissions are tied to a per-device identity and
  include a server-issued single-use token. The server rejects replays and unsigned
  submissions.

- **Safe updates.** Firmware updates are installed into a standby slot and boot-tested
  before being committed, so a failed update does not brick the device.

## The three personas

Three different people interact with the security system in different ways.

**PCB builder / hardware developer**
You assembled or designed the board. You control the OTP configuration — including
whether secure boot is enabled, which boot keys are enrolled, and whether a device
secret is programmed. Your decisions here determine how strong the security properties
of every unit you produce will be. See [pcb-builder.md](pcb-builder.md).

**Publisher**
You write software (games, applications, tools) or curate collections of software.
You use a Content Signing Key (CSK) to sign your collection bundles. You may also
obtain a Publisher Identity Certificate (PIC) to establish a persistent, verifiable
publisher identity across cartridges. See [publisher.md](publisher.md).

**Collection author**
You bundle existing ROMs and other content into a collection for distribution. In the
simplest case you do not need any keys at all — unsigned collections work fine on
cartridges where `allow_unsigned_collections` is set. If you want your collection to
be installable on locked-down units, you need a publisher signature. See
[collection-author.md](collection-author.md).

## Development mode versus sealed mode

There is no separate development firmware binary. The same firmware image runs in both
development mode and sealed mode. The difference is entirely in the OTP configuration
of the unit.

**Development mode** is any unit where secure boot is not enabled and no OTP device
secret has been programmed. This includes fresh RP2350 chips and prototype boards.
In this mode:
- Any firmware can be flashed, including unsigned builds.
- The Device Identity Key (DIK) is generated and stored in flash, but without OTP
  backing — it is labeled "unprovisioned." This means it is not protected by
  hardware-rooted encryption.
- Collections install freely if `allow_unsigned_collections` is set in policy.
- All API services work, including GET_DEVICE_ID. Leaderboard submissions are
  produced and signed by the DIK, but the server knows the device is unprovisioned.
- The Menu shows the security status clearly: "Development unit — not sealed."

**Sealed mode** is a unit with secure boot enabled and a device secret programmed in
OTP. In this mode:
- Only firmware signed by an enrolled boot key runs. Unsigned firmware is rejected
  at startup by the RP2350 boot ROM.
- The Device Identity Key private key is wrapped under a key derived from the OTP
  device secret. Extracting it requires the OTP — which does not leave the chip.
- Policy flags control what installs are accepted and how strictly.
- The Menu shows: "Sealed unit — secure boot active."

The transition from development to sealed is one-way and requires deliberate action
(OTP writes). See [pcb-builder.md](pcb-builder.md) for the exact steps.

## The key hierarchy

Six distinct key types serve distinct roles. Keys are never reused across roles.

```
                        ┌─────────────────────┐
                        │  RP2350 OTP          │
                        │  Boot key slots 0–3  │  (public key fingerprints)
                        └──────────┬──────────┘
                                   │ verifies boot signature
                                   ▼
                        ┌─────────────────────┐
                        │  Firmware image      │  (signed by FSK)
                        │  (trusted code)      │
                        └──────────┬──────────┘
                                   │ reads
                                   ▼
                        ┌─────────────────────┐
                        │  RP2350 OTP          │
                        │  Device secret seed  │  (one-time programmed)
                        └──────────┬──────────┘
                                   │ HKDF-SHA256
                                   ▼
                        ┌─────────────────────┐         ┌────────────────────┐
                        │  Storage Master Key  │         │  Device Identity   │
                        │  (SMK)               │         │  Key (DIK)         │
                        │  symmetric, RAM-only │         │  ed25519 keypair   │
                        └──────────┬──────────┘         └─────────┬──────────┘
                                   │ per-namespace HKDF            │
                   ┌───────────────┼───────────────┐               │ signs
                   ▼               ▼               ▼               ▼
             ┌─────────┐    ┌──────────┐    ┌──────────┐    score submissions,
             │ SYSTEM  │    │ PROFILES │    │  SAVES   │    device-to-device
             │ key     │    │ key      │    │  key     │    sessions, leaderboards
             └─────────┘    └──────────┘    └──────────┘

                                              (offline, publisher-controlled)
                                   ┌──────────────────────────────┐
                                   │  Publisher Root Key (PRK)    │
                                   └──────────────┬───────────────┘
                                                  │ signs
                                                  ▼
                                   ┌──────────────────────────────┐
                                   │  Publisher Identity Cert     │
                                   │  (PIC)                       │
                                   └──────────────┬───────────────┘
                                                  │ authorizes
                                                  ▼
                                   ┌──────────────────────────────┐
                                   │  Content Signing Key (CSK)   │
                                   │  signs collection bundles    │
                                   └──────────────────────────────┘
```

**Boot Root Keys (BRK)** — stored as fingerprints in OTP. The RP2350 boot ROM
verifies the firmware image signature against these before running any code.
Enrollment is irreversible. See [key-reference.md](key-reference.md).

**Firmware Signing Key (FSK)** — the private key used to sign firmware images.
Kept offline by the platform maintainer or board builder. The corresponding
public key fingerprint is enrolled in OTP slot 0 (platform key) or slots 1–3
(builder/publisher keys).

**OTP device secret seed** — a small random value programmed once into OTP. The
firmware reads it at boot, derives the Storage Master Key, then uses the SMK to
derive per-namespace encryption keys. The secret never leaves the chip.

**Storage Master Key (SMK)** — derived at every boot from the OTP device secret
using HKDF-SHA256. Never written to flash. Exists only in RAM while the firmware
is running. Used to derive a separate encryption key for each storage namespace
(SYSTEM, PROFILES, SAVES, GUEST).

**Device Identity Key (DIK)** — an ed25519 keypair generated on first boot. The
public key is stored in flash in plain text. The private key is stored encrypted
under an SMK-derived key. On unprovisioned units (no OTP secret), the private
key is stored without hardware-rooted encryption and the unit is marked
"unprovisioned." The DIK never leaves the device.

**Publisher Root Key (PRK)** — a key held by the JLPiCart platform or a delegating
authority. It signs Publisher Identity Certificates. On open platforms where any
publisher can distribute content, this key is not strictly required. On locked-down
units, only publishers with a valid PIC chain are accepted.

**Publisher Identity Certificate (PIC)** — a certificate binding a stable publisher
identity to one or more Content Signing Keys. Carried inside collection bundles.
Verified against PRK when `require_publisher_signature` is enabled.

**Content Signing Key (CSK)** — the publisher's working key for signing collection
bundles. Kept offline by the publisher. The corresponding public key is included in
the PIC.

## How trust flows at collection install time

When a collection bundle arrives (from USB, network, or optical drive), the firmware
follows these steps:

1. Check `allow_usb_collection_install` (or the relevant source flag) in the signed
   policy. If not set, reject immediately.

2. Parse the bundle header and locate the signature envelope.

3. If `require_publisher_signature` is set in policy:
   a. Extract the PIC from the bundle.
   b. Verify the PIC against the enrolled Publisher Root Key.
   c. Verify the bundle signature against the CSK in the PIC.
   d. If any step fails, reject.

4. If `allow_unsigned_collections` is set and no PIC is present, accept.

5. Record an install receipt (in the append-only event log) with the bundle digest,
   the decision (accepted/rejected), and the reason.

6. Install the collection atomically. If power is lost mid-install, the old collection
   remains valid.

## How the Device Identity Key is used

The DIK is the device's persistent cryptographic identity. It is used for:

- **GET_DEVICE_ID** — the API returns a scoped ID derived from the public key using
  HKDF. The scope controls how stable and trackable the ID is: scope 0 gives a
  stable device ID (requires policy permission), scopes 1–2 give per-collection and
  per-publisher IDs.

- **Leaderboard submissions** — the firmware signs the run token and score with the
  DIK private key. The server verifies the signature before accepting the submission.

- **Device-to-device sessions** — two cartridges authenticate each other using their
  DIKs and establish an encrypted session for local multiplayer or data exchange.

The DIK can be rotated (a new keypair generated and enrolled with the server), but
the old identity is then retired and any server-side associations must be migrated.
On unprovisioned units, the DIK private key is stored without hardware-level
encryption. This is acceptable for development. It is not acceptable for shipping
units that participate in online leaderboards.
