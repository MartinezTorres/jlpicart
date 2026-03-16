# JLPiCart Firmware Documentation

This directory contains user-facing documentation for the JLPiCart firmware platform.
The normative specification lives in `fw/spec.md`. These documents explain *how to use*
the platform, not *what it guarantees* — though they cross-reference the spec wherever
precise language matters.

## Who should read what

**I built (or am building) a PCB**
Start with [security/overview.md](security/overview.md), then read
[security/pcb-builder.md](security/pcb-builder.md). That guide walks from "I just
assembled the board" through to "ready to ship a sealed unit."

**I want to publish a game or application**
Read [security/overview.md](security/overview.md) for the trust model, then
[security/publisher.md](security/publisher.md) for signing, certificates, and
distribution.

**I want to bundle ROMs or other content**
Read [security/collection-author.md](security/collection-author.md). You probably
don't need to read anything else unless your collection requires publisher-signed
content.

**I am writing MSX software that uses the JLPiCart API**
Start with [api/getting-started.md](api/getting-started.md) (coming soon), and use
[api/capabilities.md](api/capabilities.md) as a reference for what the cartridge
can provide.

**I want to understand the key hierarchy in depth**
Read [security/key-reference.md](security/key-reference.md).

## Document status

| Document | Status | Covers |
|---|---|---|
| [security/overview.md](security/overview.md) | Draft | Trust model, personas, key roles |
| [security/pcb-builder.md](security/pcb-builder.md) | Draft | OTP provisioning, development vs. production |
| [security/key-reference.md](security/key-reference.md) | Draft | All key types, storage, lifecycle |
| [security/publisher.md](security/publisher.md) | Planned | PIC, CSK, signing bundles |
| [security/collection-author.md](security/collection-author.md) | Planned | Unsigned/signed collections |
| [api/capabilities.md](api/capabilities.md) | Draft | Capability ID catalog |
| [api/getting-started.md](api/getting-started.md) | Planned | First API program |
| [firmware/building.md](firmware/building.md) | Planned | Building from source |
| [firmware/provisioning.md](firmware/provisioning.md) | Planned | Factory provisioning workflow |

## Relationship to spec.md

`spec.md` defines *contracts* — what the platform MUST and MUST NOT do. These guides
explain *how to work with the platform* given those contracts. When there is any
conflict between a guide and the spec, the spec is authoritative.
