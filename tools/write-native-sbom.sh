#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Write a CycloneDX inventory of the pinned third-party firmware sources.

set -eu
package=${1:?usage: sh tools/write-native-sbom.sh /path/to/Deneb_Update_VERSION.deneb}
repo_root=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=tools/native-dependency-pins.sh
# shellcheck disable=SC1091
. "$repo_root/tools/native-dependency-pins.sh"

revision=$(git -C "$repo_root" rev-parse HEAD)
lvgl_path=ui/lib/lvgl
lvgl_revision=$(git -C "$repo_root" ls-tree HEAD -- "$lvgl_path" | awk '$1 == "160000" {print $3}')
if [ -z "$lvgl_revision" ] || [ "$(git -C "$repo_root/$lvgl_path" rev-parse HEAD)" != "$lvgl_revision" ]; then
    echo "LVGL checkout does not match the gitlink in HEAD. Run sh tools/setup-lvgl.sh." >&2
    exit 1
fi
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
      "version": "$ZMQ_VERSION",
      "purl": "pkg:github/zeromq/libzmq@v$ZMQ_VERSION",
      "licenses": [{"license": {"id": "MPL-2.0"}}],
      "externalReferences": [{
        "type": "distribution", "url": "$ZMQ_URL",
        "hashes": [{"alg": "SHA-256", "content": "$ZMQ_SHA256"}]
      }]
    },
    {
      "type": "application",
      "bom-ref": "lighttpd",
      "name": "lighttpd",
      "version": "$LIGHTTPD_VERSION",
      "purl": "pkg:generic/lighttpd@$LIGHTTPD_VERSION",
      "licenses": [{"license": {"id": "BSD-3-Clause"}}],
      "externalReferences": [{
        "type": "distribution", "url": "$LIGHTTPD_URL",
        "hashes": [{"alg": "SHA-256", "content": "$LIGHTTPD_SHA256"}]
      }]
    },
    {
      "type": "library",
      "bom-ref": "mbedtls",
      "name": "Mbed TLS",
      "version": "$MBEDTLS_VERSION",
      "purl": "pkg:github/mbed-tls/mbedtls@v$MBEDTLS_VERSION",
      "licenses": [{"expression": "Apache-2.0 OR GPL-2.0-or-later"}],
      "externalReferences": [{
        "type": "distribution", "url": "$MBEDTLS_URL",
        "hashes": [{"alg": "SHA-256", "content": "$MBEDTLS_SHA256"}]
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
    {"ref": "deneb", "dependsOn": ["libzmq", "lighttpd", "mbedtls", "lvgl"]}
  ]
}
EOF
chmod 0644 "$temp"
mv -f -- "$temp" "$output"
printf 'Wrote native dependency SBOM %s\n' "$output"
