#!/usr/bin/env bash
# fetch_sdcc.sh — download and verify the pinned SDCC Z80 compiler
# Output: tools/sdcc/  (contains bin/sdcc, include/, lib/, etc.)
# Version and hash are read from tools/lock.yml.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="${REPO_ROOT}/tools/lock.yml"
DEST="${REPO_ROOT}/tools/sdcc"

lock_get() {
    local section="$1" key="$2"
    awk "/^${section}:/{f=1} f && /^  ${key}:/{print \$2; exit}" "$LOCK" | tr -d '"'
}

VERSION=$(lock_get sdcc version)
URL=$(lock_get sdcc url)
EXPECTED_SHA256=$(lock_get sdcc sha256)

echo "==> Fetching SDCC ${VERSION}..."

if [ -x "${DEST}/bin/sdcc" ]; then
    INSTALLED=$("${DEST}/bin/sdcc" -v 2>&1 | head -1 || true)
    echo "Already installed: ${INSTALLED}"
    echo "Remove ${DEST} to force re-download."
    exit 0
fi

TMPFILE=$(mktemp /tmp/sdcc-XXXXXX.tar.bz2)
trap 'rm -f "$TMPFILE"' EXIT

echo "==> Downloading from ${URL}..."
curl -L --progress-bar "$URL" -o "$TMPFILE"

echo "==> Verifying SHA256..."
ACTUAL_SHA256=$(sha256sum "$TMPFILE" | awk '{print $1}')
if [ "$ACTUAL_SHA256" != "$EXPECTED_SHA256" ]; then
    echo "ERROR: SHA256 mismatch!"
    echo "  expected: ${EXPECTED_SHA256}"
    echo "  actual:   ${ACTUAL_SHA256}"
    exit 1
fi
echo "SHA256 OK"

echo "==> Extracting to ${DEST}..."
mkdir -p "$DEST"
tar -xjf "$TMPFILE" -C "$DEST" --strip-components=1

trap - EXIT
rm -f "$TMPFILE"

echo ""
echo "==> SDCC version:"
"${DEST}/bin/sdcc" -v 2>&1 | head -1
echo ""
echo "OK: SDCC installed at ${DEST}/bin/sdcc"
