#!/usr/bin/env bash
# Apply local patches to submodules after `git submodule update --init`
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCHES_DIR="${SCRIPT_DIR}/patches"
SRC_DIR="${SCRIPT_DIR}/src"

apply_patches_to() {
    local submodule_path="$1"
    local patch_dir="$2"

    if [ ! -d "${submodule_path}" ]; then
        echo "ERROR: submodule directory not found: ${submodule_path}" >&2
        exit 1
    fi

    if [ ! -d "${patch_dir}" ]; then
        echo "No patch directory for $(basename "${submodule_path}"), skipping."
        return
    fi

    for patch in "${patch_dir}"/*.patch; do
        [ -f "${patch}" ] || continue
        echo "Applying ${patch} to $(basename "${submodule_path}")..."
        git -C "${submodule_path}" apply "${patch}"
    done
}

apply_patches_to "${SRC_DIR}/tinyusb"  "${PATCHES_DIR}/tinyusb"
apply_patches_to "${SRC_DIR}/esp-at"  "${PATCHES_DIR}/esp-at"

echo "All patches applied."
