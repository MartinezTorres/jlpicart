# JLPiCart bootstrapping plan (agent checklist edition)

This document ships alongside `spec.md`. It is written for an implementation agent and for human supervision.

Guiding principles:

- Implement the **spine contracts first**, then hang features off them.
- Avoid “hidden glue”: every cross-cutting rule must live in one obvious module.
- The build must ignore legacy code placed under `old_src/`.

This plan assumes Linux amd64.

---

## Document conventions

### Stage rules

- One stage ≈ one PR.
- Every stage ends in a runnable firmware image.
- Stages are implemented in order unless a later stage explicitly says it can be done earlier.
- If something is ambiguous, implement according to `spec.md`, and update this plan in the same PR before continuing.

### File list rules

- “Files added” means these paths must exist after the stage (thin is OK).
- “Files modified” means these are the only allowed edits unless the checklist explicitly allows more.

### Testing rules

- Host tests live under `fw/tests/host/` and run in CI.
- Emulator-driven tests live under `fw/tests/openmsx/` and run in CI once openMSX is available.
- Every stage adds at least one host test unless explicitly marked hardware-only.

### Style rules inside this file

- Paths and symbols are written with inline code (``like_this``).
- Multi-line commands go in code blocks tagged as `sh`.
- Checklists use `- [ ]` everywhere (no mixed bullet styles).
- Each checklist section is numbered as `N.1`, `N.2`, … for the stage `N`.

---

## Stage 1 — Repo hygiene: isolate legacy sources and make the new tree authoritative

**Goal:** The repo builds from the new `fw/src/` only; legacy is reference-only.

### Files added

- `spec.md` — copy the current spec book verbatim (canonical version)
- `bootstrapping.md` — copy this plan verbatim
- `old_src/README.md` — “reference only; not compiled”

### Files modified

- Repo structure (move legacy sources)
- `fw/CMakeLists.txt` (and any nested `CMakeLists.txt`)

### Checklist

**1.1 Move legacy code**
- [ ] Create `old_src/` at repo root (or `fw/old_src/`) and choose one location permanently
- [ ] Move *all* legacy firmware sources under `old_src/` without changing their internal relative layout
- [ ] Add `old_src/README.md` with a single clear statement that it is not compiled

**1.2 Create the new build root**
- [ ] Create a new `fw/src/` tree with a placeholder `fw/src/main.cc`
- [ ] Update CMake so only `fw/src/**` is compiled

**1.3 Guardrails**
- [ ] Add a CI/build guard that fails if any `old_src` path is present in include paths
- [ ] Verify locally that the new firmware compiles and nothing under `old_src/` is referenced

### Definition of done

- [ ] CI builds firmware from the new tree
- [ ] Build logs show no `old_src` include paths

---

## Stage 2 — Pinned toolchain and CI (Pico SDK + openMSX 21 + SDCC 4.5)

**Goal:** Anyone (and CI) can build deterministically with pinned tools on Linux amd64, ignoring system `openmsx` and `sdcc`.

### Files added

**Core build and tests**
- `.github/workflows/fw-build.yml` — builds firmware, runs host tests, builds openMSX, runs openMSX tests (once present)
- `fw/toolchain/README.md` — pins Pico SDK and build commands
- `fw/tests/CMakeLists.txt` — host test build + `ctest` integration
- `fw/tests/host/smoke_test.cc` — trivial test to validate the harness

**Pinned tools**
- `.gitmodules` — includes the openMSX submodule entry
- `third_party/openMSX/` — git submodule pinned to openMSX 21.x commit used by this repo
- `tools/lock.yml` — single source of truth: tool versions, URLs, SHA256, openMSX commit
- `tools/fetch_sdcc.sh` — download SDCC tarball, verify SHA256, extract to `tools/sdcc/`
- `tools/build_openmsx.sh` — build openMSX from submodule, install to `tools/openmsx/`
- `tools/README.md` — explains the pinned tool approach and “ignore system tools” policy

### Files modified

- `fw/CMakeLists.txt`

### Checklist

**2.1 Pico SDK pinning**
- [ ] Pin Pico SDK (submodule or FetchContent) to a fixed tag/commit
- [ ] Record the pinned version in `tools/lock.yml` and `fw/toolchain/README.md`
- [ ] Enable `-Werror` for new code (host builds at minimum)

**2.2 openMSX as submodule**
- [ ] Add `third_party/openMSX` submodule from `https://github.com/openMSX/openMSX.git`
- [ ] Pin it to an openMSX 21.x commit and record the commit hash in `tools/lock.yml`
- [ ] Add `tools/build_openmsx.sh` that:
  - [ ] updates submodules if needed
  - [ ] builds openMSX in a dedicated build dir
  - [ ] installs or copies the resulting binary into `tools/openmsx/bin/openmsx`
  - [ ] prints `openmsx --version` after build
- [ ] Ensure CI uses `tools/openmsx/bin/openmsx` explicitly (never `openmsx` from PATH)

**2.3 SDCC pinned tarball**
- [ ] Choose the SDCC 4.5.x prebuilt tarball URL and record it in `tools/lock.yml`
- [ ] Record the tarball SHA256 in `tools/lock.yml`
- [ ] Implement `tools/fetch_sdcc.sh` that:
  - [ ] downloads to a temp file
  - [ ] verifies SHA256
  - [ ] extracts into `tools/sdcc/`
  - [ ] prints `tools/sdcc/bin/sdcc -v`
- [ ] Ensure CI uses `tools/sdcc/bin/sdcc` explicitly (never `sdcc` from PATH)

**2.4 CI workflow**
- [ ] CI checks out submodules
- [ ] CI runs `tools/fetch_sdcc.sh`
- [ ] CI runs `tools/build_openmsx.sh`
- [ ] CI builds host tests and runs `ctest`
- [ ] CI builds firmware target(s)
- [ ] CI uploads build artifacts

**2.5 Minimal tests**
- [ ] `fw/tests/host/smoke_test.cc` runs and passes in CI

### Definition of done

- [ ] CI builds and logs show the pinned openMSX and SDCC versions
- [ ] CI succeeds even if system openMSX/SDCC are absent

---

## Stage 3 — Spine v0: diag + log + SecurityPosture + PolicyStore + CapabilityRegistry (Declared/Allowed only)

**Goal:** Provide one source of truth for OTP posture, signed policy flags, and declared/allowed capabilities. No activation yet.

### Files added

**Diagnostics and logging**
- `fw/src/diag/diag.h`, `fw/src/diag/diag.cc` — stable diag codes + formatting
- `fw/src/log/log.h`, `fw/src/log/log.cc` — bounded logger (ring + UART sink)

**Security posture**
- `fw/src/security/otp_reader.h`, `fw/src/security/otp_reader.cc` — OTP read helpers (no writes here)
- `fw/src/spine/security_posture.h`, `fw/src/spine/security_posture.cc` — reads OTP once; exposes posture facts

**Policy**
- `fw/src/policy/policy_types.h` — `PolicyFlags` bitset + metadata
- `fw/src/policy/policy_verify.h`, `fw/src/policy/policy_verify.cc` — signature verification
- `fw/src/spine/policy_store.h`, `fw/src/spine/policy_store.cc` — load + verify policy from flash; expose flags

**Descriptors**
- `fw/src/boards/board_descriptor.h`, `fw/src/boards/board_descriptor.cc` — HW candidates (declared only)
- `fw/src/drivers/driver_descriptor.h` — SW descriptor struct
- `fw/src/drivers/driver_descriptor_table.cc` — the only SW descriptor list

**Capability registry**
- `fw/src/spine/capability_registry.h`, `fw/src/spine/capability_registry.cc` — Declared/Allowed sets + queries

**Entrypoint**
- `fw/src/main.cc` — boots spine in order and prints a banner

### Files modified

- `fw/CMakeLists.txt`

### Checklist

**3.1 Diagnostics (`diag`)**
- [ ] Define `enum class DiagCode : uint16_t` including at least:
  - [ ] `OK`
  - [ ] `POLICY_MISSING`
  - [ ] `POLICY_BAD_SIGNATURE`
  - [ ] `POLICY_BAD_CANONICALIZATION`
  - [ ] `OTP_UNREADABLE`
  - [ ] `INTERNAL_ASSERT`
- [ ] Implement `const char* diag_code_to_string(DiagCode)` (stable strings)
- [ ] Implement `struct DiagStatus { DiagCode code; uint32_t detail; }`
- [ ] Host test: all diag codes map to non-null strings

**3.2 Logging (`log`)**
- [ ] Implement fixed-size log ring buffer (compile-time size constant)
- [ ] Implement `log_init()`, `log_write(LogLevel, const char* msg)`
- [ ] Implement `log_flush_uart()` for non-bus-loop contexts
- [ ] Host test: N writes wrap safely and preserve last K entries

**3.3 OTP reader**
- [ ] Implement `OtpReader::read_bytes(offset, dst, len)` with bounds checks
- [ ] Implement `read_u24(offset)`, `read_u8(offset)` — RP2350 OTP rows are 24-bit (3 data bytes); `read_u32` would be wrong
- [ ] Comment each OTP offset with the corresponding `spec.md` section reference
- [ ] Host test: fake OTP buffer reads expected values

**3.4 SecurityPosture**
- [ ] Define `struct SecurityPosture` with all normative facts from `spec.md §10`:
  - [ ] `bool secure_boot_enabled`
  - [ ] `bool otp_device_secret_present`
  - [ ] `uint8_t boot_key_valid_mask` (bits 0–3 map to OTP key slots 0–3)
  - [ ] `bool debug_disabled`
  - [ ] `bool usb_boot_disabled`
  - [ ] `bool uart_boot_disabled`
  - [ ] `bool anti_rollback_enabled`
  - [ ] `bool encrypted_boot_enabled` — **NOTE**: RP2350 has no single OTP bit for
        this; it requires reading partition table imagedef headers. Set to `false`
        until Stage 6 adds storage-layer support. Stub the field with a TODO comment.
- [ ] Implement `SecurityPosture SecurityPosture::read(OtpReader&)`
- [ ] Implement `describe()` helper for logging
- [ ] Host test: posture fields match values derived from fake OTP

**3.5 Policy types**
- [ ] Define `PolicyFlags` (use `uint64_t` with named bit constants)
- [ ] Include at least:
  - [ ] `POLICY_ALLOW_NETWORK`
  - [ ] `POLICY_ALLOW_USER_COLLECTIONS`
  - [ ] `POLICY_REQUIRE_PUBLISHER_SIGNATURES`
- [ ] Define `PolicyInfo { PolicyFlags flags; uint8_t digest16[16]; uint32_t version; }`

**3.6 Policy verification**
- [ ] Implement `policy_verify(...)` using the signature algorithm defined in `spec.md`
- [ ] Implement canonicalization approach consistent with `spec.md`:
  - [ ] either canonicalize in firmware, or
  - [ ] require pre-canonicalized bytes and hash raw bytes
- [ ] Host tests:
  - [ ] valid policy verifies
  - [ ] one-bit mutation fails
  - [ ] wrong key fails
  - [ ] missing required fields yields correct diag code

**3.7 PolicyStore**
- [ ] Define policy storage location(s) in flash (temporary layout OK; stable later)
- [ ] Implement `PolicyStore::load()` returning `DiagStatus`
- [ ] On policy failure, continue boot with safe defaults (all restricted) and store diag code
- [ ] Host tests: missing → safe defaults; invalid → safe defaults + expected code

**3.8 Board descriptor (declared HW)**
- [ ] Define `BoardCapabilityDecl { const char* name; bool safe_verify; }`
- [ ] Implement `BoardDescriptor::for_current_board()` as a single centralized definition
- [ ] Document in comments: declared ≠ present; no probing in Stage 3
- [ ] **Capability ID naming**: use functional domain prefixes (`bus.*`, `net.*`, `storage.*`,
      `io.*`, `ui.*`, `video.*`, `audio.*`) per `spec.md §5.1`. The `hw`/`sw` origin is
      metadata in the descriptor, NOT part of the capability ID.

**3.9 Driver descriptor table (declared SW)**
- [ ] Define `DriverDescriptor { const char* name; /* origin=sw */ }`
- [ ] Implement `kDriverDescriptors[]` and `kDriverDescriptorCount`
- [ ] Add at least one placeholder capability: `api.core`

**3.10 CapabilityRegistry (Declared → Allowed)**
- [ ] Store declared HW and SW candidates
- [ ] Compute allowed candidates by applying policy masking rules
- [ ] Provide queries:
  - [ ] `is_declared(name)`
  - [ ] `is_allowed(name)`
  - [ ] `list_declared(out, max)`
  - [ ] `list_allowed(out, max)`
- [ ] Define masking rules in code comments with `spec.md` references
- [ ] Host tests:
  - [ ] list ordering deterministic
  - [ ] policy masking removes only expected names

**3.11 Entrypoint wiring**
- [ ] Print a single boot banner with `FW_BUILD_ID`
- [ ] Log posture summary, policy digest/flags, declared count, allowed count
- [ ] Do not start any probing or allocation in this stage

### Definition of done

- [ ] Firmware prints posture + policy + declared/allowed counts on boot
- [ ] CI host tests pass
- [ ] Only `SecurityPosture` reads OTP; only `PolicyStore` reads policy blobs

---

## Stage 4 — JLPiCart API v1: minimal MSX-facing contract

**Goal:** Implement the API window framing and a minimal `core` service that exposes posture/policy/registry summaries.

### Files added

- `fw/src/msx/api/api_window.h`, `fw/src/msx/api/api_window.cc` — API window registers + rings
- `fw/src/msx/api/services/core_service.h`, `fw/src/msx/api/services/core_service.cc` — `core` methods
- `fw/z80/api_client/` — Z80 reference client (SDCC or assembly), minimal and test-oriented

### Files modified

- `fw/src/main.cc` — maps API window into MSX-visible space
- `fw/CMakeLists.txt`

### Checklist

**4.1 API window framing**
- [ ] Define constants for window size, offsets, ring sizes (single source)
- [ ] Implement ring push/pop with wrap marker exactly per `spec.md`
- [ ] Implement `service_once()`:
  - [ ] read one request frame
  - [ ] dispatch by service id
  - [ ] write one response frame
- [ ] Host tests:
  - [ ] request → service_once → response roundtrip
  - [ ] wrap-around behavior
  - [ ] malformed frames rejected with correct error

**4.2 Core service**
- [ ] Define packed request/response structs with explicit endianness rules
- [ ] Implement:
  - [ ] `core.ping` — there is no dedicated ping method in the spec; `GET_API_INFO`
        (0x00) serves the same purpose: any successful roundtrip proves the transport.
        Consider GET_API_INFO as the de-facto ping for Stage 4.
  - [ ] `core.get_info` → `SYS_GET_API_INFO` (0x00): api_major, api_minor,
        layout_ver, feature_bits, max_frame, posture_props, boot_key_valid_mask
  - [ ] `core.get_security_info` → `SYS_GET_SECURITY_INFO` (0x05): from `SecurityPosture`
  - [ ] `core.get_policy_info` → `SYS_GET_POLICY_FLAGS` (0x06): from `PolicyStore`
  - [ ] `core.list_capabilities` → `SYS_GET_CAPS` (0x02): allowed capabilities only
- [ ] Validate all mandatory request invariants per `spec.md §5.1`:
  - [ ] `seq` MUST be nonzero; return `E_BAD_REQ` otherwise
  - [ ] `status` MUST be 0 in requests; return `E_BAD_REQ` otherwise
  - [ ] if `scratch_ofs != 0xFFFF`, validate `scratch_ofs + scratch_len` fits within
        the h2c scratch buffer; return `E_BAD_ARG` otherwise
- [ ] Host tests: at least one method returns expected values from injected spine objects

**4.3 Z80 reference client**
- [ ] Build the client with pinned SDCC from `tools/sdcc/bin/sdcc`
  (path relative to `fw/z80/api_client/` = `../../../tools/sdcc/bin/sdcc`)
- [ ] Provide a minimal program that issues `core.get_info` and prints a hex dump
- [ ] Ensure no assumptions about interrupts or specific MSX model features

**4.4 Bus mapping note**
- The bootstrapping plan originally listed "maps API window into MSX-visible space"
  as a Stage 4 task in `src/main.cc`. This requires the bus loop (Core 0 tight loop,
  subslot register, PIO memory mapping) which is a Stage 5 deliverable.
- In Stage 4, the ApiWindow buffer is initialised and polled from the firmware's
  main loop only. A TODO comment in `src/main.cc` marks where bus mapping hooks in.
- This is not a gap: the framing and service logic are fully testable without bus
  hardware, which is the stated purpose of host tests in this stage.

### Definition of done

- [ ] A Z80 program can call `core.get_info` and receive a valid response
- [ ] Host tests cover framing + at least one core method
- [ ] All mandatory request invariants (seq, status, scratch bounds) validated

---

## Stage 5 — Menu Host ABI v1: mailbox + Z80 stub ROM

**Goal:** Define and implement the mailbox ABI so the RP menu app can render/read input without ad-hoc coupling.

### Files added

- `fw/src/msx/menu/menu_host_abi.h`, `fw/src/msx/menu/menu_host_abi.cc` — mailbox page helpers
- `fw/z80/menu_stub/` — Z80 stub ROM built with pinned SDCC

### Files modified

- `fw/src/main.cc`
- `fw/CMakeLists.txt`

### Checklist

**5.1 Mailbox definition**
- [ ] Define mailbox page size and offsets as named constants
- [ ] Define packed structs:
  - [ ] `HostInfo`
  - [ ] `InputSnapshot`
  - [ ] `CommandHeader`
- [ ] Add compile-time size/layout asserts for each struct

**5.2 Host-side mailbox**
- [ ] Implement `MenuMailbox::init(ptr)`
- [ ] Implement `MenuMailbox::tick()` that processes one command at a time

**5.3 Command set (minimal v1)**
- [ ] Implement:
  - [ ] `CMD_CLEAR`
  - [ ] `CMD_DRAW_TEXT`
  - [ ] `CMD_READ_INPUT`
  - [ ] `CMD_ACK`
- [ ] Define stable error codes for unknown command ids

**5.4 Z80 stub**
- [ ] Render using BIOS-safe routines for baseline MSX
- [ ] Read input via BIOS where possible
- [ ] Poll the mailbox and execute commands without long blocking loops
- [ ] Include a small “self-test” mode (compile flag) that writes a known signature in mailbox

**5.5 Tests**
- [ ] Host tests for struct packing and mailbox encode/decode invariants

### Definition of done

- [ ] Stub ROM boots in emulator and can render a minimal UI page
- [ ] Host receives input snapshots through the mailbox

---

## Stage 6 — Storage substrate v1: flash layout + atomic KV + append log

**Goal:** Implement durable storage primitives before collections/manifests.

### Files added

- `fw/src/storage/flash_layout.h`, `fw/src/storage/flash_layout.cc` — partitions + versions
- `fw/src/storage/flash_device.h`, `fw/src/storage/flash_device.cc` — bounded read/write/erase
- `fw/src/storage/kv_store.h`, `fw/src/storage/kv_store.cc` — power-loss-safe KV
- `fw/src/storage/append_log.h`, `fw/src/storage/append_log.cc` — append-only log
- `fw/src/storage/storage_health.h`, `fw/src/storage/storage_health.cc` — corruption detection + reporting
- `fw/tests/host/flash_sim.cc` — host flash simulator for power-loss tests

### Files modified

- `fw/src/main.cc` — initialize storage early; append boot record
- `fw/src/spine/policy_store.*` — load policy via KV (if chosen here)

### Checklist

**6.1 Layout**
- [ ] Define `FLASH_LAYOUT_VERSION`
- [ ] Define partitions: `SYSTEM_KV`, `EVENT_LOG`, `CONTENT_INDEX` (reserved), `CONTENT_DATA` (reserved)
- [ ] Add compile-time asserts for partition alignment and no overlap

**6.2 Flash wrapper**
- [ ] Implement `read(addr, buf, len)`
- [ ] Implement `write(addr, buf, len)` with documented constraints
- [ ] Implement `erase(sector_addr, sector_count)`
- [ ] Host tests for bounds checking

**6.3 KV store**
- [ ] Choose and document an atomic strategy (log-structured KV recommended)
- [ ] Implement `kv_init`, `kv_get`, `kv_put`, `kv_delete`
- [ ] Host tests:
  - [ ] roundtrip put/get
  - [ ] overwrite key
  - [ ] simulated power loss mid-write: old value survives; store remains consistent

**6.4 Append log**
- [ ] Define record header: `type`, `len`, `seq`, `crc`
- [ ] Implement `append`, `iterate`
- [ ] Host tests:
  - [ ] tail corruption detected and ignored safely
  - [ ] iteration yields records in order

**6.5 Boot integration**
- [ ] Append a `BOOT` record on startup with build id hash and monotonic counter (if used)
- [ ] Storage init completes before bus start

### Definition of done

- [ ] KV and append log pass host power-loss tests
- [ ] Firmware writes and reads a boot record across reboots

---

## Stage 7 — Collections v1 (local-only): format, manifests, install, receipts

**Goal:** Implement local install from USB into flash using the storage substrate.

### Files added

- `fw/src/content/collection_format.h`, `fw/src/content/collection_format.cc` — bundle layout + hashing
- `fw/src/content/manifest.h`, `fw/src/content/manifest.cc` — manifest model
- `fw/src/content/manifest_parser.h`, `fw/src/content/manifest_parser.cc` — parsing + merge rules
- `fw/src/content/installer_usb.h`, `fw/src/content/installer_usb.cc` — scan, verify, atomic install
- `fw/src/content/receipts.h`, `fw/src/content/receipts.cc` — install receipts to append log

### Files modified

- `fw/src/main.cc`
- `fw/src/policy/policy_verify.*` — reused for bundle signature verification

### Checklist

**7.1 Bundle format**
- [ ] Parse signature envelope per `spec.md`
- [ ] Hash files with SHA-256
- [ ] Verify signatures when policy requires them

**7.2 Manifest parsing**
- [ ] Implement canonical JSON parsing (strict)
- [ ] Implement merge algorithm per `spec.md`
- [ ] Host tests for merge and override behavior

**7.3 USB install**
- [ ] Scan `/JLPICART/INSTALL/*/`
- [ ] Verify hashes then signature
- [ ] Install atomically with staging + commit marker
- [ ] Host power-loss test: interruption never yields partial visible install

**7.4 Receipts**
- [ ] Append receipts for success and failure with stable diag codes

### Definition of done

- [ ] USB install yields a persistent installed collection entry
- [ ] Interrupted install never leaves half-installed visible content

---

## Stage 8 — Activation v1: Requested → Activated (verify HW only when requested; allocate SW deterministically)

**Goal:** Turn declared/allowed into per-payload activated capabilities via safe verification (HW) and deterministic allocation (SW).

### Files added

- `fw/src/allocator/resource_model.h`, `fw/src/allocator/resource_model.cc` — budgets + accounting
- `fw/src/allocator/allocator.h`, `fw/src/allocator/allocator.cc` — deterministic planner
- `fw/src/peripherals/peripheral_manager.h`, `fw/src/peripherals/peripheral_manager.cc` — orchestrates activation and mapping plan
- `fw/src/spine/activation.h`, `fw/src/spine/activation.cc` — shared activation helpers

### Files modified

- `fw/src/spine/capability_registry.*` — add activated view + activation report
- `fw/src/main.cc` — build requested set and call allocator before launch

### Checklist

**8.1 Requested set**
- [ ] Define `RequestedCapabilities` derived from payload manifest
- [ ] Implement `requested_from_manifest(...)`

**8.2 Activation rules**
- [ ] HW activation:
  - [ ] only if declared + allowed + requested
  - [ ] only if `safe_verify=true`
  - [ ] verify touches only dedicated pins/buses
- [ ] SW activation:
  - [ ] only if declared + allowed + requested
  - [ ] only if resources allow
  - [ ] allocation is deterministic (stable order)

**8.3 Resource model**
- [ ] Define budgets (RAM, flash/cache, placeholder cycle class)
- [ ] Implement `can_allocate` and `apply_allocation`

**8.4 Allocator**
- [ ] Evaluate requested capabilities in stable order
- [ ] Produce `ActivatedCapabilities` and `MappingPlan`
- [ ] Host tests: same input yields same plan; overcommit yields stable “not activated” reasons

### Definition of done

- [ ] Nothing is probed unless requested
- [ ] Activation results are deterministic across runs

---

## Stage 9 — MSX bus layer and sw.mapper capability

**Goal:** Port the proven MSX bus loop into the new firmware tree, declare `sw.mapper`
as an activatable SW capability, and connect the Allocator output to bus configuration
via `MappingPlan`.  The bus loop runs on Core 0 (noreturn); a Core 1 service function
handles the API window and menu mailbox.

### Files added

- `fw/src/boards/gpio_defs.h` — GPIO pin and bit constants for jlpicart_board (no SDK deps)
- `fw/src/cartridges/cartridge.h` — Cartridge struct: 8×8KB memory segments, 256 IO port
  callbacks, bus-callback typedef; portable (no SDK includes)
- `fw/src/mappers/mappers.h`, `fw/src/mappers/mappers.cc` — ROM/RAM/Konami/ASCII8/ASCII16
  mapper setup functions and their RAMFUNC callbacks
- `fw/src/bus/bus.h` — MSX bus interface: `BUS::cartridges[]`, subslot state, `BUS::start()`
- `fw/src/bus/bus.cc` — Core 0 tight loop (hardware-only; excluded from host-test builds)
- `fw/src/bus/mapping_plan.h` — `MapperType` enum, `MappingPlan` / `MappingEntry` structs,
  `mapper_type_from_string()`, `mapper_type_to_string()`
- `fw/src/bus/mapping_plan.cc` — `mapper_plan_from_manifest()` bridge function

### Files modified

- `fw/src/content/manifest.h` — add `mapper_type[24]` and `subslot` to `PayloadEntry`
- `fw/src/content/manifest_parser.cc` — parse `mapper_type` and `subslot` in payload body
- `fw/src/drivers/driver_descriptor_table.cc` — add `sw.mapper` (no resource requirements)
- `fw/src/peripherals/peripheral_manager.h`, `.cc` — add `apply_mapping(MappingPlan)`
- `fw/src/main.cc` — launch Core 1 for service loop; Core 0 enters `BUS::start()`
- `fw/tests/CMakeLists.txt` — add `test_bus_mapper`

### Checklist

**9.1 Declare sw.mapper capability**
- [ ] Add `{ "sw.mapper", {} }` to `kDriverDescriptors[]`; no resource requirements
      (ROM lives in XIP flash; mapper register state is negligible)
- [ ] Comment: mapper type (ROM/RAM/banking) is specified in the manifest, not here

**9.2 GPIO pin definitions**
- [ ] Create `fw/src/boards/gpio_defs.h` with `PinMapping` enum (GPIO_A0…GPIO_SLTSL,
      GPIO64_CLK…GPIO64_BATSENS) and `PinBitMapping` / `PinBitMapping64` enums
- [ ] No SDK includes — pure constants; safe to include in host tests

**9.3 Cartridge abstraction**
- [ ] `struct Cartridge` with `name`, `rom_base`, `ram_base`,
      `memory_read_addresses[8]`, `memory_write_addresses[8]`,
      `memory_read_callbacks[8]`, `memory_write_callbacks[8]`,
      `io_read_callbacks[256]`, `io_write_callbacks[256]`
- [ ] `using BusCallback = std::pair<bool,uint8_t>(*)(Cartridge&, uint32_t)` — no SDK
- [ ] `void Cartridge::clear()` zeros all fields via `*this = Cartridge{}`
- [ ] Define `RAMFUNC` macro: `__no_inline_not_in_flash_func` on hardware, no-op for
      host tests (guarded by `JLPICART_HOST_TEST`)

**9.4 Mapper setup functions**
- [ ] `mapper_setup_rom(c, rom_base, rom_size)` — linear; mirrors last seg if < 64 KB
- [ ] `mapper_setup_rom_32k_mirrored(c, rom_base)` — 32 KB ROM, `((i+2)%4)` pattern
- [ ] `mapper_setup_konami(c, rom_base)` — Konami 8 KB banking; pages 2–5 switchable
- [ ] `mapper_setup_konami_z(c, rom_base)` — Konami without 0x6000 register; pages 4–5
- [ ] `mapper_setup_ascii8(c, rom_base)` — ASCII 8 KB banking; write callback on seg 3
- [ ] `mapper_setup_ascii16(c, rom_base)` — ASCII 16 KB banking; write callback on seg 3
- [ ] `mapper_setup_ram(c, ram_base, ram_size)` — flat RAM; both read and write addresses
- [ ] All switch callbacks declared `static` in mappers.cc with RAMFUNC; never null-deref

**9.5 MappingPlan**
- [ ] `enum class MapperType : uint8_t` — ROM, ROM_32K_MIRRORED, KONAMI, KONAMI_Z,
      ASCII8, ASCII16, RAM, NONE
- [ ] `mapper_type_from_string(s)` — canonical lowercase strings; NONE for unknown
- [ ] `mapper_type_to_string(t)` — inverse; returns `"none"` for NONE
- [ ] `struct MappingEntry { MapperType, subslot, rom_data, rom_size, ram_data, ram_size }`
- [ ] `struct MappingPlan { entries[4], entry_count, expanded }`
- [ ] `MappingPlan mapper_plan_from_manifest(manifest, payload_index)` — converts
      `PayloadEntry.mapper_type` and `.subslot` to a `MappingEntry`; `rom_data = nullptr`
      (ROM loading deferred to content-load stage); out-of-range index → empty plan

**9.6 Manifest extensions**
- [ ] Add `char mapper_type[PAYLOAD_MAPPER_TYPE_MAX]` (24 chars) to `PayloadEntry`
- [ ] Add `uint8_t subslot` (0–3, default 0) to `PayloadEntry`
- [ ] `PAYLOAD_MAPPER_TYPE_MAX = 24` constant in `manifest.h`
- [ ] Extend `parse_payload_body()` to handle both new fields; reject `subslot > 3`

**9.7 Bus layer**
- [ ] `bus.h`: `BUS::cartridges[8]`, `BUS::subslot_indexes[4]`, `BUS::is_expanded`,
      `BUS::reset_callback`, `[[noreturn]] BUS::start()`
- [ ] `bus.cc`: Core 0 tight loop identical in logic to `old_src/bus/bus.cc`; hot state
      (`subslot_indexes`, `is_expanded`, `reset_callback`) placed in `.scratch_y`;
      guarded by `#ifndef JLPICART_HOST_TEST`

**9.8 PeripheralManager wiring**
- [ ] Add `bool apply_mapping(const MappingPlan& plan)` to `PeripheralManager`
- [ ] For each entry with `rom_data != nullptr`: call `mapper_setup_XXX(BUS::cartridges[subslot], ...)`
      and set `BUS::is_expanded` when `entry_count > 1`
- [ ] If `entry.rom_data == nullptr`: log "ROM not loaded — deferred to content-load stage"
- [ ] Hardware call to `BUS::cartridges` guarded with `#ifndef JLPICART_HOST_TEST`

**9.9 Main wiring**
- [ ] After activation preflight, compute `MappingPlan` from active payload (empty in
      standby/menu mode) and call `periph_mgr.apply_mapping(mapping_plan)`
- [ ] On hardware: declare `api_win` and `menu_mbx` as `static`; store pointers in
      file-scope globals; launch Core 1 via `multicore_launch_core1(service_loop_core1)`;
      then call `BUS::start()` (noreturn)
- [ ] On host: existing `while (true)` service loop unchanged
- [ ] Set `BUS::reset_callback` before `BUS::start()` (stub for Stage 9)

**9.10 Host tests (`test_bus_mapper.cc`)**
- [ ] `mapper_type_from_string` round-trip for all 7 named types + unknown → NONE
- [ ] `mapper_setup_rom`: `read_addresses[i]` correct for 16 KB, 32 KB, 64 KB inputs
- [ ] `mapper_setup_konami`: initial addresses + write callback present on pages 2–5;
      simulate write (construct bus word) → verify address switch
- [ ] `mapper_setup_ascii8`: write callback on seg 3; simulate segment switch
- [ ] `mapper_plan_from_manifest`: manifest with `mapper_type` + `subslot` → `MappingPlan`
- [ ] `mapper_plan_from_manifest` with out-of-range index → empty plan

### Definition of done

- [ ] Bus loop compiles for RP2350 target; host tests compile and pass
- [ ] `sw.mapper` appears in declared/allowed capabilities on boot
- [ ] `MappingPlan` is computed from the active payload manifest
- [ ] `PeripheralManager` logs mapper configuration on boot

---

## Stage 10+ — Feature peripherals and services

**Goal:** Implement concrete peripherals and services by following the same pattern.

### Checklist template (must be expanded before coding)

- [ ] Declare the capability (board descriptor for HW or driver descriptor for SW)
- [ ] Define the policy mask rules and document them in `spec.md`
- [ ] Define activation rules (verify/allocate) and add host tests
- [ ] Implement MSX mapping (if classic interface) via `MappingPlan`
- [ ] Implement API service surface (if API interface) with framing tests
- [ ] Add openMSX-driven tests when MSX-visible behavior exists

---

## Appendix — Things the agent must not do

- [ ] Do not introduce a second source of truth for posture/policy/capabilities
- [ ] Do not probe anything unless declared + allowed + requested and marked safe-to-verify
- [ ] Do not read OTP outside `SecurityPosture`
- [ ] Do not parse policy blobs outside `PolicyStore`
- [ ] Do not add scattered compile-time flags for peripherals; add descriptors instead
