# JLPiCart Firmware Documentation

## Who should read what

**I built (or am building) a PCB**
Start with [security/overview.md](security/overview.md), then
[security/pcb-builder.md](security/pcb-builder.md). That guide walks from
"I just assembled the board" to "ready to ship a sealed unit."

**I want to publish a game or application**
Read [security/overview.md](security/overview.md) for the trust model, then
[security/key-reference.md](security/key-reference.md) for cryptographic
specifications. Publisher signing tooling is not yet documented.

**I am writing MSX software that uses the JLPiCart API**
Read [api/capabilities.md](api/capabilities.md) for the capability catalog
and the declared → allowed → activated lifecycle.

**I need the platform vocabulary**
Read [glossary.md](glossary.md).

**I want to understand the key hierarchy in depth**
Read [security/key-reference.md](security/key-reference.md).

## Document status

| Document | Status | Covers |
|---|---|---|
| [security/overview.md](security/overview.md) | Draft | Trust model, personas, key roles |
| [security/pcb-builder.md](security/pcb-builder.md) | Draft | OTP provisioning, development vs. production |
| [security/key-reference.md](security/key-reference.md) | Draft | All key types, storage, lifecycle |
| [api/capabilities.md](api/capabilities.md) | Draft | Capability ID catalog |
| [glossary.md](glossary.md) | Draft | Platform vocabulary |
| [toolchain.md](toolchain.md) | Current | SDK setup, build, flash, host tests |
