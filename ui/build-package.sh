#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Build the Deneb update package (.deneb).
# Run from the Deneb repo root after cross-compiling runtime binaries for MIPS.
#
# Usage:
#   ./ui/build-package.sh <path-to-compiled-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc] [path-to-deneb-dfsvc]
#   Optional supplemental evidence paths follow all six binary paths.
#
# Produces:
#   dist/Deneb_Update_<version>.deneb

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$REPO_ROOT/tools/native-dependency-pins.sh"
BINARY="${1:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc] [path-to-deneb-dfsvc]}"
WEB_API_BINARY="${2:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc] [path-to-deneb-dfsvc]}"
LIGHTTPD_BINARY="${3:?Usage: $0 <path-to-deneb-ui> <path-to-deneb-api> <path-to-lighttpd> [path-to-deneb-mdns] [path-to-deneb-printsvc] [path-to-deneb-dfsvc]}"
WEB_BUILD_DIR=$(dirname "$WEB_API_BINARY")
MDNS_BINARY="${4:-$WEB_BUILD_DIR/deneb-mdns}"
DEFAULT_COMPONENT_BUILD=build-musl
if [ "${WEB_BUILD_DIR##*/}" = build-musl-openwrt ]; then
    DEFAULT_COMPONENT_BUILD=build-musl-openwrt
fi
PRINTSVC_BINARY="${5:-$WEB_BUILD_DIR/../../printsvc/$DEFAULT_COMPONENT_BUILD/deneb-printsvc}"
DFSVC_BINARY="${6:-$WEB_BUILD_DIR/../../dfsvc/$DEFAULT_COMPONENT_BUILD/deneb-dfsvc}"
if [ "$#" -ge 6 ]; then
    shift 6
else
    set --
fi
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
if [ ! -f "$DFSVC_BINARY" ]; then
    echo "ERROR: deneb-dfsvc binary not found: $DFSVC_BINARY"
    exit 1
fi

# Nested packaging fixtures have their own inputs and provenance.
package_toolchain_root=${DENEB_PACKAGE_TOOLCHAIN_ROOT:-}
unset DENEB_PACKAGE_TOOLCHAIN_ROOT
package_runtime_key=
if [ -n "$package_toolchain_root" ]; then
    package_runtime_key=$(mips_toolchain_key "$package_toolchain_root") || package_runtime_key=
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

PACKAGE_EPOCH=${SOURCE_DATE_EPOCH-$(git -C "$REPO_ROOT" show -s --format=%ct HEAD 2>/dev/null || printf '0\n')}
case "$PACKAGE_EPOCH" in
    ''|*[!0-9]*)
        echo "ERROR: package timestamp must be a nonnegative integer in seconds" >&2
        exit 1
        ;;
esac
if ! PACKAGE_DATE=$(date -u -d "@$PACKAGE_EPOCH" +%Y-%m-%dT%H:%M:%SZ); then
    echo "ERROR: cannot format package timestamp: $PACKAGE_EPOCH" >&2
    exit 1
fi

# Clean staging
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR" "$OUTPUT_DIR"
# Keep staging modes stable while honoring the caller's umask for the output file.
OUTPUT_UMASK=$(umask)
umask 022

STRIP_TOOL="${STRIP:-}"
if [ -z "$STRIP_TOOL" ]; then
    toolchain_suffix="openwrt-$MIPS_TOOLCHAIN_RELEASE/$MIPS_TOOLCHAIN_MEMBER/bin/$MIPS_TOOLCHAIN_TARGET-strip"
    for candidate in "$MIPS_TOOLCHAIN_TARGET-strip" \
        "$REPO_ROOT/build/deneb-cross/$toolchain_suffix" "/root/deneb-build/$toolchain_suffix" \
        mipsel-linux-musl-strip /root/mipsel-linux-musl-cross/bin/mipsel-linux-musl-strip; do
        if command -v "$candidate" >/dev/null 2>&1; then
            STRIP_TOOL="$candidate"
            break
        fi
    done
fi
stage_executable() {
    input=$1
    staged="$STAGING_DIR/$2"
    cp "$input" "$staged"
    if [ -n "$package_toolchain_root" ]; then
        if ! headers=$(LC_ALL=C readelf -WhlAd "$staged") ||
            ! printf '%s\n' "$headers" | grep -Eq 'Class:[[:space:]]+ELF32' ||
            ! printf '%s\n' "$headers" | grep -Eq 'Data:.*little endian' ||
            ! printf '%s\n' "$headers" | grep -Eq 'Type:[[:space:]]+EXEC[[:space:]]' ||
            ! printf '%s\n' "$headers" | grep -Eq 'Machine:[[:space:]]+MIPS' ||
            ! printf '%s\n' "$headers" | grep -Eq 'Flags:.*o32.*mips32r2' ||
            ! printf '%s\n' "$headers" | grep -Fxq 'FP ABI: Soft float' ||
            printf '%s\n' "$headers" | grep -Eq '\(NEEDED\)|^[[:space:]]*INTERP[[:space:]]'; then
            echo "ERROR: $input is not a static MT7688 soft-float executable" >&2
            exit 1
        fi
        if [ -n "$package_runtime_key" ] &&
            ! mips_runtime_output_verified "$package_runtime_key" "$input" "$staged"; then
            package_runtime_key=
        fi
    fi
    if [ -n "$STRIP_TOOL" ]; then
        "$STRIP_TOOL" "$staged" 2>/dev/null || true
    fi
    chmod 0755 "$staged"
}

stage_executable "$BINARY" deneb-ui
stage_executable "$WEB_API_BINARY" deneb-api
stage_executable "$LIGHTTPD_BINARY" lighttpd
stage_executable "$MDNS_BINARY" deneb-mdns
stage_executable "$PRINTSVC_BINARY" deneb-printsvc
stage_executable "$DFSVC_BINARY" deneb-dfsvc

tr -d '\r' < "${REPO_ROOT}/web/lighttpd.conf" > "${STAGING_DIR}/lighttpd.conf"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-api.init" > "${STAGING_DIR}/deneb-api.init"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-web.init" > "${STAGING_DIR}/deneb-web.init"
tr -d '\r' < "${REPO_ROOT}/web/init/deneb-mdns.init" > "${STAGING_DIR}/deneb-mdns.init"
tr -d '\r' < "${REPO_ROOT}/printsvc/init/deneb-printsvc.init" > "${STAGING_DIR}/deneb-printsvc.init"
tr -d '\r' < "${REPO_ROOT}/dfsvc/init/digitalfactory.init" > "${STAGING_DIR}/digitalfactory.init"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke.sh" > "${STAGING_DIR}/deneb-printsvc-smoke"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-verify.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-verify"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-compare.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-compare"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-smoke-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-smoke-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-stability.sh" > "${STAGING_DIR}/deneb-printsvc-stability"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-active-physical-soak-runner.sh" > "${STAGING_DIR}/deneb-active-physical-soak-runner"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-stock-baseline.sh" > "${STAGING_DIR}/deneb-printsvc-stock-baseline"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-cli-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-cli-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-init-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-init-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-native-audit.sh" > "${STAGING_DIR}/deneb-printsvc-native-audit"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-native-audit-selftest.sh" > "${STAGING_DIR}/deneb-printsvc-native-audit-selftest"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-printsvc-integration-audit.sh" > "${STAGING_DIR}/deneb-printsvc-integration-audit"
tr -d '\r' < "${REPO_ROOT}/tools/deneb-python-runtime-inventory.sh" > "${STAGING_DIR}/deneb-runtime-inventory"
chmod 0755 "${STAGING_DIR}/deneb-api.init" "${STAGING_DIR}/deneb-web.init" "${STAGING_DIR}/deneb-mdns.init" "${STAGING_DIR}/deneb-printsvc.init" "${STAGING_DIR}/digitalfactory.init"
chmod 0755 "${STAGING_DIR}/deneb-printsvc-smoke" "${STAGING_DIR}/deneb-printsvc-smoke-verify" "${STAGING_DIR}/deneb-printsvc-smoke-compare" "${STAGING_DIR}/deneb-printsvc-smoke-selftest" "${STAGING_DIR}/deneb-printsvc-stability" "${STAGING_DIR}/deneb-active-physical-soak-runner" "${STAGING_DIR}/deneb-printsvc-stock-baseline" "${STAGING_DIR}/deneb-printsvc-cli-selftest" "${STAGING_DIR}/deneb-printsvc-init-selftest" "${STAGING_DIR}/deneb-printsvc-native-audit" "${STAGING_DIR}/deneb-printsvc-native-audit-selftest" "${STAGING_DIR}/deneb-printsvc-integration-audit"
chmod 0755 "${STAGING_DIR}/deneb-runtime-inventory"

if [ "$DENEB_RELEASE_CHANNEL" != "experimental" ]; then
    if [ -n "${DENEB_PRINTSVC_NATIVE_EVIDENCE_SUMMARIES:-}" ]; then
        echo "ERROR: pass supplemental native evidence paths as arguments after the six binaries" >&2
        exit 1
    fi
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
    for evidence_summary in "$@"; do
        if [ ! -f "$evidence_summary" ]; then
            echo "ERROR: native printsvc evidence summary not found: $evidence_summary" >&2
            exit 1
        fi
    done
    "${STAGING_DIR}/deneb-printsvc-smoke-verify" --stock --resources "$PRINTSVC_STOCK_SUMMARY"
    if [ "$#" -gt 0 ]; then
        "${STAGING_DIR}/deneb-printsvc-smoke-verify" --native --idle --restart --boot-sync --client-proof --firmware-proof --complete-job --resources "$PRINTSVC_NATIVE_SUMMARY"
        for evidence_summary in "$@"; do
            "${STAGING_DIR}/deneb-printsvc-smoke-verify" --native "$evidence_summary"
        done
        "${STAGING_DIR}/deneb-printsvc-smoke-compare" --require-reduction "$PRINTSVC_STOCK_SUMMARY" "$PRINTSVC_NATIVE_SUMMARY" "$@"
    else
        "${STAGING_DIR}/deneb-printsvc-smoke-verify" --full "$PRINTSVC_NATIVE_SUMMARY"
        "${STAGING_DIR}/deneb-printsvc-smoke-compare" --require-reduction "$PRINTSVC_STOCK_SUMMARY" "$PRINTSVC_NATIVE_SUMMARY"
    fi
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
sed -n '1,/^\*\/$/p' "${SCRIPT_DIR}/lib/lvgl/src/libs/lodepng/lodepng.c" > "${STAGING_DIR}/LODEPNG_LICENSE.txt"
cp "${REPO_ROOT}/notices/libzmq-4.3.5-NOTICE.txt" "${STAGING_DIR}/LIBZMQ_NOTICE.txt"
cp "${REPO_ROOT}/notices/MPL-2.0.txt" "${STAGING_DIR}/MPL-2.0.txt"
cp "${REPO_ROOT}/notices/MBEDTLS-2.28.8-LICENSE.txt" "${STAGING_DIR}/MBEDTLS_LICENSE.txt"
cp "${REPO_ROOT}/notices/LIGHTTPD-1.4.85-LICENSE.txt" "${STAGING_DIR}/LIGHTTPD_LICENSE.txt"
if [ -n "$package_runtime_key" ]; then
    cp "${REPO_ROOT}/notices/MUSL-1.2.5-COPYRIGHT.txt" "${STAGING_DIR}/MUSL_COPYRIGHT.txt"
    cp "${REPO_ROOT}/notices/GCC-14.3.0-COPYING3.txt" "${STAGING_DIR}/GCC_COPYING3.txt"
    cp "${REPO_ROOT}/notices/GCC-14.3.0-COPYING.RUNTIME.txt" "${STAGING_DIR}/GCC_COPYING.RUNTIME.txt"
fi

# Create manifest
cat > "${STAGING_DIR}/manifest.txt" <<EOF
package: ${PACKAGE_NAME}
version: ${VERSION}
channel: ${DENEB_RELEASE_CHANNEL}
date: ${PACKAGE_DATE}
native_printsvc: experimental
native_printsvc_release_gate: ${PRINTSVC_RELEASE_GATE}
contents:
  deneb-ui          - LVGL touchscreen UI binary (MIPS)
  deneb-ui.init     - OpenWrt procd init script
  deneb-api         - Local REST API and web session service (MIPS)
  deneb-api digital-factory - Internal touchscreen Digital Factory command mode
  deneb-dfsvc       - Native Digital Factory cloud connector service (MIPS)
  deneb-mdns        - Lightweight mDNS advertiser for Cura local discovery (MIPS)
  deneb-printsvc    - Native print service replacement (MIPS)
  deneb-printsvc-smoke - Native print service smoke/resource harness
  deneb-printsvc-smoke-verify - Shell verifier for smoke summary evidence
  deneb-printsvc-smoke-compare - Shell stock/native smoke summary comparator
  deneb-printsvc-smoke-selftest - Install-time shell synthetic verifier/comparator selftest
  deneb-printsvc-stability - Repeated native stability/resource harness
  deneb-active-physical-soak-runner - Time-bounded active heat/motion/job soak runner
  deneb-printsvc-stock-baseline - Guarded stock-driver baseline collector
  deneb-printsvc-cli-selftest - Install-time shell native print-service binary CLI selftest
  deneb-printsvc-init-selftest - Install-time shell native init handoff selftest
  deneb-printsvc-native-audit - Install-time shell static/package de-Python regression audit
  deneb-printsvc-native-audit-selftest - Install-time shell negative-fixture selftest for native audit
  deneb-printsvc-integration-audit - Install-time shell static client-integration ownership audit
  deneb-runtime-inventory - Device-side remaining runtime dependency inventory helper
  deneb-printsvc-macros/ - Deneb-owned native print-service macro defaults
  lighttpd          - Static web server and API reverse proxy (MIPS)
  deneb-api.init    - OpenWrt procd init script for deneb-api
  deneb-web.init    - OpenWrt procd init script for lighttpd
  deneb-mdns.init   - OpenWrt procd init script for deneb-mdns
  deneb-printsvc.init - OpenWrt procd init script for deneb-printsvc
  digitalfactory.init - OpenWrt procd init script for native deneb-dfsvc
  lighttpd.conf     - Deneb web server configuration
  www/              - Static Deneb web UI assets
  update.sh         - Installer script
  *.json            - Bundled Deneb locale files
  LICENSE           - Deneb MPL-2.0 project license
  THIRD_PARTY_NOTICES.md - Third-party dependency notices
  LVGL_LICENCE.txt  - LVGL MIT license notice
  LVGL_LICENSE_SPRINTF.txt - LVGL bundled printf helper MIT license notice
  LVGL_LICENSE_TLSF.txt - LVGL bundled TLSF helper BSD-style license notice
  LODEPNG_LICENSE.txt - LodePNG zlib-style license notice
  LIBZMQ_NOTICE.txt - libzmq MPL-2.0 notice and source location
  MPL-2.0.txt       - Mozilla Public License 2.0 text for libzmq
  MBEDTLS_LICENSE.txt - mbedTLS 2.28.8 dual-license text
  LIGHTTPD_LICENSE.txt - lighttpd 1.4.85 BSD license notice
  Verified OpenWrt toolchain builds also include MUSL_COPYRIGHT.txt,
  GCC_COPYING3.txt, and GCC_COPYING.RUNTIME.txt.
  no Python driver files are packaged; native deneb-printsvc owns marlindriver
  manifest.txt      - This file
EOF
if [ -n "$package_runtime_key" ]; then
    printf 'mips_runtime_receipt: %s\n' "$package_runtime_key" >> "${STAGING_DIR}/manifest.txt"
fi

if find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' -o -name 'deneb-df-bridge.py' \) \
    -print | grep . >/dev/null 2>&1; then
    echo "ERROR: Python driver artifact found in Deneb package staging" >&2
    find "$STAGING_DIR" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' -o -name 'deneb-df-bridge.py' \) \
        -print >&2
    exit 1
fi

"${STAGING_DIR}/deneb-printsvc-smoke-selftest"
"${STAGING_DIR}/deneb-printsvc-stability" --selftest
DENEB_REPO_ROOT="$REPO_ROOT" sh "${REPO_ROOT}/tools/deneb-printsvc-release-gate-selftest.sh"
"${STAGING_DIR}/deneb-printsvc-native-audit" --source "$REPO_ROOT"
"${STAGING_DIR}/deneb-printsvc-native-audit" --package-dir "$STAGING_DIR"
"${STAGING_DIR}/deneb-printsvc-native-audit-selftest"
"${STAGING_DIR}/deneb-printsvc-integration-audit" --source "$REPO_ROOT"
"${STAGING_DIR}/deneb-printsvc-integration-audit" --package-dir "$STAGING_DIR"
sh "${REPO_ROOT}/tools/deneb-printsvc-integration-audit-selftest.sh"
DENEB_REPO_ROOT="$STAGING_DIR" \
DENEB_PRINTSVC_INIT="${STAGING_DIR}/deneb-printsvc.init" \
DENEB_INSTALLER="${STAGING_DIR}/update.sh" \
    "${STAGING_DIR}/deneb-printsvc-init-selftest"

# Run stock menu prune selftest (host fixture — no target hardware needed)
sh "${REPO_ROOT}/tools/deneb-stock-menu-prune-selftest.sh"

# Create tar-backed .deneb package for the Deneb USB update lane
# A release wrapper publishes the matching checksum only after its additional
# archive audits pass. Invalidate any earlier success marker before replacing
# the package so a failed rebuild cannot inherit a stale digest.
rm -f "$OUTPUT_IMG.sha256" "$OUTPUT_IMG.sbom.cdx.json"
cd "$STAGING_DIR"
umask "$OUTPUT_UMASK"
export LC_ALL=C
set --
if [ -n "$package_runtime_key" ]; then
    set -- MUSL_COPYRIGHT.txt GCC_COPYING3.txt GCC_COPYING.RUNTIME.txt
fi
TAR_OPTIONS= tar --format=gnu --sort=name --mtime="@$PACKAGE_EPOCH" --owner=0 --group=0 --numeric-owner -cf "$OUTPUT_IMG" \
    deneb-ui deneb-ui.init update.sh ./*.json LICENSE THIRD_PARTY_NOTICES.md LVGL_LICENCE.txt LVGL_LICENSE_SPRINTF.txt LVGL_LICENSE_TLSF.txt LODEPNG_LICENSE.txt LIBZMQ_NOTICE.txt MPL-2.0.txt MBEDTLS_LICENSE.txt LIGHTTPD_LICENSE.txt "$@" manifest.txt \
    deneb-api deneb-dfsvc deneb-mdns deneb-printsvc deneb-printsvc-smoke deneb-printsvc-smoke-verify deneb-printsvc-smoke-compare deneb-printsvc-smoke-selftest deneb-printsvc-stability deneb-active-physical-soak-runner deneb-printsvc-stock-baseline deneb-printsvc-cli-selftest deneb-printsvc-init-selftest deneb-printsvc-native-audit deneb-printsvc-native-audit-selftest deneb-printsvc-integration-audit \
    deneb-runtime-inventory \
    deneb-printsvc-macros lighttpd deneb-api.init deneb-web.init deneb-mdns.init deneb-printsvc.init digitalfactory.init lighttpd.conf www

tar tf "$OUTPUT_IMG" > "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)update.sh$' "${STAGING_DIR}/package-files.txt"
! grep -Eq '(^|/)deneb-df-bridge$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-dfsvc$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc.init$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)digitalfactory.init$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-verify$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-compare$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-smoke-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-stability$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-active-physical-soak-runner$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-stock-baseline$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-cli-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-init-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-native-audit$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-native-audit-selftest$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-printsvc-integration-audit$' "${STAGING_DIR}/package-files.txt"
grep -Eq '(^|/)deneb-runtime-inventory$' "${STAGING_DIR}/package-files.txt"
tar -xOf "$OUTPUT_IMG" manifest.txt > "${STAGING_DIR}/package-manifest.txt"
grep -Eq "^channel: ${DENEB_RELEASE_CHANNEL}$" "${STAGING_DIR}/package-manifest.txt"
grep -Eq '^native_printsvc: experimental$' "${STAGING_DIR}/package-manifest.txt"
grep -Fx "native_printsvc_release_gate: ${PRINTSVC_RELEASE_GATE}" "${STAGING_DIR}/package-manifest.txt" >/dev/null
"${STAGING_DIR}/deneb-printsvc-native-audit" --archive "$OUTPUT_IMG"
"${STAGING_DIR}/deneb-printsvc-integration-audit" --archive "$OUTPUT_IMG"
if grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' "${STAGING_DIR}/package-files.txt"; then
    echo "ERROR: Python driver artifact found in Deneb package archive" >&2
    exit 1
fi
rm -f "${STAGING_DIR}/package-files.txt" "${STAGING_DIR}/package-manifest.txt"

if [ "${DENEB_PACKAGE_PATH_ENCODING:-}" = base64 ]; then
    encoded_package_path=$(printf '%s' "$OUTPUT_IMG" | base64 -w 0)
    printf 'Package path (base64): %s\n' "$encoded_package_path"
else
    echo "Package: ${OUTPUT_IMG}"
fi
echo "Size: $(wc -c < "$OUTPUT_IMG") bytes"
echo "Install: Copy to USB, use Deneb Maintenance > Update Firmware"
