#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Build the Deneb Touchscreen UI update package (.deneb).
# Run from the Deneb repo root after cross-compiling deneb-ui for MIPS.
#
# Usage:
#   ./ui/build-package.sh <path-to-compiled-deneb-ui>
#
# Produces:
#   dist/Deneb_UI_<version>.deneb

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BINARY="${1:?Usage: $0 <path-to-deneb-ui>}"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: binary not found: $BINARY"
    exit 1
fi

# Get version from git
VERSION=$(cd "$REPO_ROOT" && git describe --tags --always 2>/dev/null || echo "dev")
PACKAGE_NAME="Deneb_UI_${VERSION}"
STAGING_DIR="${REPO_ROOT}/build/ui-package/${PACKAGE_NAME}"
OUTPUT_DIR="${REPO_ROOT}/dist"
OUTPUT_IMG="${OUTPUT_DIR}/${PACKAGE_NAME}.deneb"

echo "Building ${PACKAGE_NAME}"

# Clean staging
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"

# Copy binary
cp "$BINARY" "${STAGING_DIR}/deneb-ui"
STRIP_TOOL="${STRIP:-}"
if [ -z "$STRIP_TOOL" ]; then
    if command -v mipsel-linux-musl-strip >/dev/null 2>&1; then
        STRIP_TOOL="mipsel-linux-musl-strip"
    elif [ -x /root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-strip ]; then
        STRIP_TOOL="/root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-strip"
    fi
fi
if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" "${STAGING_DIR}/deneb-ui" 2>/dev/null || true
fi
chmod 0755 "${STAGING_DIR}/deneb-ui"

# Copy scripts with Unix line endings. Windows worktrees can CRLF shell files,
# and OpenWrt shebangs fail hard on /etc/rc.common\r.
tr -d '\r' < "${SCRIPT_DIR}/installer/update.sh" > "${STAGING_DIR}/update.sh"
chmod 0755 "${STAGING_DIR}/update.sh"

# Copy locale files
for f in "${SCRIPT_DIR}"/locales/*.json; do
    cp "$f" "${STAGING_DIR}/"
done

# Copy init script
tr -d '\r' < "${SCRIPT_DIR}/init/deneb-ui.init" > "${STAGING_DIR}/deneb-ui.init"
chmod 0755 "${STAGING_DIR}/deneb-ui.init"

# Copy license and third-party notices for redistributed binaries/components.
cp "${REPO_ROOT}/LICENSE" "${STAGING_DIR}/LICENSE"
cp "${REPO_ROOT}/THIRD_PARTY_NOTICES.md" "${STAGING_DIR}/THIRD_PARTY_NOTICES.md"
cp "${SCRIPT_DIR}/lib/lvgl/LICENCE.txt" "${STAGING_DIR}/LVGL_LICENCE.txt"
cp "${SCRIPT_DIR}/lib/lvgl/src/stdlib/builtin/LICENSE_SPRINTF.txt" "${STAGING_DIR}/LVGL_LICENSE_SPRINTF.txt"
cp "${SCRIPT_DIR}/lib/lvgl/src/stdlib/builtin/LICENSE_TLSF.txt" "${STAGING_DIR}/LVGL_LICENSE_TLSF.txt"
cp "${REPO_ROOT}/notices/libzmq-4.3.5-NOTICE.txt" "${STAGING_DIR}/LIBZMQ_NOTICE.txt"
cp "${REPO_ROOT}/notices/MPL-2.0.txt" "${STAGING_DIR}/MPL-2.0.txt"

# Create manifest
cat > "${STAGING_DIR}/manifest.txt" <<EOF
package: ${PACKAGE_NAME}
version: ${VERSION}
date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
contents:
  deneb-ui          - LVGL touchscreen UI binary (MIPS)
  deneb-ui.init     - OpenWrt procd init script
  deneb-df-bridge   - Symlink installed to deneb-ui C Digital Factory bridge entry point
  update.sh         - Installer script
  *.json            - Bundled Deneb locale files
  LICENSE           - Deneb MPL-2.0 project license summary
  THIRD_PARTY_NOTICES.md - Third-party dependency notices
  LVGL_LICENCE.txt  - LVGL MIT license notice
  LVGL_LICENSE_SPRINTF.txt - LVGL bundled printf helper MIT license notice
  LVGL_LICENSE_TLSF.txt - LVGL bundled TLSF helper BSD-style license notice
  LIBZMQ_NOTICE.txt - libzmq MPL-2.0 notice and source location
  MPL-2.0.txt       - Mozilla Public License 2.0 text for libzmq
  manifest.txt      - This file
EOF

# Create tar-backed .deneb package for the Deneb USB update lane
cd "$STAGING_DIR"
tar cf "$OUTPUT_IMG" deneb-ui deneb-ui.init update.sh ./*.json LICENSE THIRD_PARTY_NOTICES.md LVGL_LICENCE.txt LVGL_LICENSE_SPRINTF.txt LVGL_LICENSE_TLSF.txt LIBZMQ_NOTICE.txt MPL-2.0.txt manifest.txt

echo "Package: ${OUTPUT_IMG}"
echo "Size: $(wc -c < "$OUTPUT_IMG") bytes"
echo "Install: Copy to USB, use Deneb Maintenance > Update Firmware"
