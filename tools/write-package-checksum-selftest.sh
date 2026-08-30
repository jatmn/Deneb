#!/bin/sh
# SPDX-License-Identifier: MPL-2.0

set -eu

repo_root=$(cd "$(dirname "$0")/.." && pwd)
work_dir=$(mktemp -d)
cleanup() {
    rm -rf "$work_dir"
}
trap cleanup EXIT

package="$work_dir/Deneb_Update_checksum-selftest.deneb"
checksum="$package.sha256"
printf 'verified package fixture\n' > "$package"

sh "$repo_root/tools/write-package-checksum.sh" "$package" >/dev/null
[ -f "$checksum" ]
(cd "$work_dir" && sha256sum --check "$(basename "$checksum")") >/dev/null
[ -z "$(find "$work_dir" -maxdepth 1 -type f -name '.*')" ]

printf 'tampered after publication\n' >> "$package"
if (cd "$work_dir" && sha256sum --check --status "$(basename "$checksum")"); then
    echo "Checksum unexpectedly accepted a modified package" >&2
    exit 1
fi

printf 'Package checksum self-test: PASS\n'
