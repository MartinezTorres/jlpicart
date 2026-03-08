# tools/ — pinned external tool management

All tools used by the build and CI are pinned to exact versions and must
be fetched explicitly. System-installed versions of `sdcc`, `openmsx`, or
`arm-none-eabi-gcc` are **never used** — CI and local builds always invoke
tools from paths under this directory or `fw/.pico-sdk/`.

## Pinned versions

See `tools/lock.yml` for all versions, download URLs, and SHA256 digests.

## Pico SDK + ARM toolchain + picotool

```sh
bash tools/fetch_pico_sdk.sh
```

Populates `fw/.pico-sdk/` with:
- `fw/.pico-sdk/sdk/2.2.0/` — Raspberry Pi Pico SDK
- `fw/.pico-sdk/toolchain/14_2_Rel1/` — ARM GNU toolchain (arm-none-eabi-gcc)
- `fw/.pico-sdk/picotool/2.2.0/` — picotool

This is the same layout the VS Code Pico extension creates. `fw/.pico-sdk/`
is gitignored; run this script on every fresh clone.

## SDCC (Z80 compiler)

```sh
bash tools/fetch_sdcc.sh
```

Populates `tools/sdcc/` with the SDCC 4.5.0 Linux amd64 toolchain.
Used from Stage 4 onwards for Z80 code. Always invoke as
`tools/sdcc/bin/sdcc` — never use `sdcc` from PATH.

## openMSX (MSX emulator for testing)

```sh
bash tools/build_openmsx.sh
```

Builds openMSX from `third_party/openMSX/` (submodule pinned to
RELEASE_21_0) and installs the binary to `tools/openmsx/bin/openmsx`.
This takes several minutes. Requires: SDL2, SDL2_image, SDL2_ttf, Tcl,
libpng, libogg, libvorbis, libtheora, zlib, and a C++17 compiler.

On Ubuntu/Debian, install build dependencies with:
```sh
sudo apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
    tcl-dev libpng-dev libogg-dev libvorbis-dev libtheora-dev zlib1g-dev
```

Always invoke as `tools/openmsx/bin/openmsx` — never use `openmsx`
from PATH.
