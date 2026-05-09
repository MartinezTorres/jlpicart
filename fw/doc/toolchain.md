# Toolchain and build instructions

## Prerequisites

The firmware is self-contained and airgapped — no network access is required to
build. All dependency bootstrap files live under `fw/util/`. Populate them with:

```sh
bash fw/util/get_deps.sh
```

This downloads everything into `fw/ext/` (gitignored, can be wiped at any time).

If `fw/ext/tools/pico-sdk/` is missing, the CMake configuration will fail with:
`Pico SDK not found at '...' — populate ext/tools/pico-sdk/ first`.

## Building the firmware

```sh
cd fw
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Output artifacts: `fw/build/jlpicart.elf` and `fw/build/jlpicart.uf2`.

## Flashing

Put the RP2350 into BOOTSEL mode (hold BOOTSEL, press RESET) then copy
the UF2:

```sh
cp fw/build/jlpicart.uf2 /media/$USER/RP2350/
```

Or use picotool:

```sh
fw/ext/tools/pico-sdk/picotool/2.2.0/picotool load fw/build/jlpicart.uf2 --force
```

## Building and running host tests

```sh
cd fw
cmake --build build --target tests_run
```

This configures the native test project under `build/tests/`, builds all
test executables, and runs them via ctest.

## Z80 ROM targets

Build the Z80 menu stub, standalone menu, and API client ROMs (SDCC required):

```sh
cmake --build build --target build_menu_stub   # build/z80/stub/
make -C src/msx/menu/standalone BUILD_DIR=build/z80/standalone
make -C src/msx/api/client BUILD_DIR=build/z80/api_client
```

## Clean

All build artifacts and downloaded dependencies can be wiped:

```sh
rm -rf fw/build fw/ext
```

Then restore with `bash fw/util/get_deps.sh` and rebuild.

## Pinned versions

| Component          | Version  |
|--------------------|----------|
| Pico SDK           | 2.2.0    |
| ARM toolchain      | 14_2_Rel1|
| picotool           | 2.2.0    |
| SDCC               | 4.5.0    |
| tinyusb            | 0.16.0-942|
| esp-at             | v3.3.0.0 |
| openMSX            | RELEASE_21_0|
| esp-serial-flasher | v1.8.0   |

All version hashes are in `fw/util/lock.yml`.
