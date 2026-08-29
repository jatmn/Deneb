#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

package=${1:?usage: tools/write-package-checksum.sh PACKAGE.deneb}
case "$package" in
    /*) ;;
    *) package=$(realpath "$package") ;;
esac

[ -s "$package" ] || {
    echo "Package not found or empty: $package" >&2
    exit 1
}

package_name=$(basename "$package")
case "$package_name" in
    Deneb_Update_*.deneb) ;;
    *)
        echo "Refusing to publish a checksum for an unexpected package name: $package_name" >&2
        exit 1
        ;;
esac

package_dir=$(dirname "$package")
checksum="$package.sha256"
temp_checksum=$(mktemp "$package_dir/.${package_name}.sha256.XXXXXX")
cleanup() {
    rm -f "$temp_checksum"
}
trap cleanup EXIT

hash=$(sha256sum "$package" | awk '{print $1}')
printf '%s  %s\n' "$hash" "$package_name" > "$temp_checksum"
(cd "$package_dir" && sha256sum --check --status "$(basename "$temp_checksum")") || {
    echo "Generated package checksum failed verification: $package" >&2
    exit 1
}
chmod 0644 "$temp_checksum"
mv -f "$temp_checksum" "$checksum"
temp_checksum=

printf 'Published checksum: %s\n' "$checksum"
printf 'SHA256 %s\n' "$hash"
