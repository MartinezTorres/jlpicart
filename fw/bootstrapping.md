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

## Stage 10 — Content runtime: PayloadRecord, ContentStore, launch pipeline

**Goal:** Close the loop between the installed collection store (Stage 7) and the bus
layer (Stage 9).  After this stage, if a ROM is present in CONTENT_DATA flash the
firmware will boot it directly through the full ContentStore → MappingPlan → bus loop
pipeline.  A `populate_flash.py` tool pre-populates flash from a ROM file so the
pipeline can be validated without USB host hardware.

### Files added

- `fw/src/content/content_store.h`, `.cc` — `ContentStore`: reads `CollectionRecord` and
  `PayloadRecord`s from `KvStore`; exposes `has_active_collection()`,
  `load_collection()`, `load_payload()`, `load_default_payload()`
- `fw/tools/populate_flash.py` — generates a UF2 patch from a ROM file; writes
  `SYSTEM_KV` records (`col.state`, `col.record`, `pl.<id>`) and `CONTENT_DATA`
  ROM bytes at the correct flash addresses; prints `picotool` flash command
- `fw/tests/host/test_content_store.cc` — host tests

### Files modified

- `fw/src/content/collection_format.h` — add `PayloadRecord` struct and
  `KV_PAYLOAD_PREFIX` constant
- `fw/src/bus/mapping_plan.h`, `.cc` — add
  `mapping_plan_from_payload_record(const PayloadRecord&)`
- `fw/src/content/installer.cc` — write `PayloadRecord` per payload at commit step
- `fw/src/main.cc` — check `ContentStore` on boot; replace the empty-plan MappingPlan
  block with a real payload lookup
- `fw/tests/CMakeLists.txt` — add `test_content_store`

### Checklist

**10.1 PayloadRecord**
- [x] Add to `collection_format.h`:
  - [x] `KV_PAYLOAD_PREFIX = "pl."` (3-char prefix; payload_id ≤ 45 chars in KV key)
  - [x] `PayloadRecord { payload_id[64], mapper_type[24], subslot, _pad[3],
        data_flash_offset, data_size }` — 100 bytes, packed
  - [x] `static_assert(sizeof(PayloadRecord) == 100)`
  - [x] `static_assert(sizeof(PayloadRecord) <= KV_MAX_VAL_LEN)`

**10.2 ContentStore**
- [x] `has_active_collection()` — reads `KV_COL_STATE`; returns true iff `"active"`
- [x] `load_collection(CollectionRecord&)` — reads `KV_COL_RECORD`
- [x] `load_payload(payload_id, PayloadRecord&)` — reads `"pl.<id>"` from KvStore
- [x] `load_default_payload(PayloadRecord&)` — chains through `CollectionRecord.default_payload_id`
- [x] Returns `STORAGE_NOT_FOUND` gracefully for all missing-key cases

**10.3 mapping_plan_from_payload_record**
- [x] Returns empty plan if `mapper_type` is empty/NONE or `data_size == 0`
- [x] On hardware: `rom_data = reinterpret_cast<const uint8_t*>(0x10000000u + data_flash_offset)`
- [x] On host (`JLPICART_HOST_TEST`): `rom_data = nullptr` (XIP not available)
- [x] Sets `rom_size`, `subslot`, `mapper_type`; `expanded = false` (single entry)

**10.4 Installer writes PayloadRecords**
- [x] After the `col.state = "active"` commit step, iterate `manifest.payloads[]`
- [x] For each payload: build `PayloadRecord` with `data_flash_offset = FLASH_CONTENT_DATA_OFS`
      and `data_size = 0` (ROM bytes written separately by `populate_flash.py`)
- [x] Put to KvStore with key `"pl.<payload_id>"`; failure is non-fatal (logged, not returned)
- [x] Document: `data_size = 0` means ROM not yet written; `apply_mapping()` skips bus wiring

**10.5 Main wiring**
- [x] Replace the Stage 9 empty-manifest MappingPlan block with `ContentStore` lookup
- [x] If `has_active_collection()` and `load_default_payload()` succeeds and `data_size > 0`:
      call `mapping_plan_from_payload_record()` and pass result to `apply_mapping()`
- [x] Otherwise: log reason and use empty MappingPlan (standby mode)

**10.6 populate_flash.py**
- [x] Parse mapper type from `MAPPER_<TYPE>` suffix in ROM filename (case-insensitive)
      or from `--mapper` flag
- [x] Generate KV region binary with correct CRC32 records (IEEE 802.3 / zlib)
- [x] Generate CONTENT_DATA binary (raw ROM bytes at `FLASH_CONTENT_DATA_OFS = 0x600000`)
- [x] Output single UF2 with two address ranges:
      KV at `0x10200000`; ROM at `0x10600000`
- [x] Print `picotool load <output>.uf2` command after generation
- [x] `--help` shows supported mapper names and example invocation

**10.7 Host tests (`test_content_store.cc`)**
- [x] `has_active_collection()` false on empty KvStore
- [x] `has_active_collection()` false when `col.state = "pending"`
- [x] Round-trip: put `col.state + col.record + pl.<id>` → `load_collection()` + `load_payload()` correct
- [x] `load_default_payload()` finds payload via `CollectionRecord.default_payload_id`
- [x] `mapping_plan_from_payload_record()` with `data_size > 0` → `entry_count = 1`, fields correct
- [x] `mapping_plan_from_payload_record()` with `data_size = 0` → `entry_count = 0`
- [x] `mapping_plan_from_payload_record()` with empty `mapper_type` → `entry_count = 0`
- [x] Installer integration: after `Installer::run()`, KvStore contains `pl.<id>` record

### Definition of done

- [x] Host tests pass; installer writes PayloadRecords
- [x] `populate_flash.py` generates a valid UF2 from `fw/roms/flash1M/Menace f Triton.MAPPER_LINEAR.rom`
- [ ] On hardware: flashing firmware UF2 + collection UF2 boots the ROM through the
      ContentStore → MappingPlan → bus loop pipeline with no hardcoded ROM pointers

---

## Stage 11 — Z80 toolchain: SDCC setup and menu stub

**Goal:** Compile the Z80 menu stub using a pinned SDCC version.  The stub binary
is embedded in the firmware image and copied into the menu page on init.  No openMSX
integration yet; the stage ends when the stub compiles and the firmware links.

### Design note: stub placement within the menu page

`data_ofs` is fixed at `0x0100` per spec §8.  To avoid overwriting stub code with
command data, the stub lives at the **top** of the menu page at a fixed offset
`MENU_STUB_OFS = 0x3800` (last 2 KB of the 16 KB page).  This gives:

- command data buffer: `0x0100..0x37FF` (~14.6 KB, `MENU_USABLE_DATA_LEN = 0x3700`)
- stub code region:    `0x3800..0x3FFF` (2 KB max)

`stub_entry` is set to `MENU_STUB_OFS` in `MenuMailbox::init()`.

### Files added

- `fw/tools/lock.yml` — pinned SDCC 4.5.x tarball URL + SHA256
- `fw/tools/get_sdcc.sh` — downloads + unpacks SDCC to `fw/tools/sdcc/`; verifies SHA256;
  no-op if `fw/tools/sdcc/bin/sdcc` already exists
- `fw/src/msx/menu/stub/crt0.s` — minimal Z80 startup: set SP, call `_main`, loop forever
- `fw/src/msx/menu/stub/stub_bios.h` — MSX BIOS entry constants
- `fw/src/msx/menu/stub/stub.c` — Z80 menu stub source (minimum conformance per spec §8)
- `fw/src/msx/menu/stub/Makefile` — builds `stub.c` + `crt0.s` → `stub.bin` via SDCC
- `fw/src/msx/menu/menu_stub_bin.h` — **generated**; `uint8_t kMenuStubBin[]` + size constant
- `fw/tests/host/test_menu_stub.cc` — host tests

### Files modified

- `fw/src/msx/menu/menu_host_abi.h` — add `MENU_STUB_OFS`, `MENU_USABLE_DATA_LEN` constants
- `fw/src/msx/menu/menu_host_abi.cc` — `init()`: copy `kMenuStubBin` into `page + MENU_STUB_OFS`;
  set `hdr->stub_entry = MENU_STUB_OFS`; assert stub fits within 2 KB
- `fw/CMakeLists.txt` — custom target to invoke `fw/src/msx/menu/stub/Makefile`;
  `menu_stub_bin.h` as a generated file in the build tree
- `fw/tests/CMakeLists.txt` — add `test_menu_stub`

### Checklist

**11.1 SDCC toolchain pin**
- [x] Create `fw/tools/lock.yml`:
  ```yaml
  sdcc:
    version: "4.4.0"
    platform: linux-amd64
    url: "https://sourceforge.net/projects/sdcc/files/sdcc-linux-amd64/4.4.0/sdcc-4.4.0-amd64-unknown-linux2.5.tar.bz2/download"
    sha256: ""  # filled by get_sdcc.sh on first run
  ```
- [x] Create `fw/tools/get_sdcc.sh`:
  - check for `fw/tools/sdcc/bin/sdcc`; exit 0 if present
  - download URL from lock.yml; verify SHA256 with `sha256sum -c`
  - self-pins SHA256 into lock.yml on first run; `--force` flag to re-download
  - unpack to `fw/tools/sdcc/`
- [x] Add `fw/tools/sdcc/` to `.gitignore`

**11.2 Z80 stub source**
- [x] `fw/src/msx/menu/stub/stub_bios.h` — MSX BIOS call addresses (page-0 ROM):
  - `BIOS_CHGMOD  0x005F` — change screen mode; A = mode id
  - `BIOS_POSIT   0x00C6` — set cursor; H = row (1-based), L = col (1-based)
  - `BIOS_CHPUT   0x00A2` — write char in A to screen
  - `BIOS_SNSMAT  0x0141` — sense keyboard matrix row A → result in A
  - VDP data/cmd ports: `VDP_DATA = 0x98`, `VDP_CMD = 0x99`
  - PSG ports: `PSG_REG = 0xA0`, `PSG_WRITE = 0xA1`, `PSG_READ = 0xA2`
  - `BIOS_MSXVER 0x002D` — ROM byte: 0=MSX1, 1=MSX2, 2=MSX2+, 3=turboR
- [x] `fw/src/msx/menu/stub/crt0.s` — `__start`: `di`; set SP to 0x77FE
  (2 bytes below MENU_STUB_OFS, above data buffer); call `_main`; `halt` loop
- [x] `fw/src/msx/menu/stub/stub.c` — Z80 menu stub (SDCC C + inline asm):
  - Mailbox accessed via volatile pointer macros from page base 0x4000;
    no `#include` of host headers (Z80 build is isolated)
  - On startup: detect MSX generation from BIOS byte at `0x002D`; fill
    `hdr->host_caps` and `hdr->vdp_caps`; set `mbx->resp_seq = mbx->cmd_seq` (signal ready)
  - Main loop: spin on `MBX_CMD_SEQ != MBX_RESP_SEQ`; call `dispatch()`; ack
  - `dispatch()`: clears `out_len`; switches on `cmd_id`; writes `status`; ack via `MBX_RESP_SEQ = MBX_CMD_SEQ`
  - Commands (minimum conformance, spec §8):
    - `NOP (0x0000)`: status = OK
    - `GET_HOST_INFO (0x0001)`: fill `HostInfo` (12 bytes) into data buffer; `out_len = 12`
    - `SET_MODE (0x0002)`: BIOS CHGMOD with `arg0 & 0xFF`; reject > 2 with E_UNSUPPORTED
    - `CLEAR (0x0003)`: re-issue CHGMOD with `g_mode` (clears screen); status = OK
    - `PUT_TEXT (0x0004)`: validate `in_len <= DATA_CAP`; POSIT to `(arg0>>8)&0xFF, arg0&0xFF` (1-based);
      write bytes via CHPUT; non-ASCII replaced with `?`; E_OVERFLOW / E_BAD_STATE guards
    - `READ_INPUT (0x0005)`: scan 11 keyboard rows via SNSMAT; PSG reg 14 for joy1;
      fill 16-byte `InputSnapshot`; `out_len = 16`; status = OK
    - `IDLE (0x000A)`: busy-wait `arg0` ms (~33 inner iters/ms at 3.58 MHz); status = OK
    - all others: status = E_UNSUPPORTED
- [x] `bios_posit` saves col in C register before clobbering HL for row load (audit fix)

**11.3 Makefile and build integration**
- [x] `fw/src/msx/menu/stub/Makefile`:
  ```makefile
  SDCC_FLAGS := -mz80 --no-std-crt0 --code-loc 0x7800 --data-loc 0x7700 \
                --stack-auto --out-fmt-ihx
  ```
  Note: `--code-loc 0x7800` places code at Z80 address 0x7800 (page offset 0x3800, = `MENU_STUB_OFS`);
  `--data-loc 0x7700` places static vars within the menu page (audit fix — without this SDCC
  defaults to 0x0000, outside our mapped page); also builds `menupage.rom` (16 KB fixture for Stage 12)
- [x] `fw/CMakeLists.txt` custom target `build_menu_stub`:
  - gated on presence of `fw/tools/sdcc/bin/sdcc`; no-ops with message if absent
  - runs `make -C fw/src/msx/menu/stub`
  - runs `python3 fw/tools/bin_to_c_array.py stub.bin kMenuStubBin > menu_stub_bin.h`
- [x] Create `fw/tools/bin_to_c_array.py`: reads binary file, outputs `uint8_t <name>[] = { … };`
  and `uint16_t <name>_SIZE = N;`; handles empty/missing file by outputting a 1-byte placeholder
  with `#pragma message` (not `#warning` — avoids `-Werror=cpp` failures in host test build)
- [x] `static_assert(kMenuStubBin_SIZE <= 2048, "stub exceeds 2 KB slot")`

**11.4 MenuMailbox::init() update**
- [x] Add to `menu_host_abi.h`:
  ```c
  static constexpr uint16_t MENU_STUB_OFS        = 0x3800u; // stub code at top of page
  static constexpr uint16_t MENU_USABLE_DATA_LEN = MENU_STUB_OFS - MENU_DATA_OFS; // 0x3700
  ```
- [x] `MenuMailbox::init(page, stub_entry_ignored)`: ignore the parameter; always use
  `MENU_STUB_OFS` as stub_entry (parameter kept for API stability, reserved)
- [x] Copy `kMenuStubBin` (from `menu_stub_bin.h`) into `page + MENU_STUB_OFS`
- [x] Set `hdr->stub_entry = MENU_STUB_OFS`; `hdr->data_len = MENU_USABLE_DATA_LEN`

**11.5 Host tests (`test_menu_stub.cc`)**
- [x] `kMenuStubBin_SIZE <= 2048` (fits in stub slot)
- [x] `kMenuStubBin_SIZE > 0` (non-empty when SDCC was available at build time)
- [x] `MenuMailbox::init()` copies stub to `page + MENU_STUB_OFS`; `page[MENU_DATA_OFS]` unchanged
- [x] `MenuStubHeader` in page has `sig == "JLMN"`, `abi_major == 1`,
  `stub_entry == MENU_STUB_OFS`, `data_ofs == MENU_DATA_OFS`,
  `data_len == MENU_USABLE_DATA_LEN`, `mailbox_ofs == MENU_MAILBOX_OFS`
- [x] Updated `test_menu_mailbox` assertions to use `MENU_USABLE_DATA_LEN` and `MENU_STUB_OFS`
  (these changed from Stage 5 values when Stage 11 updated `init()`)

### Definition of done

- [x] `fw/tools/get_sdcc.sh` succeeds on a clean Linux amd64 checkout
- [x] `stub.c` compiles; `stub.bin` ≤ 2048 bytes
- [x] Firmware builds; host tests pass
- [x] `stub.bin` absent → firmware still builds with zero-stub placeholder + `#pragma message`

---

## Stage 12 — openMSX integration and menu emulator tests

**Goal:** openMSX available via a pinned git submodule; a test harness in
`fw/tests/openmsx/` can boot an MSX1 machine, load the firmware, and verify the
menu stub responds to mailbox commands over a TCL script interface.

Spec reference: spec.md §13.2.

### Files added

- `third_party/openMSX` — git submodule (openMSX source, pinned to a stable tag)
- `fw/tools/lock.yml` — add `openmsx` block with pinned tag + expected binary SHA256
- `fw/tools/build_openmsx.sh` — builds openMSX from submodule into `fw/tools/openmsx/`;
  skip if binary already exists; installs system dependencies note in README
- `fw/tests/openmsx/run_test.sh` — wrapper: launch `fw/tools/openmsx/bin/openmsx` with
  a given TCL script; parse stdout for `PASS`/`FAIL` line; propagate exit code
- `fw/tests/openmsx/fixtures/msx1_jlpicart.xml` — openMSX machine config: MSX1 with
  16 KB VRAM, BIOS ROM from openMSX share, JLPiCart in primary slot (UF2 not needed;
  use `--ext jlpicart_stub` with the menu-page RAM region loaded as a cartridge ROM image)
- `fw/tests/openmsx/test_menu_stub_basic.tcl` — emulator test: boot, verify GET_HOST_INFO,
  SET_MODE, CLEAR, PUT_TEXT, READ_INPUT

### Files modified

- `.gitmodules` — add `third_party/openMSX` entry
- `fw/tools/lock.yml` — add `openmsx` block

### Checklist

**12.1 openMSX submodule**
- [x] `git submodule add https://github.com/openMSX/openMSX.git third_party/openMSX`
  (already registered at `cb61db762aba16752ff649990bf85e40627777af` = `RELEASE_21_0`)
- [x] Pin to most recent stable tag: `fw/tools/lock.yml` updated with tag `RELEASE_21_0`
  and commit SHA; `OPENMSX_SYSTEM_DATA` in `run_test.sh` points to `third_party/openMSX/share/`
- [x] `fw/tools/build_openmsx.sh`:
  - check for `fw/tools/openmsx/bin/openmsx`; skip if present (`--force` to rebuild)
  - `make -C third_party/openMSX -j$(nproc)` (uses GNUmakefile, not build.py)
  - finds binary at `derived/openmsx` symlink; copies to `fw/tools/openmsx/bin/openmsx`
  - prints BIOS ROM requirements and next-step instructions
- [x] Add `fw/tools/openmsx/` to `.gitignore`

**12.2 Test runner**
- [x] `fw/tests/openMSX/run_test.sh`:
  - usage: `run_test.sh <test.tcl> [extra openMSX args]`
  - sets `OPENMSX_USER_DATA=fw/tests/openMSX/fixtures/` (for custom machine lookup)
  - sets `OPENMSX_SYSTEM_DATA=third_party/openMSX/share/` (built-from-source share)
  - sets `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` for headless CI operation
  - wraps `fw/tools/openmsx/bin/openmsx -machine msx1_jlpicart -cart menupage.rom -script <tcl> -nolog`
  - scans output for `TESTRESULT: PASS` or `TESTRESULT: FAIL <reason>`
  - exit 0 on PASS, 1 on FAIL or timeout (default 30 s via `timeout`)
- [x] TCL test convention: test scripts call `pass` (prints `TESTRESULT: PASS`, exits 0) or
  `fail <reason>` (prints `TESTRESULT: FAIL <reason>`, exits 1); `check_eq` for comparisons

**12.3 Machine fixture**
- [x] `fw/tests/openMSX/fixtures/machines/msx1_jlpicart.xml`:
  - MSX1 using C-BIOS (no proprietary ROM; `apt install cbios`)
  - TMS9918A VDP (NTSC), YM2149 PSG, 64 KB RAM
  - slot 0: C-BIOS Main + Logo ROMs (SHA1s match system `/usr/share/openmsx/systemroms/`)
  - slot 1: `external="true"` (JLPiCart menu page loaded via `-cart menupage.rom`)
  - slot 2: `external="true"` (second cartridge slot, unused in tests)
  - slot 3: 64 KB RAM
  - `OPENMSX_USER_DATA=fw/tests/openMSX/fixtures/` → openMSX finds `machines/msx1_jlpicart.xml`
  - `OPENMSX_SYSTEM_DATA=/usr/share/openmsx` → C-BIOS ROMs + standard machine scripts found
- [x] `menupage.rom` generation target already in `fw/src/msx/menu/stub/Makefile` (Stage 11)

**12.4 Menu stub emulator test (`test_menu_stub_basic.tcl`)**
- [x] Boot MSX; `after time 2.0 step_wait_init` — wait 2 s machine time then check
  `resp_seq == cmd_seq` at 0x4040
- [x] `GET_HOST_INFO (0x0001)`: `mbx_send` → 0.1 s delay → check `status == OK`,
  `out_len == 12`, `msx_gen ∈ [1..4]`, `text_cols == 40`
- [x] `SET_MODE(0) (0x0002)`: arg0=0 → check `status == OK`
- [x] `CLEAR (0x0003)`: check `status == OK`
- [x] `PUT_TEXT (0x0004)` at (row=0,col=0) with "JLP": arg0=0x0000, in_str="JLP" → `status == OK`
  (arg0 = (row<<8)|col; 0-based coords, stub converts to 1-based for POSIT)
- [x] `READ_INPUT (0x0005)`: check `status == OK`, `out_len == 16`
- [x] Unknown command (0xFFFF): check `status == MENU_E_UNSUPPORTED (0x0001)`
- [x] All checks pass → `pass` proc prints `TESTRESULT: PASS`, calls `exit 0`

### Definition of done

- [ ] `fw/tools/build_openmsx.sh` succeeds on a clean Linux amd64 checkout
- [ ] `run_test.sh test_menu_stub_basic.tcl` exits 0
- [ ] `GET_HOST_INFO` returns `msx_gen == 1`, `text_cols == 40` for the MSX1 fixture

---

## Stage 13 — Menu and API window bus wiring

**Goal:** Map the 16 KB menu page at MSX page 1 (0x4000–0x7FFF, subslot 1) and the
16 KB API window buffer at MSX page 2 (0x8000–0xBFFF, subslot 2).  From this stage,
the Z80 can read both pages through the bus loop; the menu stub executes and the
MSX screen shows a banner.

### Files added

- `fw/src/bus/bus_map.h`, `.cc` — `BusMap::map_ro_region()`, `BusMap::map_rw_region()`:
  wire a flat SRAM buffer into `BUS::cartridges[subslot]` read/write callbacks

### Files modified

- `fw/src/peripherals/peripheral_manager.h`, `.cc` — add `map_menu_page(uint8_t* page)`
  and `map_api_window(const uint8_t* buf)` calling `BusMap`
- `fw/src/main.cc` — wire both mappings after `menu_mbx.init()` and `api_win.init()`;
  remove `TODO(menu-bus)` and `TODO(api-bus)` comments
- `fw/tests/openmsx/test_menu_bus.tcl` — openMSX test: verify both pages are readable

### Checklist

**13.1 BusMap: flat SRAM region mapping**
- [ ] `bus_map.h`:
  ```cpp
  // Map a read-only 16 KB region into BUS::cartridges[subslot].
  // page_base must be one of: 0x0000, 0x4000, 0x8000, 0xC000.
  void BusMap::map_ro_region(uint8_t subslot, uint16_t page_base,
                              const uint8_t* data);
  // Map a read-write 16 KB region (Z80 can write back, e.g., mailbox).
  void BusMap::map_rw_region(uint8_t subslot, uint16_t page_base,
                              uint8_t* data);
  ```
- [ ] `bus_map.cc` implementation:
  - compute segment index: `seg = page_base >> 13` (gives 0–7 for the 8×8 KB segments)
  - for ro: set `cartridge.memory_read_addresses[seg] = data` and `[seg+1] = data + 0x2000`
    (two 8 KB segments covering the 16 KB page); clear write callbacks for those segments
  - for rw: same plus `memory_write_addresses[seg/seg+1]`; set write callback to a small
    trampoline that stores the written byte: `page[addr & 0x3FFF] = val`
  - Guard all `BUS::` references with `#ifndef JLPICART_HOST_TEST`
- [ ] `static_assert` in `bus_map.cc` that segment indices are valid (0–7)

**13.2 PeripheralManager wiring**
- [ ] Add to `peripheral_manager.h`:
  ```cpp
  void map_menu_page(uint8_t* page);          // subslot 1, page 1 (0x4000)
  void map_api_window(const uint8_t* buf);    // subslot 2, page 2 (0x8000)
  ```
- [ ] `map_menu_page`: call `BusMap::map_rw_region(1, 0x4000, page)`; set
  `BUS::is_expanded = true`; log `"Menu page mapped: subslot 1 page 1 (0x4000–0x7FFF)"`
- [ ] `map_api_window`: call `BusMap::map_ro_region(2, 0x8000, buf)`;
  log `"API window mapped: subslot 2 page 2 (0x8000–0xBFFF)"`

**13.3 Main wiring**
- [ ] After `menu_mbx.init()` (step 9) and `api_win.init()` (step 8):
  ```cpp
  map_mgr.map_menu_page(menu_page);
  map_mgr.map_api_window(api_win.buf());
  ```
- [ ] `BUS::is_expanded = true` is idempotent; set once here; existing subslot register
  at 0xFFFF in the bus loop already handles subslot routing
- [ ] Remove `TODO(api-bus)` and `TODO(menu-bus)` comments

**13.4 openMSX test (`test_menu_bus.tcl`)**
- [ ] Load the full firmware image (or the menu-page ROM stub + API-window ROM stub);
  configure openMSX with subslot expansion enabled for our cartridge slot
- [ ] Select subslot 1 for page 1: write `0x51` to slot-select register (0xFFFF) to expose
  subslot 1 at 0x4000; read byte at 0x4000; verify it is `'J'` (MenuStubHeader.sig[0])
- [ ] Read bytes 0x4000–0x4003: verify `"JLMN"`
- [ ] Read byte 0x4004: verify `abi_major == 1`
- [ ] Select subslot 2 for page 2; read 0x8000–0x8003: verify `"JLP1"` (ApiWindowHeader.sig)
- [ ] Write a byte to 0x4040 (mailbox area): verify that a subsequent read returns the
  written value (RW mapping confirmed)
- [ ] Write a byte to 0x8000 (API window, RO): verify the API window buffer is unchanged
  (optional: openMSX should not crash; actual write protection depends on MSX memory model)

### Definition of done

- [ ] `run_test.sh test_menu_bus.tcl` passes in openMSX
- [ ] Firmware compiles for RP2350; boot log shows both mapping messages
- [ ] On hardware: MSX screen shows stub-initialised display (blank text mode)
      and does not hang on subslot access

---

## Stage 14 — USB host: collection install from USB media

**Goal:** Wire tinyusb (already a submodule at `fw/ext/tinyusb`) for USB mass-storage
host class.  The cartridge scans a USB stick for `/JLPICART/INSTALL/<id>/manifest.json`
on boot and calls `Installer::run()` for each found Install Intent.  After a successful
install the device boots the collection on next power cycle.

Spec reference: spec.md §11.1 (USB provisioning media layout, acceptance rules, receipts).

### Files added

- `fw/src/usb/usb_host.h`, `.cc` — `UsbHost`: init tinyusb, `poll()` (calls `tuh_task()`),
  `is_msc_mounted()`, mount/unmount callbacks
- `fw/src/usb/usb_install_reader.h`, `.cc` — `UsbInstallReader : InstallReader` backed
  by FatFs over tinyusb MSC block reads
- `fw/src/usb/usb_install_scanner.h`, `.cc` — `UsbInstallScanner::scan()`: enumerate
  `/JLPICART/INSTALL/*/manifest.json`; call `Installer::run()` for each; log results
- `fw/src/usb/diskio_tuh.cc` — FatFs `diskio` backend wired to `tuh_msc_read10()`
- `fw/tests/host/test_usb_scanner.cc` — host tests for scanner logic using `MemInstallReader`

### Files modified

- `fw/CMakeLists.txt` — enable tinyusb host (`CFG_TUH_ENABLED=1`, `CFG_TUH_MSC=1`);
  add FatFs sources; add `usb/` sources; configure tinyusb for RP2350 USB FS
- `fw/src/main.cc` — step 12: replace `TODO(usb-host)` stub with `usb_scanner.scan()`;
  add `usb_host.poll()` to Core 1 service loop
- `fw/tests/CMakeLists.txt` — add `test_usb_scanner`

### Checklist

**14.1 tinyusb host MSC**
- [ ] In `fw/CMakeLists.txt`:
  - add `fw/ext/tinyusb/src/` and host-class sources to the build
  - define `CFG_TUH_ENABLED=1`, `CFG_TUH_MSC=1`, `CFG_TUSB_MCU=OPT_MCU_RP2350` via
    a `tusb_config.h` header (add to `fw/src/usb/tusb_config.h`)
  - link `pico_unique_id` (needed for USB serial descriptor)
- [ ] `usb_host.cc`:
  - `UsbHost::init()`: call `tusb_init(BOARD_TUH_RHPORT, &tuh_config)` with RP2350 USB FS
    port; register mount/umount callbacks; set `g_msc_mounted = false`
  - `UsbHost::poll()`: call `tuh_task()`; guard with `#ifndef JLPICART_HOST_TEST`
  - `tuh_msc_mount_cb`: set `g_msc_mounted = true`; log `"USB MSC mounted (LUN 0)"`
  - `tuh_msc_umount_cb`: set `g_msc_mounted = false`; log `"USB MSC unmounted"`
  - `UsbHost::is_msc_mounted()`: return `g_msc_mounted`

**14.2 FatFs + diskio bridge**
- [ ] Add FatFs sources (`ff.c`, `ffsystem.c`, `ffunicode.c`) — copy from tinyusb
  `lib/fatfs/source/` or add as a separate ext entry; no git submodule needed if
  already bundled in tinyusb
- [ ] `diskio_tuh.cc`: implement `disk_read()` using `tuh_msc_read10()`; implement
  `disk_status()` and `disk_initialize()` using `g_msc_mounted`; `disk_write()` returns
  `RES_WRPRT` (read-only policy: we install from USB but do not write back)
- [ ] `UsbInstallReader::read_file(path, buf, max_len, out_len)`:
  `f_mount` (if not mounted), `f_open(path)`, `f_read`, `f_close`; return
  `STORAGE_NOT_FOUND` if file absent, `STORAGE_IO_ERROR` on FatFs error
- [ ] `UsbInstallReader::hash_file(path, digest[32])`: stream file through `sha256_ctx`;
  same error mapping
- [ ] `UsbInstallReader::file_exists(path)`: `f_stat`

**14.3 Scanner logic**
- [ ] `UsbInstallScanner::scan(kv, event_log, policy)`:
  - return early if `!usb_host.is_msc_mounted()`
  - `f_opendir("0:/JLPICART/INSTALL")` — return if dir absent (not an error)
  - `f_readdir` loop: for each entry with `AM_DIR` attribute:
    - build path `"0:/JLPICART/INSTALL/<name>/manifest.json"`
    - construct `UsbInstallReader` rooted at `"0:/JLPICART/INSTALL/<name>/"` (prepend root
      to any `path` argument passed to `read_file`/`hash_file`/`file_exists`)
    - call `Installer::run(reader, kv, event_log, policy, result)`
    - log `"install <name>: %s"` with result summary
  - At most `INSTALL_SCAN_MAX_DIRS = 8` dirs per scan (guard against malformed media)
- [ ] Skip re-install: before calling `Installer::run()`, read current `KV_COL_RECORD`;
  if `collection_id` and `version` match `manifest.json` (quick pre-parse of only
  those two fields), log `"already installed, skipping"` and continue

**14.4 Main wiring**
- [ ] Core 1 service loop: add `usb_host.poll()`:
  ```cpp
  while (true) {
      g_api_win->service_once();
      g_menu_mbx->tick();
      g_usb_host->poll();   // drives tuh_task()
      tight_loop_contents();
  }
  ```
- [ ] `UsbHost` instance as `static` in `main()`, pointer in a file-scope global
  `g_usb_host` (same pattern as `g_api_win`)
- [ ] Step 12 in `main()`: replace the `log_info("collection install: USB host not integrated")` line with:
  ```cpp
  UsbInstallScanner usb_scanner(usb_host);
  usb_scanner.scan(kv_store, event_log, policy_store);
  ```
  (blocking: runs once on boot; USB host poll loop handles future hot-plug in Core 1)
- [ ] Remove `TODO(usb-host)` comment

**14.5 Host tests (`test_usb_scanner.cc`)**
- [ ] `UsbInstallScanner` with a mock `UsbHost` (always `is_msc_mounted() = true`) and
  a mock filesystem adapter: verify `scan()` calls `Installer::run()` for each directory
  in `JLPICART/INSTALL/`
- [ ] Verify skip: second scan with same `collection_id`/`version` already in KvStore → no install
- [ ] Verify `INSTALL_SCAN_MAX_DIRS = 8` limit: 9 directories → only 8 processed
- [ ] Verify `is_msc_mounted() = false` → scan returns immediately without any install

### Definition of done

- [ ] Firmware compiles with tinyusb host MSC + FatFs enabled
- [ ] Host tests pass for scanner logic
- [ ] On hardware: USB stick with `/JLPICART/INSTALL/test/manifest.json` → device
  installs collection; EVENT_LOG receipt present; ContentStore shows active collection
  on next boot; ROM runs through the full pipeline

---

## Stage 15 — User Profiles & Identity

**Goal:** Persistent per-user profiles on the cartridge, plus the Identity API service
(0x03) that lets MSX software enumerate profiles and switch the active one.

Spec references: §4.5 (User Profiles), §6.2 (Profile Overrides schema), §7.1 (Identity
service).  SaveStore flash partition is reserved here but its API surface is deferred to
a later stage.

### Files added

- `fw/src/profiles/profile_format.h` — `ProfileRecord`, `ProfileIndex`, KV key constants
- `fw/src/profiles/profile_store.h` — `ProfileStore` CRUD interface
- `fw/src/profiles/profile_store.cc` — backed by a `KvStore` in PROFILES_KV partition
- `fw/src/msx/api/services/identity_service.h` — method IDs + dispatch prototype
- `fw/src/msx/api/services/identity_service.cc` — LIST/SET/GET_ACTIVE_PROFILE handlers
- `fw/tests/host/test_profiles.cc` — host tests

### Files modified

- `fw/src/storage/flash_layout.h` — add `FLASH_PROFILES_KV_*` and `FLASH_SAVES_KV_*`
  (carved from CONTENT_INDEX reserved space; CONTENT_INDEX shrinks from 3 MB to 2 MB)
- `fw/src/msx/api/api_types.h` — add Identity method IDs; add `IDN_ProfileListEntry`,
  `IDN_ActiveProfileResp` packed structs; bump `API_FEATURES_STAGE15`
- `fw/src/msx/api/api_window.h` — add `ProfileStore* profile_store_` member and
  `bind_profile_store()` method
- `fw/src/msx/api/api_window.cc` — dispatch `SVC_IDENTITY`; set `API_FEATURE_IDENTITY`
  in `feature_bits` when `bind_profile_store()` is called
- `fw/src/main.cc` — init `ProfileStore` from `PROFILES_KV`; call
  `api_win.bind_profile_store()`
- `fw/CMakeLists.txt` — add `src/profiles/profile_store.cc`
- `fw/tests/CMakeLists.txt` — add `test_profiles` target

### Checklist

**15.1 Flash layout**
- [ ] In `flash_layout.h`:
  - Add `FLASH_PROFILES_KV_OFS = 0x300000`, `FLASH_PROFILES_KV_SIZE = 0x080000` (512 KB)
  - Add `FLASH_SAVES_KV_OFS = 0x380000`, `FLASH_SAVES_KV_SIZE = 0x080000` (512 KB)
  - Adjust `FLASH_CONTENT_INDEX_OFS = 0x400000`, `FLASH_CONTENT_INDEX_SIZE = 0x200000` (2 MB,
    down from 3 MB)
  - Update `static_assert` chain to keep the contiguity guarantees intact

**15.2 ProfileRecord / ProfileIndex**
- [ ] `profile_format.h`:
  - `PROF_MAX_PROFILES = 8`, `PROF_NAME_MAX = 32`, `PROF_LANG_MAX = 8`
  - `PROF_ID_NONE = 0x0000`, `PROF_ID_GUEST = 0xFFFF`
  - `KV_PROF_INDEX = "prof.index"` (stores serialized `ProfileIndex`)
  - `KV_PROF_ACTIVE = "prof.active"` (stores `uint16_t profile_id`)
  - `ProfileRecord` (44 bytes packed): `profile_id u16`, `name[32]`, `lang[8]`, `flags u8`,
    `_pad u8`
  - `ProfileIndex`: `count u8`, `_pad u8`, `entries[PROF_MAX_PROFILES]`
    — sizeof = 354 bytes ≤ KV_MAX_VAL_LEN (512)

**15.3 ProfileStore**
- [ ] `ProfileStore::init(dev, part_ofs, part_size)` — calls `kv_.init()`; loads `active_id_`
  from `KV_PROF_ACTIVE` (default `PROF_ID_NONE` if absent)
- [ ] `ProfileStore::list(buf, max)` — reads `KV_PROF_INDEX`, fills `buf[0..n-1]`; returns count
- [ ] `ProfileStore::count()` — same read, just returns count
- [ ] `ProfileStore::create(name, lang, id_out)` — load index; allocate next unused ID (1-based,
  sequential scan); append entry; save index; return STORAGE_FULL if count == PROF_MAX_PROFILES
- [ ] `ProfileStore::get(profile_id, out)` — scan index; return STORAGE_NOT_FOUND if absent
- [ ] `ProfileStore::remove(profile_id)` — remove from index; if removed == active_id_, set
  active_id_ to PROF_ID_NONE and save KV_PROF_ACTIVE
- [ ] `ProfileStore::set_active(profile_id)` — verify ID exists (or PROF_ID_NONE); write
  KV_PROF_ACTIVE; update active_id_
- [ ] `ProfileStore::active()` — return cached active_id_

**15.4 Identity API service (0x03)**
- [ ] In `api_types.h`: add Identity method IDs (`IDN_LIST_PROFILES = 0x00`,
  `IDN_SET_ACTIVE_PROFILE = 0x01`, `IDN_GET_ACTIVE_PROFILE = 0x02`,
  `IDN_GUEST_BEGIN = 0x03`, `IDN_GUEST_END = 0x04`); add
  `IDN_ActiveProfileResp { u16 profile_id, u8 flags }` (3 bytes packed)
- [ ] `identity_service_handle(req, payload, payload_len, win, ps)`:
  - `IDN_LIST_PROFILES`: encode `{u16 count, {u16 id, u8 name_len, name_bytes…}...}` inline
    (profile names capped at `PROF_API_NAME_MAX = 16` to fit in API_MAX_MSG)
  - `IDN_SET_ACTIVE_PROFILE`: parse `{u16 profile_id}`; call `ps.set_active()`; respond OK
    or API_E_NOT_FOUND / API_E_INTERNAL on error
  - `IDN_GET_ACTIVE_PROFILE`: respond `IDN_ActiveProfileResp`
  - `IDN_GUEST_BEGIN` / `IDN_GUEST_END`: respond OK (stub)
  - unknown method: API_E_UNSUPPORTED

**15.5 ApiWindow wiring**
- [ ] Add `ProfileStore* profile_store_ = nullptr` to `ApiWindow`
- [ ] Add `void ApiWindow::bind_profile_store(ProfileStore& ps)`: stores pointer; ORs
  `API_FEATURE_IDENTITY` into `header().feature_bits`
- [ ] In `service_once()`: add `case SVC_IDENTITY:` — calls
  `identity_service_handle(…, *profile_store_)` if non-null, else API_E_UNSUPPORTED

**15.6 Main wiring**
- [ ] Declare `static ProfileStore profile_store` in `main()`
- [ ] After storage init: `profile_store.init(flash, FLASH_PROFILES_KV_OFS, FLASH_PROFILES_KV_SIZE)`
- [ ] After `api_win.init()`: `api_win.bind_profile_store(profile_store)`
- [ ] Log `"PROFILES_KV ready"` or `"PROFILES_KV init failed"` (mirrors KV/log pattern)

**15.7 Host tests (`test_profiles.cc`)**
- [ ] `test_profile_create_list`: create 3 profiles, list returns 3, IDs are distinct
- [ ] `test_profile_get`: get by known ID returns correct name
- [ ] `test_profile_active`: set_active/active round-trip; PROF_ID_NONE on fresh store
- [ ] `test_profile_remove`: remove profile; list shrinks; removing active resets to PROF_ID_NONE
- [ ] `test_profile_full`: create PROF_MAX_PROFILES profiles; 9th create returns STORAGE_FULL
- [ ] `test_identity_list`: identity service LIST_PROFILES encodes correct count and first name
- [ ] `test_identity_set_get_active`: SET then GET round-trip via API

### Definition of done

- [ ] All host tests pass
- [ ] `test_allocator` (pre-existing) still passes (flash_layout.h change must not break it)
- [ ] API identity service advertised in `feature_bits` when ProfileStore bound
- [ ] On hardware: profiles survive power cycle (persisted in PROFILES_KV)

---

## Stage 16 — Menu Application

**Goal:** Implement the RP2350-side `MenuApp` state machine that drives the Z80 stub via
`MenuMailbox` to display a navigable cartridge UI.  This is the "spine of the application"
and must be complete before any peripheral stages.

### Files added

- `fw/src/menu/input_decoder.h`  — MSX keyboard/joystick active-low decode helpers
- `fw/src/menu/menu_app.h`       — `MenuApp` class declaration
- `fw/src/menu/menu_app.cc`      — state machine implementation
- `fw/tests/host/test_menu_app.cc` — host tests

### Files modified

- `fw/src/msx/menu/menu_host_abi.h` — add `stub_host_caps()` accessor
- `fw/src/main.cc`                  — `MenuApp` init; Core 1 loop calls `menu_app.tick()`
- `fw/CMakeLists.txt`               — add `menu_app.cc` to firmware target
- `fw/tests/CMakeLists.txt`         — add `test_menu_app` target

### 16.1 input_decoder.h

- [ ] `key_pressed(snap, row, bit)` — active-low test (0 = pressed)
- [ ] `key_return`, `key_esc`, `key_up`, `key_down`, `key_left`, `key_right` — row/bit per MSX
  matrix (row 7 bit7=RETURN bit2=ESC; row 8 bit5=UP bit6=DOWN bit4=LEFT bit7=RIGHT)
- [ ] `joy1_up`, `joy1_down`, `joy1_trig` — PSG R14 bits 0,1,4 (active-low, 0=pressed)

### 16.2 MenuMailbox change

- [ ] Add `uint32_t stub_host_caps() const` to `MenuMailbox` — returns `hdr_->host_caps` (0 if
  not yet initialised); the Z80 stub writes this before writing `resp_seq = cmd_seq` at init,
  so non-zero means stub is ready to receive commands

### 16.3 MenuApp screens and step model

Six screens: `BOOT`, `BOOT_INFO`, `MAIN`, `COLLECTIONS`, `PROFILES`, `SETTINGS`.

- `tick()`: if a command is pending, call `mbx_->tick()`; return if still waiting; then dispatch
  to the current screen handler.  Each handler issues at most one command and advances `step_`.
- `step_` tracks position within the current screen's render+input sequence.
- `switch_screen(s)` sets `screen_`, `step_=0`, `cursor_=0`.

Screen step sequences:

| Screen      | Steps |
|-------------|-------|
| BOOT        | 0: poll `stub_host_caps()`; no commands |
| BOOT_INFO   | 0: send GET_HOST_INFO; 1: parse response → load data → switch to MAIN |
| MAIN        | 0:SET_MODE 1:CLEAR 2:header 3:game-info 4..6:items 7:footer 8:READ_INPUT 9:process |
| COLLECTIONS | 0:CLEAR 1:header 2..4:info 5:Back 6:footer 7:READ_INPUT 8:process |
| PROFILES    | 0:CLEAR 1:header 2+i:profile i (variable) base:New base+1:Back base+2:footer base+3:READ_INPUT base+4:process |
| SETTINGS    | 0:CLEAR 1:header 2:content 3:Back 4:footer 5:READ_INPUT 6:process |

- [ ] `MenuApp::init(mbx, kv, ps)` — store references; set BOOT screen
- [ ] `MenuApp::tick()` — pending check then screen dispatch
- [ ] `tick_boot()` — poll `stub_host_caps()`, switch to BOOT_INFO when non-zero
- [ ] `tick_boot_info()` — send GET_HOST_INFO; on response parse HostInfo, call
  `load_collection_data()` and `load_profile_data()`, switch to MAIN
- [ ] `tick_main()` — renders collection status + 3 items (Collections/Profiles/Settings) + footer
  + READ_INPUT; processes navigation and RETURN-to-select
- [ ] `tick_collections()` — renders collection title/version/publisher or "no collection";
  any key returns to MAIN
- [ ] `tick_profiles()` — lists profiles (active marked `*`), "New Profile...", "Back";
  RETURN on profile sets it active; RETURN on New Profile auto-creates "Profile N";
  RETURN on Back or ESC returns to MAIN
- [ ] `tick_settings()` — stub "(no settings configured)", any key returns to MAIN
- [ ] `load_collection_data()` — constructs `ContentStore(*kv_)` locally, reads `has_collection_`
  and `col_record_`
- [ ] `load_profile_data()` — calls `ps_->list()` and `ps_->active()`
- [ ] `put_text(col, row, text)` — packages `MENU_CMD_PUT_TEXT` with `arg0 = col|(row<<8)`

### 16.4 Main wiring

- [ ] Add `static MenuApp menu_app;` after `MenuMailbox` declaration
- [ ] After `menu_mbx.init(...)`: `menu_app.init(menu_mbx, kv_store, profile_store);`
- [ ] Core 1 loop: replace `g_menu_mbx->tick()` with `g_menu_app->tick()`
- [ ] Host test loop: replace `menu_mbx.tick()` with `menu_app.tick()`

### 16.5 Host tests (`test_menu_app.cc`)

- [ ] `test_menu_boot_to_boot_info` — no cmd until host_caps set; GET_HOST_INFO sent after
- [ ] `test_menu_boot_info_to_main` — after GET_HOST_INFO ack, SET_MODE sent (MAIN starts)
- [ ] `test_menu_main_renders` — full MAIN render sequence: SET_MODE, CLEAR, 5 PUT_TEXT,
  READ_INPUT in order
- [ ] `test_menu_main_navigation` — DOWN input changes cursor, triggers re-render from step 0
- [ ] `test_menu_main_select_profiles` — RETURN on item 1 sends CLEAR (PROFILES screen started)
- [ ] `test_menu_profiles_renders` — PROFILES: CLEAR, header, profile items, New, Back in order

### Definition of done

- [ ] All host tests pass (`ctest` green)
- [ ] Pre-existing tests unaffected
- [ ] On hardware: menu boots, navigates between screens, profiles accessible from Profiles screen

---

## Stage 17 — System Settings

**Goal:** Persistent, typed System Settings backed by the existing `FLASH_SYSTEM_KV_OFS`
partition.  All downstream features (WiFi, language, source priority, user overrides) have
nowhere to read defaults from until this store exists.  The Menu Settings screen changes
from a placeholder to a live display of current values.

Spec references: §4.5 "System Configuration", §5.3 "Persistence contract (v1)",
§6.2 "System Settings schema (jlpicart.system.v1)".

### Files added

- `fw/src/settings/system_settings.h`       — `SystemSettings` packed struct + KV key constants
- `fw/src/settings/system_settings_store.h` — `SystemSettingsStore` load/save interface
- `fw/src/settings/system_settings_store.cc`— implementation over the existing `KvStore`
- `fw/tests/host/test_system_settings.cc`   — host tests

### Files modified

- `fw/src/main.cc`       — declare `SystemSettingsStore settings_store`; init from `kv_store`;
  pass reference to `MenuApp`
- `fw/src/menu/menu_app.h/cc` — accept `SystemSettingsStore&`; replace
  `"(no settings configured)"` stub with a live read-only summary render
- `fw/CMakeLists.txt`          — add `system_settings_store.cc`
- `fw/tests/CMakeLists.txt`    — add `test_system_settings` target

### Checklist

**17.1 SystemSettings struct**
- [ ] `system_settings.h`:
  - `KV_SYS_SETTINGS = "sys.settings"` — single blob key for the whole struct
  - `SYS_VIDEO_AUTO=0`, `SYS_VIDEO_CRT=1`, `SYS_VIDEO_VGA=2`
  - `SYS_SRC_FLASH=0`, `SYS_SRC_USB=1`, `SYS_SRC_OPTICAL=2`, `SYS_SRC_NETWORK=3`
  - `SystemSettings` struct (power-of-two padded, ≤ KV max value size):
    - `char wifi_ssid[64]` — NUL-terminated; empty = no credentials
    - `char wifi_pass[64]` — NUL-terminated
    - `char language[8]`  — BCP-47 tag, default `"en"`
    - `uint8_t video_mode`        — `SYS_VIDEO_*`
    - `uint8_t network_enabled`   — 1=on (default), 0=globally disabled
    - `uint8_t guest_allowed`     — 1=on (default)
    - `uint8_t source_priority[4]`— ordered `SYS_SRC_*` values; default flash,usb,optical,network
    - `uint8_t reserved[46]`      — MUST be zero; pads struct to 192 bytes

**17.2 SystemSettingsStore**
- [ ] `SystemSettingsStore::init(kv)` — store reference; call `load()` into cached `settings_`
- [ ] `SystemSettingsStore::load()` — read `KV_SYS_SETTINGS`; on missing/corrupt apply
  `defaults()` and return `DIAG_NOT_FOUND` (not an error for callers)
- [ ] `SystemSettingsStore::save()` — write `KV_SYS_SETTINGS` atomically; return DiagStatus
- [ ] `SystemSettingsStore::get()` — return const ref to cached `settings_`
- [ ] `SystemSettingsStore::set(settings)` — update cache and call `save()`
- [ ] `SystemSettingsStore::defaults()` — return a `SystemSettings` with the canonical
  defaults listed above; used when the KV key is absent

**17.3 Main wiring**
- [ ] Declare `SystemSettingsStore settings_store` after `kv_store` init in `main()`
- [ ] Call `settings_store.init(kv_store)` immediately after; log init result
- [ ] Pass `settings_store` reference to `menu_app.init(...)` (extend signature)

**17.4 Menu Settings screen**
- [ ] Extend `MenuApp::init` to accept `SystemSettingsStore&`; store as `ss_`
- [ ] In `tick_settings()`: instead of the placeholder string, render:
  - row 2: `"  WiFi: <ssid or \"(none)\">"`
  - row 3: `"  Lang: <language>"`
  - row 4: `"  Video: <auto|crt|vga>"`
  - row 5: `"  Net: <on|off>"`
  - (read-only display; edit UI is a later feature stage)
- [ ] Keep the "Back → MAIN on any key" behavior

**17.5 Factory reset operations**

Spec §5.3 and §10 (Privacy) both MUST a "delete local data" operation.

- [ ] `SystemSettingsStore::wipe_user_data(kv_saves, kv_profiles)` — deletes all keys with
  prefix `"sav."` from the saves KV and all `"prof.*"` keys from the profiles KV;
  does NOT touch the collection index (SYSTEM_KV), installed collections, or EVENT_LOG
- [ ] `SystemSettingsStore::full_wipe(kv_saves, kv_profiles)` — same as above, plus erases
  `KV_SYS_SETTINGS` itself (restoring defaults on next boot)
- [ ] Both operations MUST NOT touch `FLASH_CONTENT_INDEX_OFS` (permanently installed Collections
  are preserved in a user data wipe unless the user explicitly selects "full wipe")
- [ ] Menu Settings screen gains a "Wipe user data" option that calls `wipe_user_data()`
  (confirmation prompt via two-key sequence to prevent accidents)

**17.6 Host tests (`test_system_settings.cc`)**
- [ ] `test_settings_defaults`: fresh store returns canonical defaults; `wifi_ssid` empty,
  `language=="en"`, `network_enabled==1`
- [ ] `test_settings_roundtrip`: set custom values, save, reload from same flash → same values
- [ ] `test_settings_missing_key`: KV absent → `load()` returns `DIAG_NOT_FOUND`; get() returns
  defaults; no crash
- [ ] `test_settings_corrupt`: write garbage to `KV_SYS_SETTINGS`; reload → defaults applied
- [ ] `test_wipe_user_data`: write a profile and save blob; call `wipe_user_data()`; verify
  profile KV is empty, save KV is empty, system settings still present
- [ ] `test_full_wipe`: call `full_wipe()`; system settings revert to defaults on next load

### Definition of done

- [ ] All host tests pass
- [ ] `test_menu_app` (pre-existing) still passes after `MenuApp::init` signature change
- [ ] On hardware: settings survive power cycle; Menu Settings screen shows live values

---

## Stage 18 — RESET_TO_MENU

**Goal:** System service method `0x04 RESET_TO_MENU` returns a real response and causes
`MenuApp` to return to the MAIN screen.  This is now unblocked by Stage 16: the Menu ABI
is complete.  It is also the cheapest remaining spine item.

Spec references: §7.2 System service method `0x04 RESET_TO_MENU`.

### Files modified

- `fw/src/menu/menu_app.h`                     — add `request_reset_to_menu()` + `reset_requested_` flag
- `fw/src/menu/menu_app.cc`                    — check flag at top of `tick()`; call
  `switch_screen(Screen::MAIN)` and clear flag
- `fw/src/msx/api/api_window.h`                — add `set_reset_menu_fn(void(*)())` to register
  a reset callback
- `fw/src/msx/api/api_window.cc`               — store callback; pass to core service dispatch
- `fw/src/msx/api/services/core_service.h`     — add `reset_menu_fn_` callback parameter
- `fw/src/msx/api/services/core_service.cc`    — `SYS_RESET_TO_MENU`: respond OK then invoke
  callback if non-null (replaces `API_E_UNSUPPORTED`)
- `fw/src/main.cc`                             — after `menu_app.init(...)`:
  `api_win.set_reset_menu_fn([]{ menu_app.request_reset_to_menu(); })`

### Checklist

**18.1 MenuApp reset flag**
- [ ] Add `bool reset_requested_ = false` to `MenuApp`
- [ ] `request_reset_to_menu()`: set `reset_requested_ = true`
- [ ] In `tick()`, before the pending-command check: if `reset_requested_`, call
  `switch_screen(Screen::MAIN)` and clear `reset_requested_`; then continue normally

**18.2 GET_RANDOM**

The RP2350 provides hardware TRNG via `rosc_hw->randombit` (or the dedicated TRNG peripheral
in RP2350 A2+).  The "Stage 5: requires TRNG access" stub comment is obsolete.

- [ ] Add `handle_get_random(req, win)` to `core_service.cc`:
  - parse `u16 nbytes` from request payload; cap at `c2h_scratch_len`
  - fill `nbytes` bytes into the response scratch using RP2350 TRNG
    (guard with `#ifndef JLPICART_HOST_TEST`; in host test return a counter-based stub)
  - respond with `scratch_len = nbytes`, `payload_len = 0`
- [ ] Remove the `API_E_UNSUPPORTED` return from `SYS_GET_RANDOM`

**18.3 CoreService wiring (RESET_TO_MENU)**
- [ ] Add `void (*reset_menu_fn_)() = nullptr` to `CoreService`
- [ ] Add `set_reset_menu_fn(void(*)())` setter
- [ ] In `SYS_RESET_TO_MENU` handler: respond `OK` first; then call `reset_menu_fn_()` if
  non-null; remove the `API_E_UNSUPPORTED` return

**18.4 ApiWindow wiring**
- [ ] Propagate the callback from `ApiWindow::set_reset_menu_fn()` down to `CoreService`

**18.5 Main wiring**
- [ ] After `menu_app.init(...)` and `api_win.init(...)`:
  call `api_win.set_reset_menu_fn([]{ g_menu_app->request_reset_to_menu(); })` using the
  existing `g_menu_app` static pointer pattern

**18.6 Host tests**
- [ ] Add `test_reset_to_menu` in `test_menu_app.cc` (or a new `test_core_service.cc`):
  - Boot to MAIN; simulate game calling `SYS_RESET_TO_MENU` via API; verify MenuApp returns
    to MAIN screen (first command after reset is `MENU_CMD_SET_MODE`)
  - If called during BOOT screen (before menu is up): verify response is OK and no crash
- [ ] Add `test_get_random`: call `SYS_GET_RANDOM` with `nbytes=16`; verify response OK and
  `scratch_len==16`; in host test, verify stub fills bytes (non-zero or deterministic counter)

### Definition of done

- [ ] Host tests pass; `RESET_TO_MENU` returns OK (not `API_E_UNSUPPORTED`)
- [ ] `GET_RANDOM` returns OK with requested byte count (not `API_E_UNSUPPORTED`)
- [ ] `MenuApp` returns to MAIN screen after the API call is processed

---

## Stage 19 — Storage service (0x01)

**Goal:** Save blob CRUD API backed by `FLASH_SAVES_KV_OFS` (already partitioned).
This satisfies the spec conformance requirement: "a conforming implementation MUST implement
at least one of Storage or UserStats" (§7.2).

Spec references: §7.2 Storage service (0x01): `LIST_BLOBS`, `READ_BLOB`,
`WRITE_BLOB_BEGIN/CHUNK/COMMIT`, `DELETE_BLOB`.

### Files added

- `fw/src/storage/save_store.h`  — blob CRUD interface over a `KvStore`
- `fw/src/storage/save_store.cc` — implementation; blob keys use scheme `"sav.<profile_id>.<blob_id>"`
- `fw/src/msx/api/services/storage_service.h` — method IDs + dispatch prototype
- `fw/src/msx/api/services/storage_service.cc`— handlers for all Storage methods
- `fw/tests/host/test_save_store.cc`          — host tests

### Files modified

- `fw/src/msx/api/api_types.h`   — add Storage method IDs + request/response structs;
  add `API_FEATURES_STAGE19 = API_FEATURE_SYSTEM | API_FEATURE_IDENTITY | API_FEATURE_STORAGE`
- `fw/src/msx/api/api_window.h`  — add `SaveStore* save_store_` + `bind_save_store()` method
- `fw/src/msx/api/api_window.cc` — dispatch `SVC_STORAGE`; set `API_FEATURE_STORAGE` in
  `feature_bits` when `bind_save_store()` is called
- `fw/src/main.cc`               — declare `static KvStore saves_kv`; init from
  `FLASH_SAVES_KV_OFS/SIZE`; declare `static SaveStore save_store`; init;
  call `api_win.bind_save_store(save_store)`
- `fw/CMakeLists.txt`            — add `save_store.cc`, `storage_service.cc`
- `fw/tests/CMakeLists.txt`      — add `test_save_store` target

### Checklist

**19.1 SaveStore**
- [ ] `SaveStore::init(kv, profile_store)` — store references
- [ ] Blob key scheme: `snprintf(key, "sav.%04x.%04x", profile_id, blob_id)` — unique per
  profile+blob; fits in KV key limit
- [ ] `SaveStore::list(profile_id, kind, out, max_out)` — scan keys matching
  `"sav.<profile_id>."` prefix; populate `out[]` with `{blob_id, size, max_bytes, flags}`
- [ ] `SaveStore::read(profile_id, blob_id, offset, buf, len)` — read blob bytes;
  return `DIAG_NOT_FOUND` if absent
- [ ] `SaveStore::write_begin(profile_id, blob_id, total_len, flags, handle_out)` —
  allocate a write handle (simple index into a small table of in-progress writes); return
  `DIAG_FULL` if table full
- [ ] `SaveStore::write_chunk(handle, offset, buf, len)` — accumulate into a staging buffer
- [ ] `SaveStore::write_commit(handle)` — write staged bytes to KV atomically; release handle
- [ ] `SaveStore::delete_blob(profile_id, blob_id)` — delete key; return `DIAG_NOT_FOUND` if absent
- [ ] Max concurrent in-progress writes: `SAVE_WRITE_HANDLES = 4`

**19.2 Storage API service**
- [ ] Add Storage method IDs to `api_types.h`:
  `STG_LIST_BLOBS=0x00`, `STG_READ_BLOB=0x01`, `STG_WRITE_BLOB_BEGIN=0x02`,
  `STG_WRITE_BLOB_CHUNK=0x03`, `STG_WRITE_BLOB_COMMIT=0x04`, `STG_DELETE_BLOB=0x05`
- [ ] `storage_service_handle(req, payload, len, win, save_store)`:
  - `STG_LIST_BLOBS`: parse `{u8 blob_kind}`; call `save_store.list()`; encode response
  - `STG_READ_BLOB`: parse `{u16 blob_id, u32 offset, u16 len}`; read into response scratch
  - `STG_WRITE_BLOB_BEGIN`: parse `{u16 blob_id, u32 total_len, u16 flags}`; respond with
    `{u16 handle, u16 chunk_hint}` (chunk_hint = scratch buffer size)
  - `STG_WRITE_BLOB_CHUNK`: parse `{u16 handle, u32 offset, u16 len}`; data from request
    scratch; respond OK
  - `STG_WRITE_BLOB_COMMIT`: parse `{u16 handle}`; commit; respond OK or error
  - `STG_DELETE_BLOB`: parse `{u16 blob_id}`; delete; respond OK or `E_NOT_FOUND`

**19.3 ApiWindow wiring**
- [ ] `bind_save_store(SaveStore&)`: store pointer; OR `API_FEATURE_STORAGE` into
  `header().feature_bits`
- [ ] In `service_once()`: `case SVC_STORAGE:` → call `storage_service_handle` if non-null,
  else `E_UNSUPPORTED`

**19.4 Main wiring**
- [ ] Declare and init `static KvStore saves_kv` from `FLASH_SAVES_KV_OFS`/`SIZE` after
  PROFILES_KV init; log result
- [ ] Declare `static SaveStore save_store`; call `save_store.init(saves_kv, profile_store)`
- [ ] After `api_win.init(...)`: call `api_win.bind_save_store(save_store)`

**19.5 Host tests (`test_save_store.cc`)**
- [ ] `test_save_write_read`: write blob, read back exact bytes; round-trip integrity
- [ ] `test_save_list`: write two blobs for same profile; list returns both; sizes correct
- [ ] `test_save_delete`: write blob, delete, list returns 0; read returns `DIAG_NOT_FOUND`
- [ ] `test_save_profile_isolation`: blob written under profile A not visible under profile B
- [ ] `test_save_concurrent_writes`: open `SAVE_WRITE_HANDLES` simultaneous writes; all commit
  successfully; (SAVE_WRITE_HANDLES + 1)th open returns `DIAG_FULL`
- [ ] `test_storage_api_roundtrip`: call `STG_WRITE_BLOB_BEGIN/CHUNK/COMMIT` + `STG_READ_BLOB`
  via the API framing; verify round-trip

### Definition of done

- [ ] All host tests pass
- [ ] `API_FEATURE_STORAGE` bit set in `feature_bits` when save store is bound
- [ ] Platform is now spec-conformant (System + Identity + Storage all active)
- [ ] On hardware: save blob persists across power cycle

---

## Stage 20 — Boot mode enforcement

**Goal:** `CollectionRecord::boot_mode` (parsed since Stage 7 and stored since Stage 9) is
actually respected at runtime.  A `direct`-boot collection bypasses MAIN and transitions
directly to a LAUNCH screen.  A `menu_first` collection (or no collection) boots to MAIN
as today.

This stage implements the Preflight→Activation part of §5.2 for the common case
(ROM already in flash).  Full staging (cache fill, WAIT-line handoff) is deferred.

Spec references: §4.5 "Collections and Payloads" (boot mode), §5.2 "Launch workflow contract (v1)".

### Files added

- `fw/src/menu/launch_screen.h`  — `tick_launch()` step table declaration (kept in `menu_app.cc`)
- `fw/tests/host/test_boot_mode.cc` — host tests

### Files modified

- `fw/src/menu/menu_app.h`   — add `Screen::LAUNCH`; add `launch_payload_id_[64]` field
- `fw/src/menu/menu_app.cc`  — `tick_boot_info()`: check `boot_mode`; `switch_screen(LAUNCH)`
  if direct; add `tick_launch()` handler
- `fw/src/msx/api/api_window.h` — add `active_payload_id_[64]` field + `set_active_payload()` setter
- `fw/tests/CMakeLists.txt`  — add `test_boot_mode` target

### Checklist

**20.1 LAUNCH screen**
- [ ] Add `Screen::LAUNCH` to the `Screen` enum in `menu_app.h`
- [ ] `tick_launch()` step sequence:
  - step 0: `MENU_CMD_CLEAR`
  - step 1: `PUT_TEXT 0,0 "  Launching <title>..."`
  - step 2: `PUT_TEXT 0,2 "  (press any key to cancel)"` — stub; real WAIT-line handoff is later
  - step 3: `MENU_CMD_READ_INPUT`
  - step 4: process — any key cancels and returns to MAIN; otherwise loop back to step 3
    (payload remains active on bus; this is a placeholder until Core 0 handoff is staged)

**20.2 Boot mode check in tick_boot_info()**
- [ ] After `load_collection_data()`: if `has_collection_` and `col_record_.boot_mode == 1u`
  (direct), copy `col_record_.title` to `launch_title_` and call `switch_screen(Screen::LAUNCH)`
- [ ] Otherwise: `switch_screen(Screen::MAIN)` as before

**20.3 Active payload context in ApiWindow**

Stage 21 (UserStats) keys stats per `profile_id + payload_id`.  `ApiWindow` needs to know
which payload is currently active so service handlers can use the correct key.

- [ ] Add `char active_payload_id_[64]` to `ApiWindow`; default to empty string
- [ ] Add `void ApiWindow::set_active_payload(const char* payload_id)` setter
- [ ] In `tick_launch()` step 0 (CLEAR), call `api_win_->set_active_payload(launch_payload_id_)`
  to register the active payload; MenuApp receives an `ApiWindow*` via `init()` for this
- [ ] When transitioning from LAUNCH back to MAIN (cancel or collection done), call
  `api_win_->set_active_payload("")` to clear the active payload

**20.4 MAIN → LAUNCH path**
- [ ] When the user selects a collection from the Collections screen (future: payload selection
  within a collection), the selection also triggers `switch_screen(Screen::LAUNCH)`
- [ ] For now, the Collections screen "Back" still returns to MAIN (no payload selection UI yet)

**20.5 Host tests (`test_boot_mode.cc`)**
- [ ] `test_direct_boot`: create a collection with `boot_mode=1`; after GET_HOST_INFO ack,
  verify MenuApp transitions to LAUNCH screen (first command is `MENU_CMD_CLEAR`, not
  `MENU_CMD_SET_MODE`)
- [ ] `test_menu_first_boot`: collection with `boot_mode=0`; after GET_HOST_INFO ack, verify
  MenuApp transitions to MAIN (first command is `MENU_CMD_SET_MODE`)
- [ ] `test_no_collection_boot`: no collection in KV; verify MAIN regardless

### Definition of done

- [ ] Host tests pass
- [ ] On hardware: a direct-boot collection shows the LAUNCH screen on power-on, not MAIN

---

## Stage 21 — UserStats service (0x04)

**Goal:** Per-user, per-payload stats, achievements, and leaderboard submissions.  Offline
persistence is the baseline (network sync is additive, not required here).  Together with
Stage 19, this provides the full API conformance profile.

Spec references: §7.2 UserStats service (0x04), §7.3 "Stats, achievements, and leaderboards
contract (v1)".

### Files added

- `fw/src/stats/stats_store.h`   — stat/achievement/leaderboard persistence interface
- `fw/src/stats/stats_store.cc`  — backed by a `KvStore` slice in `FLASH_SAVES_KV_OFS`
  (separate key namespace `"st.*"` from save blobs `"sav.*"`)
- `fw/src/msx/api/services/userstats_service.h` — method IDs + dispatch prototype
- `fw/src/msx/api/services/userstats_service.cc`— handlers for all UserStats methods
- `fw/tests/host/test_stats_store.cc`           — host tests

### Files modified

- `fw/src/msx/api/api_types.h`   — add UserStats method IDs + structs;
  add `API_FEATURE_USERSTATS` to feature bitmap
- `fw/src/msx/api/api_window.h`  — add `StatsStore*` + `bind_stats_store()` method
- `fw/src/msx/api/api_window.cc` — dispatch `SVC_USERSTATS`; set `API_FEATURE_USERSTATS`
  in `feature_bits` when `bind_stats_store()` is called
- `fw/src/main.cc`               — declare `static StatsStore stats_store`; init from
  `saves_kv` (same partition, distinct key prefix); call `api_win.bind_stats_store()`
- `fw/CMakeLists.txt`            — add `stats_store.cc`, `userstats_service.cc`
- `fw/tests/CMakeLists.txt`      — add `test_stats_store` target

### Checklist

**21.1 StatsStore**
- [ ] Key scheme:
  - stat:        `"st.<profile_id>.<payload_id>.s.<stat_id>"`   → `int32_t` value
  - achievement: `"st.<profile_id>.<payload_id>.a.<ach_id>"`    → `uint8_t` (0/1)
  - leaderboard entry: `"st.<profile_id>.<payload_id>.l.<lb_id>"` → packed `{u32 score, u32 ts}`
- [ ] `StatsStore::init(kv, profile_store)` — store references
- [ ] `StatsStore::stat_get(profile_id, payload_id, stat_id, out_value)` — read key;
  return 0 if absent (not an error)
- [ ] `StatsStore::stat_set(profile_id, payload_id, stat_id, value, op)` — op 0=set, 1=add,
  2=max; read-modify-write atomically via KV write; return DiagStatus
- [ ] `StatsStore::ach_unlock(profile_id, payload_id, ach_id)` — write `1` to ach key;
  idempotent (already-unlocked is OK)
- [ ] `StatsStore::leader_begin(profile_id, payload_id, lb_id, token_out, token_len)` —
  generate a 16-byte nonce token (from `TRNG` or counter; store it in a small table);
  return token bytes
- [ ] `StatsStore::leader_submit(handle, score, proof_kind, proof_buf, proof_len)` —
  validate token is known; write leaderboard entry (local only for now); mark token used

**21.2 UserStats API service**
- [ ] Add UserStats method IDs to `api_types.h`:
  `UST_STAT_GET=0x00`, `UST_STAT_SET=0x01`, `UST_ACH_UNLOCK=0x02`,
  `UST_LEADER_RUN_BEGIN=0x03`, `UST_LEADER_SUBMIT=0x04`
- [ ] `userstats_service_handle(req, payload, len, win, stats_store)`:
  - `UST_STAT_GET`: parse `{u16 stat_id}`; respond `{s32 value}`
  - `UST_STAT_SET`: parse `{u16 stat_id, s32 value, u8 op}`; respond OK
  - `UST_ACH_UNLOCK`: parse `{u16 ach_id}`; respond OK
  - `UST_LEADER_RUN_BEGIN`: parse `{u16 lb_id}`; respond token in scratch
  - `UST_LEADER_SUBMIT`: parse `LeaderSubmitReq` (see §7.2); respond OK or error
  - active profile from `profile_store.active()`; active payload ID from a
    per-request context parameter (set by ApiWindow from a future `active_payload_id_` field)
  - unknown method: `E_UNSUPPORTED`

**21.3 ApiWindow wiring**
- [ ] `bind_stats_store(StatsStore&)`: store pointer; OR `API_FEATURE_USERSTATS` into
  `header().feature_bits`
- [ ] In `service_once()`: `case SVC_USERSTATS:` → call handler if non-null

**21.4 Main wiring**
- [ ] `static StatsStore stats_store`; `stats_store.init(saves_kv, profile_store)` (reuses
  `saves_kv` initialized in Stage 19)
- [ ] `api_win.bind_stats_store(stats_store)` after `api_win.init()`

**21.5 Host tests (`test_stats_store.cc`)**
- [ ] `test_stat_set_get`: set stat 1 = 42; get returns 42; absent stat returns 0
- [ ] `test_stat_add`: set 10, add 5 → 15; add negative → 10
- [ ] `test_stat_max`: set 10, max(8) → stays 10; max(20) → 20
- [ ] `test_ach_unlock`: unlock ach; re-unlock is idempotent; get returns 1
- [ ] `test_leaderboard_submit`: begin run → token; submit with score; local entry written
- [ ] `test_stats_profile_isolation`: stat written under profile A not visible under profile B

### Definition of done

- [ ] All host tests pass
- [ ] `API_FEATURE_USERSTATS` bit set in `feature_bits` when stats store is bound
- [ ] Platform is fully spec-conformant: System + Identity + Storage + UserStats all active
- [ ] On hardware: stat survives power cycle; achievement unlock persists

---

## Stage 22 — Device Identity Key (DIK) and GET_DEVICE_ID

**Goal:** Establish a stable, device-unique identity so that (a) `GET_DEVICE_ID` returns a
real ID, and (b) Stage 21 leaderboard submissions can be signed with the device key.  The
spec (§10, §11) is explicit: "During initialization, the device MUST establish a Device
Identity Key (DIK).  The private key MUST never leave the device."

Without this stage, Stage 21 is locally functional but non-conformant on the anti-cheat
contract (submissions cannot be signed), and online service integration is impossible.

Spec references: §10 "Key hierarchy and roles (DIK)", §11.1 "Initialization and provisioning",
§7.2 System service method `0x01 GET_DEVICE_ID`.

### Design constraints

- DIK MUST be an asymmetric keypair (ed25519 or P-256 recommended).
- Private key MUST be stored encrypted under a key derived from the RP2350 OTP device secret
  seed (SMK derivation per §10 "Storage encryption contract").
- Public key MAY be exposed via `GET_DEVICE_ID` (scope=device, policy permitting).
- If no OTP device secret exists (development unit without provisioned secret), the DIK is
  derived from a flash-stored random seed (clearly flagged as "unprovisioned" in posture).
- DIK generation MUST be idempotent: generated once on first boot, then reloaded from flash.

### Files added

- `fw/src/identity/device_identity.h`  — `DeviceIdentity` class: keygen, load/save, sign, public key
- `fw/src/identity/device_identity.cc` — implementation; uses RP2350 TRNG for keygen;
  derives storage key from OTP secret via HKDF-SHA256 (or stores plaintext on unprovisioned units
  with a flag); ed25519 via a small bundled implementation or RP2350 hardware acceleration
- `fw/tests/host/test_device_identity.cc` — host tests

### Files modified

- `fw/src/msx/api/services/core_service.h/cc` — accept `DeviceIdentity&`;
  implement `SYS_GET_DEVICE_ID`: return 16-byte scoped ID (HKDF of DIK public key + scope label)
- `fw/src/msx/api/api_window.h/cc`  — pass `DeviceIdentity&` to core service
- `fw/src/main.cc`                  — declare `static DeviceIdentity device_identity`;
  call `device_identity.init_or_load(flash, FLASH_SYSTEM_KV_OFS)`; pass to api_win
- `fw/CMakeLists.txt`               — add `device_identity.cc`
- `fw/tests/CMakeLists.txt`         — add `test_device_identity` target

### Checklist

**22.1 DeviceIdentity**
- [ ] `DeviceIdentity::init_or_load(kv)`:
  - attempt to load `"dik.pub"` + `"dik.priv"` keys from KV (encrypted priv)
  - if absent: generate new ed25519 keypair using RP2350 TRNG; store public key as
    `"dik.pub"` (plaintext); store private key as `"dik.priv"` (encrypted with SMK);
    write `"dik.flags"` = `{uint8_t provisioned}` (1 if OTP secret present, 0 if seed-only)
  - if present: load and verify key format
- [ ] `DeviceIdentity::sign(msg, len, sig_out)` — sign 64-byte output using ed25519 with
  loaded private key
- [ ] `DeviceIdentity::public_key(out_32)` — copy public key bytes
- [ ] `DeviceIdentity::is_provisioned()` — return flag read from `"dik.flags"`
- [ ] SMK derivation stub: if `posture.otp_device_secret_present`, derive key via HKDF;
  otherwise use a flash-stored random seed with a "unprovisioned" flag
  (`#ifndef JLPICART_HOST_TEST`: hardware path; host test uses a fixed test key)

**22.2 GET_DEVICE_ID implementation**
- [ ] Remove `API_E_UNSUPPORTED` from `SYS_GET_DEVICE_ID`
- [ ] Parse `scope` byte (0=device, 1=collection-scoped, 2=publisher-scoped)
- [ ] Respond with 16 bytes: HKDF-SHA256(DIK public key, scope label) truncated to 16 bytes
  — this avoids exposing the raw public key unless scope=device and policy permits
- [ ] If `scope=0` (stable device ID) and `!policy.expose_stable_device_id`, return
  `API_E_DENIED`

**22.3 Leaderboard signing wiring (Stage 21 completion)**
- [ ] In `userstats_service.cc`: `UST_LEADER_SUBMIT` — sign the canonical submission payload
  with `device_identity.sign()`; include signature bytes in the submission record stored to KV
  (so offline records are pre-signed and ready for server sync)

**22.4 Main wiring**
- [ ] Declare `static DeviceIdentity device_identity` in `main()`
- [ ] After `kv_store` init: `device_identity.init_or_load(kv_store)`
- [ ] Log `"DIK ready (provisioned)"` or `"DIK ready (seed-only, unprovisioned)"` from flag
- [ ] Pass `device_identity` to `api_win.init(...)` (extend signature)

**22.5 Host tests (`test_device_identity.cc`)**
- [ ] `test_dik_generate`: fresh KV → keygen runs; public key and encrypted private key stored
- [ ] `test_dik_reload`: load a pre-generated key; verify same public key returned after reload
- [ ] `test_dik_idempotent`: call `init_or_load()` twice on same KV → same public key, no new
  keygen
- [ ] `test_dik_sign_verify`: sign a message; verify signature with the public key (ed25519
  verify using the same library)
- [ ] `test_get_device_id_scoped`: scope=1 returns different 16 bytes than scope=0

### Definition of done

- [ ] All host tests pass
- [ ] `GET_DEVICE_ID` returns 16 bytes (not `API_E_UNSUPPORTED`)
- [ ] On hardware: DIK survives power cycle; leaderboard records include a valid signature field
- [ ] `is_provisioned()` correctly reflects OTP secret presence

---

## Stage 23+ — Feature peripherals and services

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
