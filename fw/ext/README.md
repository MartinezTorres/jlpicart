# External Dependencies

## Layout

```
fw/ext/
├── src/              — source trees (git submodules)
│   ├── openmsx/
│   ├── tinyusb/
│   └── esp-at/
├── bin/              — downloaded/built binaries (gitignored)
│   ├── openmsx/
│   └── sdcc/
├── tools/            — toolchains and SDKs (gitignored)
│   └── pico-sdk/
├── patches/          — local patches applied to submodules
│   └── tinyusb/
├── esp32/            — vendored ESP32 tooling
│   ├── esp-serial-flasher/
│   └── at.py
├── lock.yml          — pinned versions, URLs, SHA256 digests
├── fetch_pico_sdk.sh — Pico SDK + ARM toolchain + picotool → tools/pico-sdk/
├── get_sdcc.sh       — SDCC Z80 compiler → bin/sdcc/
├── build_openmsx.sh  — openMSX emulator → bin/openmsx/
└── apply_patches.sh  — apply local patches to submodules
```

## Setup after clone

```bash
git submodule update --init
bash fw/ext/apply_patches.sh
bash fw/ext/fetch_pico_sdk.sh
bash fw/ext/get_sdcc.sh
bash fw/ext/build_openmsx.sh   # optional, for integration tests
```

---

## tinyusb

- **Source:** https://github.com/hathach/tinyusb
- **Pinned commit:** `4232642899362fa5e9cf0dc59bad6f1f6d32c563`
- **Local patches:** `patches/tinyusb/local_changes.patch`
- **Origin:** Modified from the copy bundled with pico-sdk

### Updating tinyusb upstream

1. Update the submodule to the new commit:
   ```bash
   git -C fw/ext/src/tinyusb fetch
   git -C fw/ext/src/tinyusb checkout <new-sha>
   ```
2. Regenerate the patch file (rebase local changes on top of new upstream):
   ```bash
   git -C fw/ext/src/tinyusb apply fw/ext/patches/tinyusb/local_changes.patch
   git -C fw/ext/src/tinyusb diff HEAD > fw/ext/patches/tinyusb/local_changes.patch
   ```
3. Update the pinned SHA in `.gitmodules` and commit everything.

---

## esp-at

- **Source:** https://github.com/espressif/esp-at
- **Pinned commit:** `498f10caf9f4e61ba30bd9cc637c0401125ef681` (release/v3.3.0.0)
- **Local patches:** none
- **Firmware binary:** ESP32C3-AT v3.3.0.0 — https://github.com/espressif/esp-at/releases/tag/v3.3.0.0#ESP32C3-AT

### AT.PY tool

- Docs: https://docs.espressif.com/projects/esp-at/en/release-v3.3.0.0/esp32c3/Compile_and_Develop/tools_at_py.html
- Script: `fw/ext/esp32/at.py`

Modify firmware binary for custom UART pins:
```bash
python3 fw/ext/esp32/at.py modify_bin --baud 115200 --tx_pin 21 --rx_pin 20 --cts_pin -1 --rts_pin -1 --input factory_MINI-1.bin
```

---

## esp-serial-flasher

- **Source:** https://github.com/espressif/esp-serial-flasher (v1.8.0)
- **Tracking:** Vendored directly in the main repo (no submodule)
- **Local patches:** none — modify files directly and commit to main repo

---

## sdcc

- **Version:** 4.5.0 (pinned in `lock.yml`)
- **Tracking:** Downloaded to `bin/sdcc/` — not tracked in git
- **Install:** `bash fw/ext/get_sdcc.sh`

---

## openmsx

- **Version:** RELEASE_21_0 (pinned in `lock.yml`)
- **Source:** `src/openmsx` submodule (built locally)
- **Tracking:** Built to `bin/openmsx/` — not tracked in git
- **Install:** `bash fw/ext/build_openmsx.sh`
