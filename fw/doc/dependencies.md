# External Dependencies

## Layout

```
fw/util/
├── get_deps.sh   — download and install all dependencies
├── lock.yml      — pinned versions, URLs, SHA256 digests
├── patches/      — local patches applied to downloaded sources
│   └── tinyusb/
└── tools/        — utility scripts
    └── at.py     — ESP-AT firmware modifier

fw/ext/            (gitignored, nuke freely)
├── src/           — downloaded source trees
│   ├── tinyusb/
│   ├── esp-at/
│   └── openmsx/
├── bin/           — downloaded/built binaries
│   ├── sdcc/
│   └── openmsx/   (built from src/openmsx/)
└── tools/         — toolchains, SDKs, and tools
    ├── pico-sdk/  (fetched by get_deps.sh)
    └── esp-serial-flasher/
```

## Setup after clone

```bash
bash fw/util/get_deps.sh          # download and build everything
```

---

## tinyusb

- **Source:** https://github.com/hathach/tinyusb
- **Pinned commit:** `4232642899362fa5e9cf0dc59bad6f1f6d32c563`
- **Local patches:** `patches/tinyusb/local_changes.patch`
- **Origin:** Modified from the copy bundled with pico-sdk

### Updating tinyusb upstream

1. Update the pinned commit in `lock.yml` (url + sha256).
2. Regenerate the patch:
   ```bash
   bash fw/util/get_deps.sh tinyusb --force
   git -C fw/ext/src/tinyusb diff HEAD > fw/util/patches/tinyusb/local_changes.patch
   ```
3. Commit `lock.yml` and the updated patch.

---

## esp-at

- **Source:** https://github.com/espressif/esp-at
- **Pinned commit:** `498f10caf9f4e61ba30bd9cc637c0401125ef681` (release/v3.3.0.0)
- **Firmware binary:** ESP32C3-AT v3.3.0.0 — https://github.com/espressif/esp-at/releases/tag/v3.3.0.0#ESP32C3-AT

### AT.PY tool

- Docs: https://docs.espressif.com/projects/esp-at/en/release-v3.3.0.0/esp32c3/Compile_and_Develop/tools_at_py.html
- Script: `fw/util/tools/at.py`

Modify firmware binary for custom UART pins:
```bash
python3 fw/util/tools/at.py modify_bin --baud 115200 --tx-pin 21 --rx-pin 20 --cts-pin -1 --rts-pin -1 --input factory_MINI-1.bin
```

---

## esp-serial-flasher

- **Source:** https://github.com/espressif/esp-serial-flasher
- **Version:** v1.8.0
- **Not yet integrated.** When needed, add:
  ```cmake
  add_subdirectory(ext/tools/esp-serial-flasher
      ${CMAKE_BINARY_DIR}/esp32_flasher)
  ```

---

## sdcc

- **Version:** 4.5.0 (pinned in `lock.yml`)
- **Dest:** `ext/bin/sdcc/` — not tracked in git

---

## openmsx

- **Version:** RELEASE_21_0 (pinned in `lock.yml`)
- **Source:** downloaded to `ext/src/openmsx/`
- **Build:** done by `get_deps.sh` if system deps are present (SDL2, etc.)
