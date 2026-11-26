#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Write the firmware SBOM for the selected release inputs.

set -eu
if [ "$#" -ne 4 ] && [ "$#" -ne 6 ]; then
    echo "usage: $0 PACKAGE ZMQ_ROOT LIGHTTPD_ROOT MBEDTLS_ROOT [DENEB_REV LVGL_REV]" >&2
    exit 1
fi
package=$1
zmq_root=$2
lighttpd_root=$3
mbedtls_root=$4
repo_root=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$repo_root/tools/native-dependency-pins.sh"

if [ "$#" -eq 6 ]; then
    # The Windows wrapper verifies these revisions with Git on the checkout host.
    revision=$5
    lvgl_revision=$6
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
    {"ref": "deneb", "dependsOn": ["libzmq", "lighttpd", "mbedtls", "lvgl"]}
  ]
}
EOF
chmod 0644 "$temp"
mv -fT -- "$temp" "$output"
printf 'Wrote native dependency SBOM %s\n' "$output"
