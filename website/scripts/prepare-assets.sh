#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Stage the locked FlexSearch bundle for the Hextra search UI.

set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
pin="${root}/vendor-pins/flexsearch.bundle.min.js.sha256"
vendor="${root}/assets/js/vendor"
expected="$(awk '{print $1}' "$pin")"
if [[ ${#expected} -ne 64 ]]; then
    printf '%s\n' "invalid FlexSearch pin in ${pin}" >&2
    exit 1
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/flexsearch.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cp "${root}/package.json" "${root}/package-lock.json" "$work/"
npm ci --prefix "$work" --ignore-scripts --no-audit --no-fund
bundle="${work}/node_modules/flexsearch/dist/flexsearch.bundle.min.js"
actual="$(sha256sum "$bundle" | awk '{print $1}')"
if [[ "$actual" != "$expected" ]]; then
    printf '%s\n' "FlexSearch checksum mismatch: got ${actual}, expected ${expected}" >&2
    exit 1
fi

mkdir -p "$vendor"
cp "$bundle" "${vendor}/flexsearch.bundle.min.js"
printf '%s\n' "FlexSearch ready at ${vendor}/flexsearch.bundle.min.js"
