#!/usr/bin/env bash
# build_openmsx.sh — build openMSX from the pinned submodule
# Output: tools/openmsx/bin/openmsx
# See tools/README.md for build dependencies.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OPENMSX_SRC="${REPO_ROOT}/third_party/openMSX"
OPENMSX_OUT="${REPO_ROOT}/tools/openmsx"
OPENMSX_BIN="${OPENMSX_OUT}/bin/openmsx"

echo "==> Checking submodule..."
if [ ! -f "${OPENMSX_SRC}/build/main.mk" ]; then
    echo "ERROR: third_party/openMSX submodule not populated."
    echo "Run: git submodule update --init third_party/openMSX"
    exit 1
fi

echo "==> Building openMSX (this takes several minutes)..."
cd "${OPENMSX_SRC}"

# Build using openMSX's own build system. INSTALL_BASE sets the output prefix.
make -j"$(nproc)" INSTALL_BASE="${OPENMSX_OUT}" install

echo ""
echo "==> Build complete. Verifying..."
if [ ! -x "${OPENMSX_BIN}" ]; then
    echo "ERROR: Expected binary not found at ${OPENMSX_BIN}"
    exit 1
fi

echo "==> openMSX version:"
"${OPENMSX_BIN}" --version
echo ""
echo "OK: openMSX installed at ${OPENMSX_BIN}"
