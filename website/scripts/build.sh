#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Build the public Deneb site. Requires Hugo extended and Go (for Hugo modules).

set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
hugo_version="$(tr -d '[:space:]' < "${root}/vendor-pins/hugo.version")"
hugo_bin="${HUGO_BIN:-}"

if [[ -z "$hugo_bin" ]]; then
    if command -v hugo >/dev/null 2>&1; then
        hugo_bin="$(command -v hugo)"
    else
        printf '%s\n' "hugo not found; set HUGO_BIN or install Hugo extended ${hugo_version}" >&2
        exit 1
    fi
fi

got="$("$hugo_bin" version)"
if [[ "$got" != *"${hugo_version}"* ]] || [[ "$got" != *"extended"* ]]; then
    printf '%s\n' "need Hugo extended ${hugo_version}; got: ${got}" >&2
    exit 1
fi

bash "${root}/scripts/prepare-assets.sh"
(
    cd "$root"
    HUGO_ENVIRONMENT=production "$hugo_bin" --gc --minify
)
bash "${root}/scripts/check-site-links.sh" --public "${root}/public"
