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

- **Authentic collections.** Collections can be signed with an ed25519 keypair. The
  platform stores a raw 32-byte public key at `1:/system/pub_anchor.bin` and verifies
  bundle signatures against it before installing. On devices configured to require
  publisher signatures, unsigned content is rejected.

## The two personas

Two different people interact with the security system in different ways.

**PCB builder / hardware developer**
You assembled or designed the board. You control the OTP configuration — including
whether secure boot is enabled, which boot keys are enrolled, and whether a device
secret is programmed. Your decisions here determine how strong the security properties
of every unit you produce will be. See [pcb-builder.md](pcb-builder.md).

**Collection publisher / author**
You bundle ROMs and other content into a collection for distribution. You can sign your
collection bundles with an ed25519 private key. Cartridges that have a matching public
key in `pub_anchor.bin` will accept your signed collections. In the simplest case,
unsigned collections work on cartridges where `allow_unsigned_collections` is set in
policy.

## Development mode versus sealed mode

There is no separate development firmware binary. The same firmware image runs in both
development mode and sealed mode. The difference is entirely in the OTP configuration
of the unit.

**Development mode** is any unit where secure boot is not enabled and no OTP device
secret has been programmed. This includes fresh RP2350 chips and prototype boards.
In this mode:
- Any firmware can be flashed, including unsigned builds.
- The Device Identity Key (DIK) is generated and stored in flash, but without OTP
  backing — it is labeled "unprovisioned." The private key is wrapped with a zero-derived
  key (effectively unencrypted).
- Collections install freely if `allow_unsigned_collections` is set in policy. In DEV
  mode, the firmware applies `POLICY_DEV_DEFAULTS` which permits USB installs and
  unsigned collections.
- All API services work, including GET_DEVICE_ID.
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

Four distinct key types serve distinct roles. Keys are never reused across roles.

```
                    ┌─────────────────────┐
                    │  RP2350 OTP          │
                    │  Boot key slots 0-3  │  (public key fingerprints)
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
                               │ HKDF-SHA256 (info="jlpicart.smk")
                               ▼
                    ┌─────────────────────┐
                    │  Storage Master Key  │
                    │  (SMK)               │
                    │  symmetric, RAM-only │
                    └──────────┬──────────┘
                               │ HKDF-SHA256 (info="jlpicart.ns.dik.priv")
                               ▼
                    ┌─────────────────────┐         ┌────────────────────┐
                    │  DIK wrap key       │         │  Device Identity   │
                    │  (RAM-only)         │         │  Key (DIK)         │
                    │  encrypts DIK priv  │         │  ed25519 keypair   │
                    └─────────────────────┘         └────────────────────┘

                    (separate, publisher-controlled)
                    ┌─────────────────────┐
                    │  Publisher ed25519  │
                    │  signing keypair    │
                    │  (offline)          │
                    └──────────┬──────────┘
                               │ signs
                               ▼
                    ┌─────────────────────┐
                    │  Collection bundle  │
                    │  manifest.json +    │
                    │  bundle.sig         │
                    └─────────────────────┘

                    ┌─────────────────────┐
                    │  pub_anchor.bin     │  (32-byte ed25519 public key)
                    │  1:/system/         │  verifies bundle.sig
                    └─────────────────────┘
```

**Boot Root Keys (BRK)** — stored as fingerprints in OTP. The RP2350 boot ROM
verifies the firmware image signature against these before running any code.
Enrollment is irreversible. See [key-reference.md](key-reference.md).

**Firmware Signing Key (FSK)** — the private key used to sign firmware images.
Kept offline by the platform maintainer or board builder. The corresponding
public key fingerprint is enrolled in OTP slot 0 (platform key) or slots 1-3
(builder/publisher keys).

**OTP device secret seed** — a small random value programmed once into OTP. The
firmware reads it at boot, derives the Storage Master Key (SMK) via HKDF-SHA256
with info `"jlpicart.smk"`, then uses the SMK to derive namespace-specific
encryption keys. The secret never leaves the chip.

**Storage Master Key (SMK)** — derived at every boot from the OTP device secret.
Never written to flash. Exists only in RAM while the firmware is running. Currently
used to derive one namespace key: `dik.priv` (for DIK private key wrapping). The
derivation function is `smk_derive_ns_key(smk, "dik.priv")` which expands to
HKDF-SHA256 with info `"jlpicart.ns.dik.priv"`.

**Device Identity Key (DIK)** — an ed25519 keypair generated on first boot. The
public key is stored in flash in plain text. The private key is stored encrypted
using XChaCha20-Poly1305 (Monocypher) under an SMK-derived wrap key. On unprovisioned
units (no OTP secret), the private key is stored without hardware-rooted encryption
and the unit is marked "unprovisioned." The DIK never leaves the device. Stored as
a single file: `1:/system/dik.bin` (137 bytes: pub[32] | nonce[24] | mac[16] | priv_ct[64] | flags[1]).

**Publisher signing key** — an ed25519 keypair held offline by the collection
publisher. The private key signs `manifest.json` at bundle creation time. The
corresponding public key is stored on-device as `1:/system/pub_anchor.bin` (raw
32-byte ed25519 public key). The firmware verifies the `bundle.sig` signature
against this anchor before installing signed collections.

## How trust flows at collection install time

When a collection bundle arrives (from USB), the firmware follows these steps:

1. Check `allow_usb_collection_install` in the active policy. If not set, reject
   immediately.

2. Parse `manifest.json` from the bundle directory.

3. Parse `bundle.sig` (if present) to get the signature envelope: algorithm, key ID,
   file hashes, and raw signature bytes.

4. If `require_publisher_signature` is set in policy:
   a. Load the publisher anchor public key from `1:/system/pub_anchor.bin`.
   b. Compute SHA-256 of the `manifest.json` bytes.
   c. Verify the ed25519 signature in `bundle.sig` against the anchor public key.
   d. If verification fails or anchor is missing, reject.

5. If `allow_unsigned_collections` is set and no signature is present, accept.

6. Install the collection atomically: write all files to a staging directory, then
   commit by writing `1:/collections/active.txt`. Power loss before the commit
   leaves the old collection intact.

## How the Device Identity Key is used

The DIK is the device's persistent cryptographic identity. It is used for:

- **GET_DEVICE_ID** — the API returns a scoped 16-byte ID derived from the public key.
  The derivation is SHA-256(pub_key[32] || label[3] || 0x00[1]), truncated to 16 bytes.
  Labels: `"dev"` (scope 0, stable device ID, requires policy permission),
  `"col"` (scope 1, per-collection), `"pub"` (scope 2, per-publisher).

The DIK can be rotated (a new keypair generated), but the old identity is then retired.
On unprovisioned units, the DIK private key is stored without hardware-level encryption.
This is acceptable for development.
