# External Dependencies

## Overview

External dependencies are managed as git submodules with local patches applied on top.

After cloning the repo, initialize submodules:
```bash
git submodule update --init
bash fw/ext/apply_patches.sh
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
   git -C fw/ext/tinyusb fetch
   git -C fw/ext/tinyusb checkout <new-sha>
   ```
2. Regenerate the patch file (rebase local changes on top of new upstream):
   ```bash
   # Apply existing patch, then re-diff against new HEAD
   git -C fw/ext/tinyusb apply fw/ext/patches/tinyusb/local_changes.patch
   git -C fw/ext/tinyusb diff HEAD > fw/ext/patches/tinyusb/local_changes.patch
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
- Script: https://github.com/espressif/esp-at/blob/498f10ca/tools/at.py

Modify firmware binary for custom UART pins:
```bash
python3 at.py modify_bin --baud 115200 --tx_pin 21 --rx_pin 20 --cts_pin -1 --rts_pin -1 --input factory_MINI-1.bin
```

---

## esp-serial-flasher

- **Source:** https://github.com/espressif/esp-serial-flasher (v1.8.0)
- **Tracking:** Vendored directly in the main repo (no submodule)
- **Local patches:** none — modify files directly and commit to main repo

---

## sdcc

- **Version:** 4.4.0 (pinned in `ext/lock.yml`)
- **Tracking:** Downloaded locally to `ext/sdcc/` — not tracked in git
- **Install:** `bash fw/ext/get_sdcc.sh`

---

## openmsx

- **Version:** RELEASE_21_0 (pinned in `ext/lock.yml`)
- **Source:** `third_party/openMSX` submodule (built locally)
- **Tracking:** Built locally to `ext/openmsx/` — not tracked in git
- **Install:** `bash fw/ext/build_openmsx.sh`
