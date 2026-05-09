#!/usr/bin/env bash
# get_deps.sh — download and install all pinned dependencies into fw/ext/.
#
# Reads version, URL, and SHA256 from fw/ext/lock.yml.
# On subsequent runs: skips already-installed deps (use --force to re-download).
#
# Usage:
#   bash fw/ext/get_deps.sh          # download everything missing
#   bash fw/ext/get_deps.sh --force  # re-download everything
#   bash fw/ext/get_deps.sh pico_sdk # download a single dependency
#
# After first run:
#   fw/ext/src/tinyusb/              — tinyusb source (with local patches)
#   fw/ext/src/esp-at/               — esp-at source (not built)
#   fw/ext/src/openmsx/              — openMSX source (built by default)
#   fw/ext/bin/openmsx/              — openMSX binary
#   fw/ext/tools/pico-sdk/sdk/2.2.0/ — Pico SDK
#   fw/ext/tools/pico-sdk/toolchain/ — ARM GNU toolchain
#   fw/ext/tools/pico-sdk/picotool/  — picotool binary
#   fw/ext/tools/esp-serial-flasher/ — esp-serial-flasher source
#   fw/ext/bin/sdcc/                 — SDCC Z80 compiler
#
# picotool is built from source and requires cmake + a C++ compiler.
# openMSX is built if system deps are present (SDL2, etc.).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOCK="${SCRIPT_DIR}/lock.yml"
FORCE=0
SINGLE=""

for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        *) SINGLE="$arg" ;;
    esac
done

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

lock_get() {
    # lock_get <section> <key> — extract a value from lock.yml
    local section="$1" key="$2"
    awk "/^${section}:/{f=1} f && /^  ${key}:/{print \$2; exit}" "$LOCK" | tr -d '"'
}

download_and_verify() {
    # download_and_verify <name> <url> <sha256> <dest_file>
    local name="$1" url="$2" expected_sha="$3" dest="$4"
    echo "==> Downloading ${name}..."
    curl -fL --progress-bar "$url" -o "$dest"
    local actual
    actual=$(sha256sum "$dest" | awk '{print $1}')
    if [ "$actual" != "$expected_sha" ]; then
        echo "ERROR: SHA256 mismatch for ${name}!" >&2
        echo "  expected: ${expected_sha}" >&2
        echo "  actual:   ${actual}" >&2
        rm -f "$dest"
        exit 1
    fi
    echo "  SHA256 OK"
}

extract_to() {
    # extract_to <tarball> <format> <dest>
    # format: z (gzip), J (xz), j (bz2)
    local tarball="$1" fmt="$2" dest="$3"
    mkdir -p "$dest"
    tar "-x${fmt}f" "$tarball" -C "$dest" --strip-components=1
}

dep_done() {
    # Check if a dependency is already installed (not --force).
    if [ "$FORCE" -eq 0 ]; then
        local check="$1"
        if [ -e "$check" ]; then
            echo "  Already present: $check"
            return 0
        fi
    fi
    return 1
}

# ---------------------------------------------------------------------------
# Pico SDK (requires git submodules for tinyusb etc.)
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "pico_sdk" ]; then exit 0; fi

SDK_VERSION=$(lock_get pico_sdk version)
SDK_URL=$(lock_get pico_sdk url)
SDK_SHA=$(lock_get pico_sdk sha256)
SDK_DEST="${REPO_ROOT}/fw/ext/tools/pico-sdk/sdk/${SDK_VERSION}"

if dep_done "${SDK_DEST}/pico_sdk_init.cmake"; then
    :
else
    TMPFILE=$(mktemp /tmp/pico-sdk-XXXXXX.tar.gz)
    trap 'rm -f "$TMPFILE"' EXIT
    download_and_verify "Pico SDK ${SDK_VERSION}" "$SDK_URL" "$SDK_SHA" "$TMPFILE"
    echo "==> Extracting Pico SDK to ${SDK_DEST}..."
    extract_to "$TMPFILE" z "${SDK_DEST}"
    echo "==> Initializing Pico SDK submodules..."
    git -C "${SDK_DEST}" submodule update --init --depth=1 2>&1 | tail -5
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: Pico SDK at ${SDK_DEST}"
fi

# ---------------------------------------------------------------------------
# ARM toolchain
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "arm_toolchain" ]; then exit 0; fi

TC_VERSION=$(lock_get arm_toolchain version)
TC_URL=$(lock_get arm_toolchain url)
TC_SHA=$(lock_get arm_toolchain sha256)
TC_DEST="${REPO_ROOT}/fw/ext/tools/pico-sdk/toolchain/${TC_VERSION}"

if dep_done "${TC_DEST}/bin/arm-none-eabi-gcc"; then
    :
else
    TMPFILE=$(mktemp /tmp/arm-toolchain-XXXXXX.tar.xz)
    trap 'rm -f "$TMPFILE"' EXIT
    download_and_verify "ARM toolchain ${TC_VERSION}" "$TC_URL" "$TC_SHA" "$TMPFILE"
    echo "==> Extracting ARM toolchain to ${TC_DEST}..."
    extract_to "$TMPFILE" J "${TC_DEST}"
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: ARM toolchain at ${TC_DEST}"
fi

# ---------------------------------------------------------------------------
# picotool (built from source)
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "picotool" ]; then exit 0; fi

PT_VERSION=$(lock_get picotool version)
PT_URL=$(lock_get picotool url)
PT_SHA=$(lock_get picotool sha256)
PT_DEST="${REPO_ROOT}/fw/ext/tools/pico-sdk/picotool/${PT_VERSION}"

if dep_done "${PT_DEST}/picotool"; then
    :
else
    TMPFILE=$(mktemp /tmp/picotool-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/picotool-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "picotool ${PT_VERSION}" "$PT_URL" "$PT_SHA" "$TMPFILE"
    echo "==> Building picotool..."
    extract_to "$TMPFILE" z "${TMPDIR}"
    mkdir -p "${TMPDIR}/build"
    cmake -S "$TMPDIR" -B "${TMPDIR}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_SDK_PATH="${SDK_DEST}" \
        -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
        -Wno-dev -DCMAKE_INSTALL_PREFIX="${PT_DEST}" 2>&1 | tail -5
    make -C "${TMPDIR}/build" -j"$(nproc)" 2>&1 | tail -5
    mkdir -p "${PT_DEST}"
    cp "${TMPDIR}/build/picotool" "${PT_DEST}/picotool"
    trap - EXIT
    rm -f "$TMPFILE"
    rm -rf "$TMPDIR"
    echo "OK: picotool at ${PT_DEST}/picotool"
fi

# ---------------------------------------------------------------------------
# SDCC
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "sdcc" ]; then exit 0; fi

SDCC_VERSION=$(lock_get sdcc version)
SDCC_URL=$(lock_get sdcc url)
SDCC_SHA=$(lock_get sdcc sha256)
SDCC_DEST="${REPO_ROOT}/fw/ext/bin/sdcc"

if dep_done "${SDCC_DEST}/bin/sdcc"; then
    :
else
    TMPFILE=$(mktemp /tmp/sdcc-XXXXXX.tar.bz2)
    TMPDIR=$(mktemp -d /tmp/sdcc-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "SDCC ${SDCC_VERSION}" "$SDCC_URL" "$SDCC_SHA" "$TMPFILE"
    echo "==> Extracting SDCC to ${SDCC_DEST}..."
    tar -xjf "$TMPFILE" -C "$TMPDIR"
    EXTRACTED=$(ls -d "${TMPDIR}"/sdcc-*/ 2>/dev/null | head -1)
    mkdir -p "${SDCC_DEST}"
    cp -a "${EXTRACTED}/." "${SDCC_DEST}/"
    trap - EXIT
    rm -f "$TMPFILE"
    rm -rf "$TMPDIR"
    echo "OK: SDCC at ${SDCC_DEST}"
fi

# ---------------------------------------------------------------------------
# tinyusb (with local patches)
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "tinyusb" ]; then exit 0; fi

TU_VERSION=$(lock_get tinyusb version)
TU_URL=$(lock_get tinyusb url)
TU_SHA=$(lock_get tinyusb sha256)
TU_DEST="${REPO_ROOT}/fw/ext/src/tinyusb"
TU_PATCHES=$(lock_get tinyusb patches)

if dep_done "${TU_DEST}/src/tusb.c"; then
    echo "  (patches may need re-applying if tinyusb was updated)"
else
    TMPFILE=$(mktemp /tmp/tinyusb-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/tinyusb-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "tinyusb ${TU_VERSION}" "$TU_URL" "$TU_SHA" "$TMPFILE"
    echo "==> Extracting tinyusb to ${TU_DEST}..."
    extract_to "$TMPFILE" z "${TMPDIR}"
    rm -rf "${TU_DEST}"
    mv "${TMPDIR}" "${TU_DEST}"
    # Apply local patches
    if [ -n "$TU_PATCHES" ] && [ -d "$TU_PATCHES" ]; then
        echo "==> Applying local patches..."
        for patch in "${TU_PATCHES}"/*.patch; do
            [ -f "$patch" ] || continue
            echo "  ${patch}"
            git -C "${TU_DEST}" apply "$patch"
        done
    fi
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: tinyusb at ${TU_DEST}"
fi

# ---------------------------------------------------------------------------
# esp-at (source only, not built — firmware binary is separate)
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "esp_at" ]; then exit 0; fi

EA_VERSION=$(lock_get esp_at version)
EA_URL=$(lock_get esp_at url)
EA_SHA=$(lock_get esp_at sha256)
EA_DEST="${REPO_ROOT}/fw/ext/src/esp-at"

if dep_done "${EA_DEST}/README.md"; then
    :
else
    TMPFILE=$(mktemp /tmp/esp-at-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/esp-at-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "esp-at ${EA_VERSION}" "$EA_URL" "$EA_SHA" "$TMPFILE"
    echo "==> Extracting esp-at to ${EA_DEST}..."
    extract_to "$TMPFILE" z "${TMPDIR}"
    rm -rf "${EA_DEST}"
    mv "${TMPDIR}" "${EA_DEST}"
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: esp-at at ${EA_DEST}"
fi

# ---------------------------------------------------------------------------
# openMSX (download + build)
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "openmsx" ]; then exit 0; fi

OM_VERSION=$(lock_get openmsx version)
OM_URL=$(lock_get openmsx url)
OM_SHA=$(lock_get openmsx sha256)
OM_SRC="${REPO_ROOT}/fw/ext/src/openmsx"
OM_DEST="${REPO_ROOT}/fw/ext/bin/openmsx"
OM_BIN="${OM_DEST}/bin/openmsx"

# Download source if missing.
if dep_done "${OM_SRC}/GNUmakefile"; then
    :
else
    TMPFILE=$(mktemp /tmp/openmsx-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/openmsx-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "openMSX ${OM_VERSION}" "$OM_URL" "$OM_SHA" "$TMPFILE"
    echo "==> Extracting openMSX to ${OM_SRC}..."
    extract_to "$TMPFILE" z "${TMPDIR}"
    rm -rf "${OM_SRC}"
    mv "${TMPDIR}" "${OM_SRC}"
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: openMSX source at ${OM_SRC}"
fi

# Build if binary missing.
if dep_done "${OM_BIN}"; then
    :
else
    # Check system prerequisites.
    MISSING=()
    for hdr in SDL2/SDL.h SDL2/SDL_ttf.h png.h; do
        if ! find /usr/include /usr/local/include -name "$(basename "$hdr")" 2>/dev/null | grep -q .; then
            MISSING+=("$hdr")
        fi
    done
    if [[ ${#MISSING[@]} -gt 0 ]]; then
        echo "  openMSX build deps missing: ${MISSING[*]}"
        echo "  Install: sudo apt-get install libsdl2-dev libsdl2-ttf-dev libpng-dev \\"
        echo "    libogg-dev libvorbis-dev libtcl-dev libao-dev zlib1g-dev \\"
        echo "    libfreetype6-dev python3 g++ make"
    else
        echo "==> Building openMSX ${OM_VERSION}..."
        # GLEW bootstrap (no sudo required).
        GLEW_TMPDIR=""
        if ! find /usr/include /usr/local/include -name "glew.h" 2>/dev/null | grep -q .; then
            GLEW_TMPDIR="${OM_DEST}/glew-bootstrap"
            if [[ ! -f "$GLEW_TMPDIR/usr/include/GL/glew.h" ]]; then
                echo "  libglew-dev not found — downloading to $GLEW_TMPDIR (no sudo needed)..."
                mkdir -p "$GLEW_TMPDIR"
                (cd "$GLEW_TMPDIR" && apt-get download libglew-dev libglew2.2 libglu1-mesa-dev 2>&1 | grep -v "^$")
                for deb in "$GLEW_TMPDIR"/*.deb; do
                    dpkg -x "$deb" "$GLEW_TMPDIR"
                done
            fi
            export CPATH="$GLEW_TMPDIR/usr/include${CPATH:+:$CPATH}"
            PROBE_OUT="${OM_SRC}/derived/x86_64-linux-opt/config"
            PROBE_MK="${PROBE_OUT}/probed_defs.mk"
            if [[ ! -f "$PROBE_MK" ]]; then
                echo "  Running openMSX probe with bootstrapped GLEW paths..."
                mkdir -p "$PROBE_OUT"
                (
                    cd "$OM_SRC"
                    LIBRARY_PATH="$GLEW_TMPDIR/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}" \
                    python3 build/probe.py "g++ -m64" "$PROBE_OUT" linux SYS_DYN "" 2>&1
                    touch "$PROBE_MK"
                )
            fi
        fi
        NCPU=$(nproc 2>/dev/null || echo 4)
        if [[ -n "$GLEW_TMPDIR" ]]; then
            make -C "$OM_SRC" -j"$NCPU" LDFLAGS="-L$GLEW_TMPDIR/usr/lib/x86_64-linux-gnu"
        else
            make -C "$OM_SRC" -j"$NCPU"
        fi
        # Locate and install binary.
        BUILT_BIN=""
        if [[ -x "$OM_SRC/derived/openmsx" ]]; then
            BUILT_BIN="$OM_SRC/derived/openmsx"
        else
            BUILT_BIN="$(find "$OM_SRC/derived" -maxdepth 3 -name "openmsx" -type f 2>/dev/null | head -1)"
        fi
        if [[ -n "$BUILT_BIN" ]] && [[ -x "$BUILT_BIN" ]]; then
            mkdir -p "${OM_DEST}/bin"
            cp -f "$BUILT_BIN" "$OM_BIN"
            chmod +x "$OM_BIN"
            echo "OK: openMSX binary at ${OM_BIN}"
        else
            echo "  openMSX build succeeded but binary not found" >&2
        fi
    fi
fi

# ---------------------------------------------------------------------------
# esp-serial-flasher
# ---------------------------------------------------------------------------
if [ -n "$SINGLE" ] && [ "$SINGLE" != "esp_serial_flasher" ]; then exit 0; fi

EF_VERSION=$(lock_get esp_serial_flasher version)
EF_URL=$(lock_get esp_serial_flasher url)
EF_SHA=$(lock_get esp_serial_flasher sha256)
EF_DEST="${REPO_ROOT}/fw/ext/tools/esp-serial-flasher"

if dep_done "${EF_DEST}/README.md"; then
    :
else
    TMPFILE=$(mktemp /tmp/esp-serial-flasher-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/esp-serial-flasher-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "esp-serial-flasher ${EF_VERSION}" "$EF_URL" "$EF_SHA" "$TMPFILE"
    echo "==> Extracting esp-serial-flasher to ${EF_DEST}..."
    extract_to "$TMPFILE" z "${TMPDIR}"
    rm -rf "${EF_DEST}"
    mv "${TMPDIR}" "${EF_DEST}"
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: esp-serial-flasher at ${EF_DEST}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "==> All dependencies installed."
