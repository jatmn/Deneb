#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Build the Deneb update package (.deneb).
# Run from the Deneb repo root after cross-compiling runtime binaries for MIPS.
#
# Usage:
#   ./ui/build-package.sh <path-to-compiled-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc]
#
# Produces:
#   dist/Deneb_Update_<version>.deneb

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BINARY="${1:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc]}"
WEB_API_BINARY="${2:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc]}"
LIGHTTPD_BINARY="${3:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc]}"
MDNS_BINARY="${4:-$(dirname "$WEB_API_BINARY")/deneb-mdns}"
PRINTSVC_BINARY="${5:-$(dirname "$WEB_API_BINARY")/../../printsvc/build-musl/deneb-printsvc}"
DENEB_RELEASE_CHANNEL="${DENEB_RELEASE_CHANNEL:-experimental}"
PRINTSVC_STOCK_SUMMARY="${DENEB_PRINTSVC_STOCK_SUMMARY:-}"
PRINTSVC_NATIVE_SUMMARY="${DENEB_PRINTSVC_NATIVE_SUMMARY:-}"
PRINTSVC_RELEASE_GATE="non-experimental packages require verified stock/native smoke summaries with strict resource reduction"

if [ ! -f "$BINARY" ]; then
    echo "ERROR: binary not found: $BINARY"
    exit 1
fi

if [ ! -f "$WEB_API_BINARY" ]; then
    echo "ERROR: deneb-api binary not found: $WEB_API_BINARY"
    exit 1
fi
if [ ! -f "$LIGHTTPD_BINARY" ]; then
    echo "ERROR: lighttpd binary not found: $LIGHTTPD_BINARY"
    exit 1
fi
if [ ! -f "$MDNS_BINARY" ]; then
    echo "ERROR: deneb-mdns binary not found: $MDNS_BINARY"
    exit 1
fi
if [ ! -f "$PRINTSVC_BINARY" ]; then
    echo "ERROR: deneb-printsvc binary not found: $PRINTSVC_BINARY"
    exit 1
fi

# Get version from git, unless a selftest supplies an isolated package name.
VERSION="${DENEB_PACKAGE_VERSION_OVERRIDE:-$(cd "$REPO_ROOT" && git describe --tags --always 2>/dev/null || echo "dev")}"
PACKAGE_NAME="Deneb_Update_${VERSION}"
STAGING_DIR="${REPO_ROOT}/build/ui-package/${PACKAGE_NAME}"
OUTPUT_DIR="${REPO_ROOT}/dist"
OUTPUT_IMG="${OUTPUT_DIR}/${PACKAGE_NAME}.deneb"

echo "Building ${PACKAGE_NAME}"

case "$DENEB_RELEASE_CHANNEL" in
    experimental|nightly|stable) ;;
    *)
        echo "ERROR: DENEB_RELEASE_CHANNEL must be experimental, nightly, or stable" >&2
        exit 1
        ;;
esac

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

# Copy web/API runtime into the same Deneb update package.
cp "$WEB_API_BINARY" "${STAGING_DIR}/deneb-api"
if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" "${STAGING_DIR}/deneb-api" 2>/dev/null || true
fi
chmod 0755 "${STAGING_DIR}/deneb-api"

cp "$LIGHTTPD_BINARY" "${STAGING_DIR}/lighttpd"
if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" "${STAGING_DIR}/lighttpd" 2>/dev/null || true
fi
chmod 0755 "${STAGING_DIR}/lighttpd"

cp "$MDNS_BINARY" "${STAGING_DIR}/deneb-mdns"
if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" "${STAGING_DIR}/deneb-mdns" 2>/dev/null || true
fi
chmod 0755 "${STAGING_DIR}/deneb-mdns"

cp "$PRINTSVC_BINARY" "${STAGING_DIR}/deneb-printsvc"
if [ -n "$STRIP_TOOL" ]; then
    "$STRIP_TOOL" "${STAGING_DIR}/deneb-printsvc" 2>/dev/null || true
fi
chmod 0755 "${STAGING_DIR}/deneb-printsvc"

tr -d '\r' < "${REPO_ROOT}/web/lighttpd.conf" > "${STAGING_DIR}/lighttpd.conf"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-api.init" > "${STAGING_DIR}/deneb-api.init"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-web.init" > "${STAGING_DIR}/deneb-web.init"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-mdns.init" > "${STAGING_DIR}/deneb-mdns.init"
tr -d '\r' < "${REPO_ROOT}/printsvc/init/deneb-printsvc.init" > "${STAGING_DIR}/deneb-printsvc.init"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke.sh" > "${STAGING_DIR}/deneb-printsvc-smoke"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-verify.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-verify"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-compare.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-compare"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-cli-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-cli-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-init-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-init-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-release-gate-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-release-gate-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-native-audit.sh" > "${STAGING_DIR}/deneb-printsvc-native-audit"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-native-audit-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-native-audit-selftest"
chmod 0755 "${STAGING_DIR}/deneb-api.init" "${STAGING_DIR}/deneb-web.init" "${STAGING_DIR}/deneb-mdns.init" "${STAGING_DIR}/deneb-printsvc.init"
chmod 0755 "${STAGING_DIR}/deneb-printsvc-smoke" "${STAGING_DIR}/deneb-printsvc-smoke-verify" "${STAGING_DIR}/deneb-printsvc-smoke-compare" "${STAGING_DIR}/deneb-printsvc-smoke-selftest" "${STAGING_DIR}/deneb-printsvc-cli-selftest" "${STAGING_DIR}/deneb-printsvc-init-selftest" "${STAGING_DIR}/deneb-printsvc-release-gate-selftest" "${STAGING_DIR}/deneb-printsvc-native-audit" "${STAGING_DIR}/deneb-printsvc-native-audit-selftest"

if [ "$DENEB_RELEASE_CHANNEL" != "experimental" ]; then
    if [ -z "$PRINTSVC_STOCK_SUMMARY" ] || [ -z "$PRINTSVC_NATIVE_SUMMARY" ]; then
        echo "ERROR: non-experimental native printsvc builds require DENEB_PRINTSVC_STOCK_SUMMARY and DENEB_PRINTSVC_NATIVE_SUMMARY" >&2
        exit 1
    fi
    if [ ! -f "$PRINTSVC_STOCK_SUMMARY" ]; then
        echo "ERROR: stock printsvc smoke summary not found: $PRINTSVC_STOCK_SUMMARY" >&2
        exit 1
    fi
    if [ ! -f "$PRINTSVC_NATIVE_SUMMARY" ]; then
        echo "ERROR: native printsvc smoke summary not found: $PRINTSVC_NATIVE_SUMMARY" >&2
        exit 1
    fi
    "${STAGING_DIR}/deneb-printsvc-smoke-verify" --full "$PRINTSVC_NATIVE_SUMMARY"
    "${STAGING_DIR}/deneb-printsvc-smoke-compare" --require-reduction "$PRINTSVC_STOCK_SUMMARY" "$PRINTSVC_NATIVE_SUMMARY"
fi

mkdir -p "${STAGING_DIR}/deneb-printsvc-macros"
cp "${REPO_ROOT}/printsvc/macros/"*.gcode "${STAGING_DIR}/deneb-printsvc-macros/"
chmod 0644 "${STAGING_DIR}"/deneb-printsvc-macros/*.gcode

mkdir -p "${STAGING_DIR}/www"
cp -r "${REPO_ROOT}/web/www/"* "${STAGING_DIR}/www/"

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
channel: ${DENEB_RELEASE_CHANNEL}
date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
native_printsvc: experimental
native_printsvc_release_gate: ${PRINTSVC_RELEASE_GATE}
contents:
  deneb-ui          - LVGL touchscreen UI binary (MIPS)
  deneb-ui.init     - OpenWrt procd init script
  deneb-df-bridge   - Symlink installed to deneb-ui C Digital Factory bridge entry point
  deneb-api         - Local REST API and web session service (MIPS)
  deneb-mdns        - Lightweight mDNS advertiser for Cura local discovery (MIPS)
  deneb-printsvc    - Native print service replacement (MIPS)
  deneb-printsvc-smoke - Native print service smoke/resource harness
  deneb-printsvc-smoke-verify - Shell verifier for smoke summary evidence
  deneb-printsvc-smoke-compare - Shell stock/native smoke summary comparator
  deneb-printsvc-smoke-selftest - Shell synthetic verifier/comparator selftest
  deneb-printsvc-cli-selftest - Shell native print-service binary CLI selftest
  deneb-printsvc-init-selftest - Shell native init handoff selftest
  deneb-printsvc-release-gate-selftest - Shell release-channel gate selftest
  deneb-printsvc-native-audit - Shell static/package de-Python regression audit
  deneb-printsvc-native-audit-selftest - Shell negative-fixture selftest for native audit
  deneb-printsvc-macros/ - Deneb-owned native print-service macro defaults
  lighttpd          - Static web server and API reverse proxy (MIPS)
  deneb-api.init    - OpenWrt procd init script for deneb-api
  deneb-web.init    - OpenWrt procd init script for lighttpd
  deneb-mdns.init   - OpenWrt procd init script for deneb-mdns
  deneb-printsvc.init - OpenWrt procd init script for deneb-printsvc
  lighttpd.conf     - Deneb web server configuration
  www/              - Static Deneb web UI assets
  update.sh         - Installer script
  *.json            - Bundled Deneb locale files
  LICENSE           - Deneb MPL-2.0 project license summary
  THIRD_PARTY_NOTICES.md - Third-party dependency notices
  LVGL_LICENCE.txt  - LVGL MIT license notice
  LVGL_LICENSE_SPRINTF.txt - LVGL bundled printf helper MIT license notice
  LVGL_LICENSE_TLSF.txt - LVGL bundled TLSF helper BSD-style license notice
  LIBZMQ_NOTICE.txt - libzmq MPL-2.0 notice and source location
  MPL-2.0.txt       - Mozilla Public License 2.0 text for libzmq
  no Python driver files are packaged; native deneb-printsvc owns marlindriver
  manifest.txt      - This file
EOF

if find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
    -print | grep . >/dev/null 2>&1; then
    echo "ERROR: Python driver artifact found in Deneb package staging" >&2
    find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
        -print >&2
    exit 1
fi

"${STAGING_DIR}/deneb-printsvc-smoke-selftest"
DENEB_REPO_ROOT="$REPO_ROOT" "${STAGING_DIR}/deneb-printsvc-release-gate-selftest"
"${STAGING_DIR}/deneb-printsvc-native-audit" --source "$REPO_ROOT"
"${STAGING_DIR}/deneb-printsvc-native-audit" --package-dir "$STAGING_DIR"
"${STAGING_DIR}/deneb-printsvc-native-audit-selftest"
DENEB_REPO_ROOT="$STAGING_DIR" \
DENEB_PRINTSVC_INIT="${STAGING_DIR}/deneb-printsvc.init" \
DENEB_INSTALLER="${STAGING_DIR}/update.sh" \
    "${STAGING_DIR}/deneb-printsvc-init-selftest"

# Create tar-backed .deneb package for the Deneb USB update lane
cd "$STAGING_DIR"
tar cf "$OUTPUT_IMG" deneb-ui deneb-ui.init update.sh ./*.json LICENSE THIRD_PARTY_NOTICES.md LVGL_LICENCE.txt LVGL_LICENSE_SPRINTF.txt LVGL_LICENSE_TLSF.txt LIBZMQ_NOTICE.txt MPL-2.0.txt manifest.txt \
    deneb-api deneb-mdns deneb-printsvc deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-cli-selftest deneb-printsvc-init-selftest deneb-printsvc-release-gate-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-macros lighttpd deneb-api.init deneb-web.init deneb-mdns.init deneb-printsvc.init lighttpd.conf www

tar tf "$OUTPUT_IMG" > "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)update.sh$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc.init$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-verify$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-compare$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-cli-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-init-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-release-gate-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-native-audit$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-native-audit-selftest$' "${STAGING_DIR}/package-files.txt"
tar -xOf "$OUTPUT_IMG" manifest.txt > "${STAGING_DIR}/package-manifest.txt"
grep -Eq "^channel: ${DENEB_RELEASE_CHANNEL}$" "${STAGING_DIR}/package-manifest.txt"
grep -Eq '^native_printsvc: experimental$' "${STAGING_DIR}/package-manifest.txt"
grep -Fx "native_printsvc_release_gate: ${PRINTSVC_RELEASE_GATE}" "${STAGING_DIR}/package-manifest.txt" >/dev/null
"${STAGING_DIR}/deneb-printsvc-native-audit" --archive "$OUTPUT_IMG"
if grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' "${STAGING_DIR}/package-files.txt"; then
    echo "ERROR: Python driver artifact found in Deneb package archive" >&2
    exit 1
fi
rm -f "${STAGING_DIR}/package-files.txt" "${STAGING_DIR}/package-manifest.txt"

echo "Package: ${OUTPUT_IMG}"
echo "Size: $(wc -c < "$OUTPUT_IMG") bytes"
echo "Install: Copy to USB, use Deneb Maintenance > Update Firmware"
