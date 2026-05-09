#!/usr/bin/env bash
# get_deps.sh — download and install all pinned dependencies into fw/ext/.
#
# Reads version, URL, and SHA256 from fw/util/lock.yml.
# On subsequent runs: skips already-installed deps (use --force to re-download).
#
# Usage:
#   bash fw/util/get_deps.sh          # download everything missing
#   bash fw/util/get_deps.sh --force  # re-download everything
#   bash fw/util/get_deps.sh pico_sdk # download a single dependency
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
    # extract_to <tarball> <format> <dest> — extract directly into dest
    # so files inherit parent directory group and respect umask.
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
# Dependency functions
# ---------------------------------------------------------------------------

install_pico_sdk() {
    local version url sha dest
    version=$(lock_get pico_sdk version)
    url=$(lock_get pico_sdk url)
    sha=$(lock_get pico_sdk sha256)
    dest="${REPO_ROOT}/fw/ext/tools/pico-sdk/sdk/${version}"

    if dep_done "${dest}/pico_sdk_init.cmake"; then
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/pico-sdk-XXXXXX.tar.gz)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "Pico SDK ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting Pico SDK to ${dest}..."
    extract_to "$tmpfile" z "$dest"
    echo "==> Initializing Pico SDK submodules..."
    git -C "$dest" submodule update --init --depth=1 2>&1 | tail -5
    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: Pico SDK at ${dest}"
}

install_arm_toolchain() {
    local version url sha dest
    version=$(lock_get arm_toolchain version)
    url=$(lock_get arm_toolchain url)
    sha=$(lock_get arm_toolchain sha256)
    dest="${REPO_ROOT}/fw/ext/tools/pico-sdk/toolchain/${version}"

    if dep_done "${dest}/bin/arm-none-eabi-gcc"; then
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/arm-toolchain-XXXXXX.tar.xz)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "ARM toolchain ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting ARM toolchain to ${dest}..."
    extract_to "$tmpfile" J "$dest"
    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: ARM toolchain at ${dest}"
}

install_picotool() {
    local version url sha dest
    version=$(lock_get picotool version)
    url=$(lock_get picotool url)
    sha=$(lock_get picotool sha256)
    dest="${REPO_ROOT}/fw/ext/tools/pico-sdk/picotool/${version}"
    local sdk_dest="${REPO_ROOT}/fw/ext/tools/pico-sdk/sdk/$(lock_get pico_sdk version)"

    if dep_done "${dest}/picotool"; then
        return
    fi

    local tmpfile tmpdir
    tmpfile=$(mktemp /tmp/picotool-XXXXXX.tar.gz)
    tmpdir=$(mktemp -d /tmp/picotool-src-XXXXXX)
    trap 'rm -f "$tmpfile"; rm -rf "$tmpdir"' EXIT
    download_and_verify "picotool ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Building picotool..."
    extract_to "$tmpfile" z "$tmpdir"
    mkdir -p "${tmpdir}/build"
    cmake -S "$tmpdir" -B "${tmpdir}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_SDK_PATH="$sdk_dest" \
        -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
        -Wno-dev -DCMAKE_INSTALL_PREFIX="$dest" 2>&1 | tail -5
    make -C "${tmpdir}/build" -j"$(nproc)" 2>&1 | tail -5
    mkdir -p "$dest"
    cp "${tmpdir}/build/picotool" "${dest}/picotool"
    trap - EXIT
    rm -f "$tmpfile"
    rm -rf "$tmpdir"
    echo "OK: picotool at ${dest}/picotool"
}

install_sdcc() {
    local version url sha dest
    version=$(lock_get sdcc version)
    url=$(lock_get sdcc url)
    sha=$(lock_get sdcc sha256)
    dest="${REPO_ROOT}/fw/ext/bin/sdcc"

    if dep_done "${dest}/bin/sdcc"; then
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/sdcc-XXXXXX.tar.bz2)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "SDCC ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting SDCC to ${dest}..."
    extract_to "$tmpfile" j "$dest"
    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: SDCC at ${dest}"
}

install_tinyusb() {
    local version url sha dest patches
    version=$(lock_get tinyusb version)
    url=$(lock_get tinyusb url)
    sha=$(lock_get tinyusb sha256)
    dest="${REPO_ROOT}/fw/ext/src/tinyusb"
    patches="$(lock_get tinyusb patches)"

    if dep_done "${dest}/src/tusb.c"; then
        echo "  (patches may need re-applying if tinyusb was updated)"
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/tinyusb-XXXXXX.tar.gz)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "tinyusb ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting tinyusb to ${dest}..."
    rm -rf "$dest"
    extract_to "$tmpfile" z "$dest"

    if [ -n "$patches" ] && [ -d "${REPO_ROOT}/${patches}" ]; then
        echo "==> Applying local patches..."
        for patch in "${REPO_ROOT}/${patches}"/*.patch; do
            [ -f "$patch" ] || continue
            echo "  ${patch}"
            git -C "$dest" apply --recount --ignore-space-change "$patch"
        done
    fi

    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: tinyusb at ${dest}"
}

install_esp_at() {
    local version url sha dest
    version=$(lock_get esp_at version)
    url=$(lock_get esp_at url)
    sha=$(lock_get esp_at sha256)
    dest="${REPO_ROOT}/fw/ext/src/esp-at"

    if dep_done "${dest}/README.md"; then
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/esp-at-XXXXXX.tar.gz)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "esp-at ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting esp-at to ${dest}..."
    rm -rf "$dest"
    extract_to "$tmpfile" z "$dest"
    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: esp-at at ${dest}"
}

install_openmsx() {
    local version url sha src dest bin
    version=$(lock_get openmsx version)
    url=$(lock_get openmsx url)
    sha=$(lock_get openmsx sha256)
    src="${REPO_ROOT}/fw/ext/src/openmsx"
    dest="${REPO_ROOT}/fw/ext/bin/openmsx"
    bin="${dest}/bin/openmsx"

    if ! dep_done "${src}/GNUmakefile"; then
        local tmpfile
        tmpfile=$(mktemp /tmp/openmsx-XXXXXX.tar.gz)
        trap 'rm -f "$tmpfile"' EXIT
        download_and_verify "openMSX ${version}" "$url" "$sha" "$tmpfile"
        echo "==> Extracting openMSX to ${src}..."
        rm -rf "$src"
        extract_to "$tmpfile" z "$src"
        trap - EXIT
        rm -f "$tmpfile"
        echo "OK: openMSX source at ${src}"
    fi

    if dep_done "$bin"; then
        return
    fi

    local missing=()
    for hdr in SDL2/SDL.h SDL2/SDL_ttf.h png.h; do
        if ! find /usr/include /usr/local/include -name "$(basename "$hdr")" 2>/dev/null | grep -q .; then
            missing+=("$hdr")
        fi
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "  openMSX build deps missing: ${missing[*]}"
        echo "  Install: sudo apt-get install libsdl2-dev libsdl2-ttf-dev libpng-dev \\"
        echo "    libogg-dev libvorbis-dev libtcl-dev libao-dev zlib1g-dev \\"
        echo "    libfreetype6-dev python3 g++ make"
        return
    fi

    echo "==> Building openMSX ${version}..."
    local glew_tmpdir=""
    if ! find /usr/include /usr/local/include -name "glew.h" 2>/dev/null | grep -q .; then
        glew_tmpdir="${dest}/glew-bootstrap"
        if [[ ! -f "$glew_tmpdir/usr/include/GL/glew.h" ]]; then
            echo "  libglew-dev not found — downloading to $glew_tmpdir (no sudo needed)..."
            mkdir -p "$glew_tmpdir"
            (cd "$glew_tmpdir" && apt-get download libglew-dev libglew2.2 libglu1-mesa-dev 2>&1 | grep -v "^$")
            for deb in "$glew_tmpdir"/*.deb; do
                dpkg -x "$deb" "$glew_tmpdir"
            done
        fi
        export CPATH="$glew_tmpdir/usr/include${CPATH:+:$CPATH}"
        local probe_out="${src}/derived/x86_64-linux-opt/config"
        local probe_mk="${probe_out}/probed_defs.mk"
        if [[ ! -f "$probe_mk" ]]; then
            echo "  Running openMSX probe with bootstrapped GLEW paths..."
            mkdir -p "$probe_out"
            (
                cd "$src"
                LIBRARY_PATH="$glew_tmpdir/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}" \
                python3 build/probe.py "g++ -m64" "$probe_out" linux SYS_DYN "" 2>&1
                touch "$probe_mk"
            )
        fi
    fi

    local ncpu
    ncpu=$(nproc 2>/dev/null || echo 4)
    if [[ -n "$glew_tmpdir" ]]; then
        make -C "$src" -j"$ncpu" LDFLAGS="-L$glew_tmpdir/usr/lib/x86_64-linux-gnu"
    else
        make -C "$src" -j"$ncpu"
    fi

    local built_bin=""
    if [[ -x "$src/derived/openmsx" ]]; then
        built_bin="$src/derived/openmsx"
    else
        built_bin="$(find "$src/derived" -maxdepth 3 -name "openmsx" -type f 2>/dev/null | head -1)"
    fi
    if [[ -n "$built_bin" ]] && [[ -x "$built_bin" ]]; then
        mkdir -p "${dest}/bin"
        cp -f "$built_bin" "$bin"
        chmod +x "$bin"
        echo "OK: openMSX binary at ${bin}"
    else
        echo "  openMSX build succeeded but binary not found" >&2
    fi
}

install_esp_serial_flasher() {
    local version url sha dest
    version=$(lock_get esp_serial_flasher version)
    url=$(lock_get esp_serial_flasher url)
    sha=$(lock_get esp_serial_flasher sha256)
    dest="${REPO_ROOT}/fw/ext/tools/esp-serial-flasher"

    if dep_done "${dest}/README.md"; then
        return
    fi

    local tmpfile
    tmpfile=$(mktemp /tmp/esp-serial-flasher-XXXXXX.tar.gz)
    trap 'rm -f "$tmpfile"' EXIT
    download_and_verify "esp-serial-flasher ${version}" "$url" "$sha" "$tmpfile"
    echo "==> Extracting esp-serial-flasher to ${dest}..."
    rm -rf "$dest"
    extract_to "$tmpfile" z "$dest"
    trap - EXIT
    rm -f "$tmpfile"
    echo "OK: esp-serial-flasher at ${dest}"
}

# ---------------------------------------------------------------------------
# Dispatcher
# ---------------------------------------------------------------------------

DEPS=(pico_sdk arm_toolchain picotool sdcc tinyusb esp_at openmsx esp_serial_flasher)

if [ -n "$SINGLE" ]; then
    install_"${SINGLE}"
else
    for dep in "${DEPS[@]}"; do
        install_"${dep}"
    done
fi

echo ""
echo "==> All dependencies installed."
