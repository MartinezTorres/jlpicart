# Toolchain and build instructions

## Prerequisites

Run once per fresh clone to populate the SDK and toolchain:

```sh
bash tools/fetch_pico_sdk.sh
```

This creates `fw/.pico-sdk/` with the pinned SDK, ARM toolchain, and
picotool. See `tools/README.md` for details.

## Building the firmware

```sh
mkdir fw/build && cd fw/build
PICO_TOOLCHAIN_PATH=../../fw/.pico-sdk/toolchain/14_2_Rel1/bin \
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
fw/.pico-sdk/picotool/2.2.0/picotool load fw/build/jlpicart.uf2 --force
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
