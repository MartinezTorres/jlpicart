#!/usr/bin/env bash
# fw/tools/build_openmsx.sh — build openMSX from the pinned third_party submodule.
#
# Invoke from the fw/ root or repo root:
#   bash fw/tools/build_openmsx.sh
#
# Installs the binary at:
#   fw/tools/openmsx/bin/openmsx
#
# System prerequisites (install with apt before running):
#   libsdl2-dev libsdl2-ttf-dev libpng-dev libogg-dev libvorbis-dev
#   libtcl-dev libao-dev zlib1g-dev libfreetype6-dev python3 g++ make
#
# libsdl2-ttf-dev is required for the emulation core (OSD text rendering).
# libglew-dev headers are required to compile (RELEASE_21_0 includes GL/glew.h
# unconditionally).  If libglew-dev is not installed, this script downloads and
# extracts it without sudo (apt-get download + dpkg -x) and exposes the headers
# via CPATH / LIBRARY_PATH so the openMSX probe and compiler find them.
#
# Options:
#   --force   re-build even if binary already exists

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
FW_ROOT="$REPO_ROOT/fw"
OPENMSX_SRC="$REPO_ROOT/third_party/openMSX"
OPENMSX_DEST="$FW_ROOT/tools/openmsx"
OPENMSX_BIN="$OPENMSX_DEST/bin/openmsx"

FORCE=0
for arg in "$@"; do
    case "$arg" in
        --force) FORCE=1 ;;
        *) echo "Unknown argument: $arg" >&2; exit 1 ;;
    esac
done

# --- Guard: already built ---
if [[ -x "$OPENMSX_BIN" ]] && [[ "$FORCE" -eq 0 ]]; then
    echo "build_openmsx.sh: already built at $OPENMSX_BIN"
    echo "  Run with --force to rebuild."
    exit 0
fi

# --- Guard: submodule initialised ---
if [[ ! -f "$OPENMSX_SRC/GNUmakefile" ]]; then
    echo "build_openmsx.sh: third_party/openMSX not populated." >&2
    echo "  Run: git submodule update --init third_party/openMSX" >&2
    exit 1
fi

# --- Guard: required build dependencies ---
MISSING=()
for hdr in SDL2/SDL.h SDL2/SDL_ttf.h png.h; do
    if ! find /usr/include /usr/local/include -name "$(basename "$hdr")" 2>/dev/null | grep -q .; then
        MISSING+=("$hdr")
    fi
done
if [[ ${#MISSING[@]} -gt 0 ]]; then
    echo "build_openmsx.sh: missing required headers: ${MISSING[*]}" >&2
    echo "  sudo apt-get install libsdl2-dev libsdl2-ttf-dev libpng-dev \\" >&2
    echo "    libogg-dev libvorbis-dev libtcl-dev libao-dev zlib1g-dev \\" >&2
    echo "    libfreetype6-dev python3 g++ make" >&2
    exit 1
fi

echo "=== Building openMSX RELEASE_21_0 ==="
echo "    Source : $OPENMSX_SRC"
echo "    Install: $OPENMSX_BIN"
echo ""

# --- GLEW bootstrap (no sudo required) ---
# RELEASE_21_0 includes GL/glew.h unconditionally in several source files.
# If libglew-dev (headers + linker stub) is not installed, download and
# extract the deb without sudo, then run the openMSX probe manually (before
# make starts) with CPATH and LIBRARY_PATH pointing at the bootstrap tree.
#
# Why manually: main.mk line 226 sets LIBRARY_PATH:=$(BUILD_PATH)/lib which
# overrides our env var for all recipe subprocesses launched by make.  Running
# probe.py directly (before make) avoids that clobber.  Once probed_defs.mk
# exists, make skips the probe step and proceeds to compilation, which uses
# CPATH (set below) to find GL/glew.h.  The libGLEW runtime (libGLEW.so.2.2)
# is typically already installed as a system transitive dependency.
if ! find /usr/include /usr/local/include -name "glew.h" 2>/dev/null | grep -q .; then
    GLEW_TMPDIR="$OPENMSX_DEST/glew-bootstrap"
    if [[ ! -f "$GLEW_TMPDIR/usr/include/GL/glew.h" ]]; then
        echo "  libglew-dev not found — downloading to $GLEW_TMPDIR (no sudo needed)..."
        mkdir -p "$GLEW_TMPDIR"
        (cd "$GLEW_TMPDIR" && apt-get download libglew-dev libglew2.2 libglu1-mesa-dev 2>&1 | grep -v "^$")
        for deb in "$GLEW_TMPDIR"/*.deb; do
            dpkg -x "$deb" "$GLEW_TMPDIR"
        done
    fi
    echo "  Using bootstrapped GLEW from $GLEW_TMPDIR"

    # CPATH makes gcc/g++ find GL/glew.h during compilation.
    export CPATH="$GLEW_TMPDIR/usr/include${CPATH:+:$CPATH}"

    # Run the openMSX probe manually so LIBRARY_PATH reaches it before
    # main.mk can reassign that variable.  Probe args mirror main.mk:
    #   "$(CXX) $(TARGET_FLAGS)"  outDir  OS  LINK_MODE  3rdPartyInstallDir
    PROBE_OUT="$OPENMSX_SRC/derived/x86_64-linux-opt/config"
    PROBE_MK="$PROBE_OUT/probed_defs.mk"
    if [[ ! -f "$PROBE_MK" ]]; then
        echo "  Running openMSX probe with bootstrapped GLEW paths..."
        mkdir -p "$PROBE_OUT"
        (
            cd "$OPENMSX_SRC"
            LIBRARY_PATH="$GLEW_TMPDIR/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}" \
            python3 build/probe.py "g++ -m64" "$PROBE_OUT" linux SYS_DYN "" 2>&1
            touch "$PROBE_MK"
        )
    fi
fi

# Build in the submodule (parallel, using all cores).
NCPU=$(nproc 2>/dev/null || echo 4)
# If we bootstrapped GLEW, pass its lib dir via LDFLAGS (command-line override
# has highest priority; main.mk's LDFLAGS:= assignment is overridden by this).
# LDFLAGS entries are prefixed with -Wl, by main.mk before passing to g++,
# so -L works correctly for the linker.
if [[ -n "${GLEW_TMPDIR:-}" ]]; then
    make -C "$OPENMSX_SRC" -j"$NCPU" \
        LDFLAGS="-L$GLEW_TMPDIR/usr/lib/x86_64-linux-gnu"
else
    make -C "$OPENMSX_SRC" -j"$NCPU"
fi

# Locate the produced binary.
# openMSX places it at derived/<platform>/openmsx and creates a symlink at
# derived/openmsx. Accept whichever exists.
BUILT_BIN=""
if [[ -x "$OPENMSX_SRC/derived/openmsx" ]]; then
    BUILT_BIN="$OPENMSX_SRC/derived/openmsx"
else
    BUILT_BIN="$(find "$OPENMSX_SRC/derived" -maxdepth 3 -name "openmsx" -type f 2>/dev/null | head -1)"
fi

if [[ -z "$BUILT_BIN" ]] || [[ ! -x "$BUILT_BIN" ]]; then
    echo "build_openmsx.sh: build succeeded but binary not found under $OPENMSX_SRC/derived" >&2
    exit 1
fi

# Install.
mkdir -p "$OPENMSX_DEST/bin"
cp -f "$BUILT_BIN" "$OPENMSX_BIN"
chmod +x "$OPENMSX_BIN"

echo ""
echo "=== openMSX installed: $OPENMSX_BIN ==="
echo ""
echo "The test fixture uses C-BIOS (no proprietary ROM required)."
echo "Ensure the cbios package is installed:"
echo "  sudo apt install cbios"
echo ""
echo "Run the Stage 12 test:"
echo "  bash fw/tests/openMSX/run_test.sh fw/tests/openMSX/test_menu_stub_basic.tcl"
