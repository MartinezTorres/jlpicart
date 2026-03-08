#!/usr/bin/env bash
# fetch_pico_sdk.sh — download and install the pinned Pico SDK, ARM toolchain,
# and picotool into fw/.pico-sdk/, matching the VS Code Pico extension layout.
#
# Layout produced:
#   fw/.pico-sdk/sdk/2.2.0/          — Pico SDK source
#   fw/.pico-sdk/toolchain/14_2_Rel1/ — ARM GNU toolchain (arm-none-eabi-gcc)
#   fw/.pico-sdk/picotool/2.2.0/     — picotool binary
#
# Version and hash information is read from tools/lock.yml.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOCK="${REPO_ROOT}/tools/lock.yml"
PICO_SDK_DIR="${REPO_ROOT}/fw/.pico-sdk"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

lock_get() {
    # lock_get <section> <key>
    # Extracts a value from a named section in tools/lock.yml.
    local section="$1" key="$2"
    awk "/^${section}:/{f=1} f && /^  ${key}:/{print \$2; exit}" "$LOCK" | tr -d '"'
}

download_and_verify() {
    local name="$1" url="$2" expected_sha256="$3" dest_file="$4"
    echo "==> Downloading ${name}..."
    curl -L --progress-bar "$url" -o "$dest_file"
    echo "==> Verifying ${name} SHA256..."
    local actual
    actual=$(sha256sum "$dest_file" | awk '{print $1}')
    if [ "$actual" != "$expected_sha256" ]; then
        echo "ERROR: SHA256 mismatch for ${name}!"
        echo "  expected: ${expected_sha256}"
        echo "  actual:   ${actual}"
        exit 1
    fi
    echo "SHA256 OK"
}

# ---------------------------------------------------------------------------
# Pico SDK
# ---------------------------------------------------------------------------

SDK_VERSION=$(lock_get pico_sdk version)
SDK_URL=$(lock_get pico_sdk url)
SDK_SHA256=$(lock_get pico_sdk sha256)
SDK_SUBDIR=$(lock_get pico_sdk extract_subdir)
SDK_DEST="${PICO_SDK_DIR}/sdk/${SDK_VERSION}"

if [ -f "${SDK_DEST}/pico_sdk_init.cmake" ]; then
    echo "Pico SDK ${SDK_VERSION} already present at ${SDK_DEST}"
else
    TMPFILE=$(mktemp /tmp/pico-sdk-XXXXXX.tar.gz)
    trap 'rm -f "$TMPFILE"' EXIT
    download_and_verify "Pico SDK ${SDK_VERSION}" "$SDK_URL" "$SDK_SHA256" "$TMPFILE"
    echo "==> Extracting Pico SDK to ${SDK_DEST}..."
    mkdir -p "${SDK_DEST}"
    tar -xzf "$TMPFILE" -C "${SDK_DEST}" --strip-components=1
    # Initialize Pico SDK submodules (tinyusb, etc.)
    echo "==> Initializing Pico SDK submodules..."
    git -C "${SDK_DEST}" submodule update --init --depth=1 2>&1 | tail -5
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: Pico SDK installed at ${SDK_DEST}"
fi

# ---------------------------------------------------------------------------
# ARM toolchain
# ---------------------------------------------------------------------------

TOOLCHAIN_VERSION=$(lock_get arm_toolchain version)
TOOLCHAIN_URL=$(lock_get arm_toolchain url)
TOOLCHAIN_SHA256=$(lock_get arm_toolchain sha256)
TOOLCHAIN_DEST="${PICO_SDK_DIR}/toolchain/${TOOLCHAIN_VERSION}"

if [ -x "${TOOLCHAIN_DEST}/bin/arm-none-eabi-gcc" ]; then
    echo "ARM toolchain ${TOOLCHAIN_VERSION} already present at ${TOOLCHAIN_DEST}"
else
    TMPFILE=$(mktemp /tmp/arm-toolchain-XXXXXX.tar.xz)
    trap 'rm -f "$TMPFILE"' EXIT
    download_and_verify "ARM toolchain ${TOOLCHAIN_VERSION}" "$TOOLCHAIN_URL" "$TOOLCHAIN_SHA256" "$TMPFILE"
    echo "==> Extracting ARM toolchain to ${TOOLCHAIN_DEST}..."
    mkdir -p "${TOOLCHAIN_DEST}"
    tar -xJf "$TMPFILE" -C "${TOOLCHAIN_DEST}" --strip-components=1
    trap - EXIT
    rm -f "$TMPFILE"
    echo "OK: ARM toolchain installed at ${TOOLCHAIN_DEST}"
fi

# ---------------------------------------------------------------------------
# picotool (built from source)
# ---------------------------------------------------------------------------

PICOTOOL_VERSION=$(lock_get picotool version)
PICOTOOL_URL=$(lock_get picotool url)
PICOTOOL_SHA256=$(lock_get picotool sha256)
PICOTOOL_DEST="${PICO_SDK_DIR}/picotool/${PICOTOOL_VERSION}"

if [ -x "${PICOTOOL_DEST}/picotool" ]; then
    echo "picotool ${PICOTOOL_VERSION} already present at ${PICOTOOL_DEST}"
else
    TMPFILE=$(mktemp /tmp/picotool-XXXXXX.tar.gz)
    TMPDIR=$(mktemp -d /tmp/picotool-src-XXXXXX)
    trap 'rm -f "$TMPFILE"; rm -rf "$TMPDIR"' EXIT
    download_and_verify "picotool ${PICOTOOL_VERSION}" "$PICOTOOL_URL" "$PICOTOOL_SHA256" "$TMPFILE"
    echo "==> Extracting and building picotool..."
    tar -xzf "$TMPFILE" -C "$TMPDIR" --strip-components=1
    mkdir -p "${TMPDIR}/build"
    cmake -S "$TMPDIR" -B "${TMPDIR}/build" \
        -DCMAKE_BUILD_TYPE=Release \
        -DPICO_SDK_PATH="${SDK_DEST}" \
        -DFETCHCONTENT_FULLY_DISCONNECTED=OFF \
        -Wno-dev -DCMAKE_INSTALL_PREFIX="${PICOTOOL_DEST}" 2>&1 | tail -5
    make -C "${TMPDIR}/build" -j"$(nproc)" 2>&1 | tail -5
    mkdir -p "${PICOTOOL_DEST}"
    cp "${TMPDIR}/build/picotool" "${PICOTOOL_DEST}/picotool"
    trap - EXIT
    rm -f "$TMPFILE"
    rm -rf "$TMPDIR"
    echo "OK: picotool installed at ${PICOTOOL_DEST}/picotool"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
echo "==> Installed versions:"
"${TOOLCHAIN_DEST}/bin/arm-none-eabi-gcc" --version | head -1
"${PICOTOOL_DEST}/picotool" version 2>/dev/null || true
echo ""
echo "OK: fw/.pico-sdk/ is ready. Build with:"
echo "    PICO_TOOLCHAIN_PATH=${TOOLCHAIN_DEST}/bin cmake fw/ ..."
