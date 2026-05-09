# External Dependencies

## Layout

```
fw/ext/
├── src/              — downloaded source trees
│   ├── tinyusb/
│   ├── esp-at/
│   └── openmsx/
├── bin/              — downloaded/built binaries (gitignored)
│   ├── sdcc/
│   └── openmsx/      (built from src/openmsx/)
├── tools/            — toolchains, SDKs, and tools
│   ├── pico-sdk/     (gitignored, fetched by get_deps.sh)
│   └── esp-serial-flasher/
├── patches/          — local patches applied to downloaded sources
│   └── tinyusb/
├── lock.yml          — pinned versions, URLs, SHA256 digests
├── get_deps.sh       — download all dependencies → fw/ext/
└── build_openmsx.sh  — build openMSX from src/openmsx/ → bin/openmsx/
```

## Setup after clone

```bash
bash fw/ext/get_deps.sh          # download everything
bash fw/ext/build_openmsx.sh     # optional, for integration tests
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
   bash fw/ext/get_deps.sh tinyusb --force
   git -C fw/ext/src/tinyusb diff HEAD > fw/ext/patches/tinyusb/local_changes.patch
   ```
3. Commit `lock.yml` and the updated patch.

---

## esp-at

- **Source:** https://github.com/espressif/esp-at
- **Pinned commit:** `498f10caf9f4e61ba30bd9cc637c0401125ef681` (release/v3.3.0.0)
- **Firmware binary:** ESP32C3-AT v3.3.0.0 — https://github.com/espressif/esp-at/releases/tag/v3.3.0.0#ESP32C3-AT

### AT.PY tool

- Docs: https://docs.espressif.com/projects/esp-at/en/release-v3.3.0.0/esp32c3/Compile_and_Develop/tools_at_py.html
- Script: `fw/ext/tools/at.py`

Modify firmware binary for custom UART pins:
```bash
python3 fw/ext/tools/at.py modify_bin --baud 115200 --tx-pin 21 --rx-pin 20 --cts-pin -1 --rts-pin -1 --input factory_MINI-1.bin
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
- **Dest:** `bin/sdcc/` — not tracked in git

---

## openmsx

- **Version:** RELEASE_21_0 (pinned in `lock.yml`)
- **Source:** downloaded to `src/openmsx/`
- **Build:** `bash fw/ext/build_openmsx.sh` → `bin/openmsx/`
