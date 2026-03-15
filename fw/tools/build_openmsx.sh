#!/usr/bin/env bash
# fw/tools/build_openmsx.sh — build openMSX from the pinned third_party submodule.
#
# Invoke from the fw/ root or repo root:
#   bash fw/tools/build_openmsx.sh
#
# Installs the binary at:
#   fw/tools/openmsx/bin/openmsx
#
# If build fails due to missing SDL2_ttf dev headers, tests fall back to the
# system openMSX binary (run_test.sh checks /usr/bin/openmsx automatically).
# To install the required header: sudo apt install libsdl2-ttf-dev
#
# System prerequisites (install with apt before running):
#   libsdl2-dev libsdl2-ttf-dev libpng-dev libogg-dev libvorbis-dev
#   libtcl-dev libao-dev zlib1g-dev libfreetype6-dev python3 g++ make
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

echo "=== Building openMSX RELEASE_21_0 ==="
echo "    Source : $OPENMSX_SRC"
echo "    Install: $OPENMSX_BIN"
echo ""

# Build in the submodule (parallel, using all cores).
NCPU=$(nproc 2>/dev/null || echo 4)
make -C "$OPENMSX_SRC" -j"$NCPU"

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
