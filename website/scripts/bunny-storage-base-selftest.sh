#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0

set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
helper="${root}/scripts/bunny-storage-base.sh"

check() {
    local got want
    got="$(bash "$helper" "$1" "$2")"
    want="$3"
    if [[ "$got" != "$want" ]]; then
        echo "bunny-storage-base.sh '$1' '$2' => '$got' (want '$want')" >&2
        exit 1
    fi
}

check "https://storage.bunnycdn.com" "deneb3d" "https://storage.bunnycdn.com/deneb3d"
check "https://storage.bunnycdn.com/" "deneb3d" "https://storage.bunnycdn.com/deneb3d"
check "https://storage.bunnycdn.com/deneb3d" "deneb3d" "https://storage.bunnycdn.com/deneb3d"
check "https://storage.bunnycdn.com/deneb3d/" "deneb3d" "https://storage.bunnycdn.com/deneb3d"
check "https://ny.storage.bunnycdn.com" "deneb3d" "https://ny.storage.bunnycdn.com/deneb3d"
check "https://ny.storage.bunnycdn.com/deneb3d" "deneb3d" "https://ny.storage.bunnycdn.com/deneb3d"

echo "Bunny storage base self-test: PASS"
