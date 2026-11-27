#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Write the firmware SBOM for the selected release inputs.

set -eu
if [ "$#" -ne 5 ] && [ "$#" -ne 7 ]; then
    echo "usage: $0 PACKAGE ZMQ_ROOT LIGHTTPD_ROOT MBEDTLS_ROOT TOOLCHAIN_ROOT [DENEB_REV LVGL_REV]" >&2
    exit 1
fi
package=$1
zmq_root=$2
lighttpd_root=$3
mbedtls_root=$4
toolchain_root=$5
repo_root=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$repo_root/tools/native-dependency-pins.sh"

if [ "$#" -eq 7 ]; then
    # The Windows wrapper verifies these revisions with Git on the checkout host.
    revision=$6
    lvgl_revision=$7
else
    revision=$(git -C "$repo_root" rev-parse --verify HEAD)
    lvgl_path=ui/lib/lvgl
    lvgl_revision=$(git -C "$repo_root" ls-tree HEAD -- "$lvgl_path" | awk '$1 == "160000" {print $3}')
    if [ -z "$lvgl_revision" ] || [ "$(git -C "$repo_root/$lvgl_path" rev-parse --verify HEAD)" != "$lvgl_revision" ]; then
        echo "LVGL checkout does not match the gitlink in HEAD. Run sh tools/setup-lvgl.sh." >&2
        exit 1
    fi
fi

is_commit_hash() {
    [ "${#1}" -eq 40 ] || return 1
    case "$1" in *[!0-9a-f]*) return 1 ;; esac
}

if ! is_commit_hash "$revision" || ! is_commit_hash "$lvgl_revision"; then
    echo "Invalid Git revisions for the release SBOM." >&2
    exit 1
fi

source_metadata() {
    if ! native_dependency_verified "$1" "$2"; then
        printf '%s\n' '      "properties": [{"name": "deneb:source-provenance", "value": "unverified"}]'
        return
    fi
    cat <<EOF
      "version": "$3",
      "purl": "$4",
      "licenses": [$5],
      "externalReferences": [{
        "type": "distribution", "url": "$6",
        "hashes": [{"alg": "SHA-256", "content": "$7"}]
      }]
EOF
}

toolchain_verified=false
if runtime_key=$(mips_toolchain_key "$toolchain_root") &&
    manifest=$(tar -xOf "$package" manifest.txt 2>/dev/null) &&
    printf '%s\n' "$manifest" | grep -Fxq "mips_runtime_receipt: $runtime_key"; then
    toolchain_verified=true
fi

runtime_metadata() {
    if ! "$toolchain_verified"; then
        printf '%s\n' '      "properties": [{"name": "deneb:source-provenance", "value": "unverified"}]'
        return
    fi
    cat <<EOF
      "version": "$1",
      "purl": "$2",
      "licenses": [$3],
      "properties": [{"name": "deneb:openwrt-release", "value": "$MIPS_TOOLCHAIN_RELEASE"}],
      "externalReferences": [{
        "type": "distribution", "url": "$4",
        "hashes": [{"alg": "SHA-256", "content": "$5"}]
      }, {
        "type": "distribution", "url": "$MIPS_TOOLCHAIN_URL",
        "hashes": [{"alg": "SHA-256", "content": "$MIPS_TOOLCHAIN_SHA256"}]
      }, {
        "type": "vcs", "url": "https://github.com/openwrt/openwrt/tree/$MIPS_TOOLCHAIN_REVISION/toolchain"
      }]
EOF
}

gcc_runtime_license='{"expression": "GPL-3.0-or-later WITH GCC-exception-3.1"}'

package_hash=$(sha256sum < "$package")
package_hash=${package_hash%% *}
output="$package.sbom.cdx.json"
temp=$(mktemp "$output.XXXXXX")
trap 'rm -f -- "$temp"' EXIT
trap 'exit 1' HUP INT TERM

cat > "$temp" <<EOF
{
  "bomFormat": "CycloneDX",
  "specVersion": "1.6",
  "version": 1,
  "metadata": {
    "component": {
      "type": "firmware",
      "bom-ref": "deneb",
      "name": "Deneb",
      "version": "$revision",
      "hashes": [{"alg": "SHA-256", "content": "$package_hash"}]
    }
  },
  "components": [
    {
      "type": "library",
      "bom-ref": "libzmq",
      "name": "libzmq",
$(source_metadata libzmq "$zmq_root" "$ZMQ_VERSION" "pkg:github/zeromq/libzmq@v$ZMQ_VERSION" \
    '{"license": {"id": "MPL-2.0"}}' "$ZMQ_URL" "$ZMQ_SHA256")
    },
    {
      "type": "application",
      "bom-ref": "lighttpd",
      "name": "lighttpd",
$(source_metadata lighttpd "$lighttpd_root" "$LIGHTTPD_VERSION" "pkg:generic/lighttpd@$LIGHTTPD_VERSION" \
    '{"license": {"id": "BSD-3-Clause"}}' "$LIGHTTPD_URL" "$LIGHTTPD_SHA256")
    },
    {
      "type": "library",
      "bom-ref": "mbedtls",
      "name": "Mbed TLS",
$(source_metadata mbedtls "$mbedtls_root" "$MBEDTLS_VERSION" "pkg:github/mbed-tls/mbedtls@v$MBEDTLS_VERSION" \
    '{"expression": "Apache-2.0 OR GPL-2.0-or-later"}' "$MBEDTLS_URL" "$MBEDTLS_SHA256")
    },
    {
      "type": "library",
      "bom-ref": "musl",
      "name": "musl",
$(runtime_metadata "$MIPS_TOOLCHAIN_MUSL_VERSION" "pkg:generic/musl@$MIPS_TOOLCHAIN_MUSL_VERSION" \
    '{"license": {"id": "MIT"}}' "$MUSL_URL" "$MUSL_SHA256")
    },
    {
      "type": "library",
      "bom-ref": "libstdc++",
      "name": "libstdc++",
$(runtime_metadata "$MIPS_TOOLCHAIN_GCC_VERSION" "pkg:generic/libstdc%2B%2B@$MIPS_TOOLCHAIN_GCC_VERSION" \
    "$gcc_runtime_license" "$GCC_URL" "$GCC_SHA256")
    },
    {
      "type": "library",
      "bom-ref": "libgcc",
      "name": "libgcc",
      "description": "GCC support and exception unwinding runtime",
$(runtime_metadata "$MIPS_TOOLCHAIN_GCC_VERSION" "pkg:generic/libgcc@$MIPS_TOOLCHAIN_GCC_VERSION" \
    "$gcc_runtime_license" "$GCC_URL" "$GCC_SHA256")
    },
    {
      "type": "library",
      "bom-ref": "lodepng",
      "name": "LodePNG",
      "version": "20261102",
      "purl": "pkg:github/lvandeve/lodepng@7b2893e35891c0c85bee5595caafaf3c2044b409",
      "licenses": [{"license": {"id": "Zlib"}}],
      "externalReferences": [{
        "type": "distribution",
        "url": "https://codeload.github.com/lvandeve/lodepng/tar.gz/7b2893e35891c0c85bee5595caafaf3c2044b409",
        "hashes": [{"alg": "SHA-256", "content": "e0b571062acdbe187fd4e8b0ff46f6e7d1b634457ff1a4b383aaac4c78bfd59b"}]
      }]
    },
    {
      "type": "library",
      "bom-ref": "lvgl",
      "name": "LVGL",
      "version": "$lvgl_revision",
      "purl": "pkg:github/lvgl/lvgl@$lvgl_revision",
      "licenses": [{"license": {"id": "MIT"}}],
      "externalReferences": [{
        "type": "vcs", "url": "https://github.com/lvgl/lvgl/tree/$lvgl_revision"
      }]
    }
  ],
  "dependencies": [
    {"ref": "deneb", "dependsOn": ["libzmq", "lighttpd", "mbedtls", "musl", "libstdc++", "libgcc", "lodepng", "lvgl"]}
  ]
}
EOF
chmod 0644 "$temp"
mv -fT -- "$temp" "$output"
printf 'Wrote native dependency SBOM %s\n' "$output"
