# Toolchain and build instructions

## Prerequisites

The firmware is self-contained and airgapped — no network access is required to
build. All dependencies live under `fw/ext/`. Ensure these
directories are populated before building (they are typically checked in or
populated by the release process).

If `fw/ext/tools/pico-sdk/` is missing, the CMake configuration will fail with:
`Pico SDK not found at '...' — populate ext/tools/pico-sdk/ first`.

## Building the firmware

```sh
mkdir fw/build && cd fw/build
PICO_TOOLCHAIN_PATH=../ext/tools/pico-sdk/toolchain/14_2_Rel1/bin \
    cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
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

## Building host tests

```sh
mkdir fw/tests/build && cd fw/tests/build
cmake ..
make -j$(nproc)
ctest --output-on-failure
```

## Pinned versions

| Component     | Version     |
|---------------|-------------|
| Pico SDK      | 2.2.0       |
| ARM toolchain | 14_2_Rel1   |
| picotool      | 2.2.0       |
| SDCC          | 4.5.0       |
| openMSX       | RELEASE_21_0 |

All version hashes are in `ext/lock.yml`.
