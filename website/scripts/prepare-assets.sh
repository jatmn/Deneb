#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Download the pinned FlexSearch runtime for the Hextra search UI.
# The file is not committed; GitHub Actions and local builds fetch it.

set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
pin="${root}/vendor-pins/flexsearch.bundle.min.js.sha256"
vendor="${root}/assets/js/vendor"
url="https://cdn.jsdelivr.net/npm/flexsearch@0.8.143/dist/flexsearch.bundle.min.js"

expected="$(awk '{print $1}' "$pin")"
if [[ ${#expected} -ne 64 ]]; then
    printf '%s\n' "invalid FlexSearch pin in ${pin}" >&2
    exit 1
fi

mkdir -p "$vendor"
tmp="$(mktemp "${TMPDIR:-/tmp}/flexsearch.XXXXXX")"
trap 'rm -f "$tmp"' EXIT

curl -fsSL "$url" -o "$tmp"
actual="$(sha256sum "$tmp" | awk '{print $1}')"
if [[ "$actual" != "$expected" ]]; then
    printf '%s\n' "FlexSearch checksum mismatch: got ${actual}, expected ${expected}" >&2
    exit 1
fi

mv "$tmp" "${vendor}/flexsearch.bundle.min.js"
trap - EXIT
printf '%s\n' "FlexSearch 0.8.143 ready at ${vendor}/flexsearch.bundle.min.js"
