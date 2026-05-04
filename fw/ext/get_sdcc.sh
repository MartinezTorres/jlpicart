#!/usr/bin/env bash
# get_sdcc.sh — download and unpack pinned SDCC Z80 compiler to fw/ext/sdcc/
# Usage: bash fw/ext/get_sdcc.sh [--force]
#
# On first run: downloads, unpacks, prints SHA256, updates lock.yml.
# On subsequent runs: verifies SHA256, no-ops if already present.
# With --force: re-downloads even if already present.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOCK_YML="${SCRIPT_DIR}/lock.yml"
SDCC_DIR="${SCRIPT_DIR}/sdcc"
SDCC_BIN="${SDCC_DIR}/bin/sdcc"
TMP_DIR="${SCRIPT_DIR}/.sdcc_tmp"

# Parse lock.yml (minimal: grep for the fields we need).
SDCC_VERSION=$(grep 'version:' "$LOCK_YML" | head -1 | awk -F'"' '{print $2}')
SDCC_URL=$(grep 'url:' "$LOCK_YML" | head -1 | awk -F'"' '{print $2}')
SDCC_SHA256=$(grep 'sha256:' "$LOCK_YML" | head -1 | awk -F'"' '{print $2}')

FORCE=0
[[ "${1:-}" == "--force" ]] && FORCE=1

if [[ -x "$SDCC_BIN" && "$FORCE" -eq 0 ]]; then
    echo "SDCC ${SDCC_VERSION} already present at ${SDCC_BIN} — nothing to do."
    echo "Run with --force to re-download."
    exit 0
fi

echo "Downloading SDCC ${SDCC_VERSION}..."
mkdir -p "$TMP_DIR"
TARBALL="${TMP_DIR}/sdcc.tar.bz2"

if ! curl -fSL --progress-bar -o "$TARBALL" "$SDCC_URL"; then
    echo "ERROR: download failed. Check URL in lock.yml." >&2
    exit 1
fi

ACTUAL_SHA256=$(sha256sum "$TARBALL" | awk '{print $1}')
echo "SHA256: ${ACTUAL_SHA256}"

if [[ -n "$SDCC_SHA256" ]]; then
    if [[ "$ACTUAL_SHA256" != "$SDCC_SHA256" ]]; then
        echo "ERROR: SHA256 mismatch!" >&2
        echo "  expected: ${SDCC_SHA256}" >&2
        echo "  actual:   ${ACTUAL_SHA256}" >&2
        rm -f "$TARBALL"
        exit 1
    fi
    echo "SHA256 verified."
else
    echo "WARNING: no sha256 pinned in lock.yml. Pinning now..."
    # Update the sha256 line in lock.yml in-place.
    sed -i "s|  sha256: \"\"|  sha256: \"${ACTUAL_SHA256}\"|" "$LOCK_YML"
    echo "Updated lock.yml with sha256: ${ACTUAL_SHA256}"
    echo "Commit lock.yml to freeze the pin."
fi

echo "Unpacking to ${SDCC_DIR}..."
rm -rf "$SDCC_DIR"
mkdir -p "$SDCC_DIR"
# The tarball extracts to sdcc-<version>/; move its contents into SDCC_DIR.
tar -xjf "$TARBALL" -C "$TMP_DIR"
EXTRACTED=$(ls -d "${TMP_DIR}"/sdcc-*/ 2>/dev/null | head -1)
if [[ -z "$EXTRACTED" ]]; then
    echo "ERROR: could not find extracted sdcc directory in ${TMP_DIR}" >&2
    exit 1
fi
cp -a "${EXTRACTED}/." "${SDCC_DIR}/"

rm -rf "$TMP_DIR"
echo "SDCC ${SDCC_VERSION} installed at ${SDCC_DIR}."
echo "Compiler: ${SDCC_BIN}"
