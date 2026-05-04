#!/usr/bin/env bash
# fw/tests/openMSX/run_test.sh — run one openMSX TCL test script.
#
# Usage:
#   bash fw/tests/openMSX/run_test.sh <test.tcl> [extra openMSX options ...]
#
# Examples:
#   bash fw/tests/openMSX/run_test.sh fw/tests/openMSX/test_menu_stub_basic.tcl
#   bash fw/tests/openMSX/run_test.sh test_menu_stub_basic.tcl -machine Philips_VG_8020
#
# Environment (override defaults):
#   OPENMSX_BIN     path to openmsx binary (default: fw/ext/openmsx/bin/openmsx)
#   MENUPAGE_ROM    path to menupage.rom (default: fw/src/msx/menu/stub/menupage.rom)
#   TEST_MACHINE    openMSX machine name (default: msx1_jlpicart)
#   TEST_TIMEOUT    seconds before FAIL (default: 30)
#
# Exit code: 0 = PASS, 1 = FAIL or timeout.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
FW_ROOT="$REPO_ROOT/fw"

# --- Argument parsing ---
if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <test.tcl> [extra openMSX options ...]" >&2
    exit 1
fi

TEST_SCRIPT="$1"; shift

# Resolve relative path for test script.
if [[ ! -f "$TEST_SCRIPT" ]]; then
    # Try relative to the openMSX tests directory.
    if [[ -f "$SCRIPT_DIR/$TEST_SCRIPT" ]]; then
        TEST_SCRIPT="$SCRIPT_DIR/$TEST_SCRIPT"
    else
        echo "run_test.sh: test script not found: $TEST_SCRIPT" >&2
        exit 1
    fi
fi
TEST_SCRIPT="$(realpath "$TEST_SCRIPT")"

# --- Configuration with overridable defaults ---
OPENMSX_BIN="${OPENMSX_BIN:-$FW_ROOT/ext/openmsx/bin/openmsx}"
MENUPAGE_ROM="${MENUPAGE_ROM:-$FW_ROOT/src/msx/menu/stub/menupage.rom}"
TEST_MACHINE="${TEST_MACHINE:-msx1_jlpicart}"
TEST_TIMEOUT="${TEST_TIMEOUT:-30}"

# --- Pre-flight checks ---
if [[ ! -x "$OPENMSX_BIN" ]]; then
    echo "run_test.sh: openMSX binary not found at $OPENMSX_BIN" >&2
    echo "  Run: bash fw/ext/build_openmsx.sh" >&2
    exit 1
fi

if [[ ! -f "$MENUPAGE_ROM" ]]; then
    echo "run_test.sh: menupage.rom not found at $MENUPAGE_ROM" >&2
    echo "  Run: make -C fw/src/msx/menu/stub menupage.rom" >&2
    echo "  (Requires SDCC: bash fw/ext/get_sdcc.sh)" >&2
    exit 1
fi

# --- Environment for headless SDL ---
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}"
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"

# Let openMSX find our custom machine fixture.
# OPENMSX_USER_DATA mirrors the openMSX share directory structure:
#   fixtures/machines/msx1_jlpicart.xml  → machine "msx1_jlpicart"
export OPENMSX_USER_DATA="$SCRIPT_DIR/fixtures"

# Point OPENMSX_SYSTEM_DATA to the pinned openMSX share directory.
# Using the pinned share avoids version mismatches with system scripts.
# C-BIOS ROMs are looked up in systemroms/ under both OPENMSX_USER_DATA and
# OPENMSX_SYSTEM_DATA; the system cbios package (/usr/share/openmsx/systemroms)
# is the fallback if the pinned share has no systemroms.
OPENMSX_SRC_SHARE="$REPO_ROOT/third_party/openMSX/share"
if [[ -d "$OPENMSX_SRC_SHARE" ]]; then
    export OPENMSX_SYSTEM_DATA="${OPENMSX_SYSTEM_DATA:-$OPENMSX_SRC_SHARE}"
else
    export OPENMSX_SYSTEM_DATA="${OPENMSX_SYSTEM_DATA:-/usr/share/openmsx}"
fi

# Warn if OPENMSX_SYSTEM_DATA doesn't exist (C-BIOS ROMs won't be found).
if [[ ! -d "$OPENMSX_SYSTEM_DATA" ]]; then
    echo "run_test.sh: warning: OPENMSX_SYSTEM_DATA=$OPENMSX_SYSTEM_DATA not found" >&2
    echo "  Install C-BIOS: sudo apt install cbios   (or set OPENMSX_SYSTEM_DATA)" >&2
fi

echo "run_test.sh: running $TEST_SCRIPT"
echo "  machine : $TEST_MACHINE"
echo "  cart    : $MENUPAGE_ROM"
echo "  timeout : ${TEST_TIMEOUT}s"

# --- Run openMSX with timeout ---
# Use -control stdio so the event loop runs headlessly (SDL_VIDEODRIVER=dummy
# blocks the event loop in -script mode but not in -control mode).
# Feed: (1) set power on, (2) source the test script, (3) sleep to keep stdin
# open until the test calls exit 0/1 or the timeout fires.
# TESTRESULT lines are written to stderr by the test script and appear as
# plain text in the combined output (2>&1).
TMPOUT="$(mktemp)"
trap "rm -f '$TMPOUT'" EXIT

# Build control input as a subshell feeding the XML protocol on stdin.
# Step 1: load menupage.rom into slot-1 RAM before the CPU starts.
#   Slot 1 is configured as 16 KB RAM in the machine fixture so that the
#   Z80 stub can write to the mailbox (0x4040) and data buffer (0x4100).
#   A plain ROM cartridge (-cart) is read-only in openMSX; debug writes to
#   those addresses are silently discarded.  Loading the image into RAM via
#   debug commands lets both the TCL test and the Z80 stub read/write the
#   shared mailbox region.
# Step 2: set power on.
# Step 3: source the test script (after time callbacks use the openMSX
#   scheduler, which fires from the main loop — not from vwait).
_ctrl_input() {
    # Power on first so hardware (including Stub Test RAM) is initialised.
    # In -control stdio mode, debug commands run while the Z80 is paused
    # (the scheduler does not advance during command processing), so the ROM
    # bytes are in place before C-BIOS executes a single instruction.
    printf '<openmsx-control><command>set power on</command></openmsx-control>\n'
    # Write directly to the {Stub Test RAM} debuggable (defined in the machine
    # fixture as the 16 KB RAM at slot 1, 0x4000-0x7FFF).  Writing after
    # power-on ensures the device exists; the debuggable bypasses slot
    # selection and writes the backing store directly.
    printf '<openmsx-control><command>
set _fh [open {%s} rb]
set _rom [read $_fh]
close $_fh
debug write_block {Stub Test RAM} 0 $_rom
unset _rom _fh
</command></openmsx-control>\n' "$MENUPAGE_ROM"
    printf '<openmsx-control><command>source {%s}</command></openmsx-control>\n' "$TEST_SCRIPT"
    sleep "$TEST_TIMEOUT"
}

set +e
timeout "$TEST_TIMEOUT" "$OPENMSX_BIN" \
    -machine "$TEST_MACHINE" \
    -control stdio \
    "$@" \
    < <(_ctrl_input) \
    >"$TMPOUT" 2>&1
OPENMSX_EXIT=$?
set -e

# Print all output for diagnostics (tail 40 lines to keep CI logs readable).
echo "--- openMSX output ---"
tail -40 "$TMPOUT"
echo "--- end ---"

# Check for timeout.
if [[ $OPENMSX_EXIT -eq 124 ]]; then
    echo "FAIL: openMSX timed out after ${TEST_TIMEOUT}s"
    exit 1
fi

# Parse TESTRESULT line.
RESULT_LINE="$(grep -m1 '^TESTRESULT:' "$TMPOUT" || true)"

if [[ -z "$RESULT_LINE" ]]; then
    echo "FAIL: no TESTRESULT line in output (openMSX exit $OPENMSX_EXIT)"
    exit 1
fi

if [[ "$RESULT_LINE" == "TESTRESULT: PASS" ]]; then
    echo "PASS"
    exit 0
else
    echo "FAIL: $RESULT_LINE"
    exit 1
fi
