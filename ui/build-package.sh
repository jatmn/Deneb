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

# Copy helper scripts
tr -d '\r' < "${SCRIPT_DIR}/scripts/deneb-df-bridge.py" > "${STAGING_DIR}/deneb-df-bridge.py"
chmod 0755 "${STAGING_DIR}/deneb-df-bridge.py"

# Create manifest
cat > "${STAGING_DIR}/manifest.txt" <<EOF
package: ${PACKAGE_NAME}
version: ${VERSION}
date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
contents:
  deneb-ui          - LVGL touchscreen UI binary (MIPS)
  deneb-ui.init     - OpenWrt procd init script
  deneb-df-bridge.py - Digital Factory Gershwin IPC bridge
  update.sh         - Installer script
  *.json            - Bundled Deneb locale files
  manifest.txt      - This file
EOF

# Create tar-backed .deneb package for the Deneb USB update lane
cd "$STAGING_DIR"
tar cf "$OUTPUT_IMG" deneb-ui deneb-ui.init deneb-df-bridge.py update.sh ./*.json manifest.txt

echo "Package: ${OUTPUT_IMG}"
echo "Size: $(wc -c < "$OUTPUT_IMG") bytes"
echo "Install: Copy to USB, use Deneb Maintenance > Update Firmware"
