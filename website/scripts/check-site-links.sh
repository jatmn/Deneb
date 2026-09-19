#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Validate site-root wiki and image links in website/content.
# Source mode maps /docs and /images onto content files and hugo.yaml mounts.
# --public DIR also requires the matching built route (and fragment) under DIR.

set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
public_dir=""

while (($#)); do
    case "$1" in
        --root)
            (($# >= 2)) || { printf '%s\n' "check-site-links.sh: --root needs a directory" >&2; exit 2; }
            root="$(cd -- "$2" && pwd)"
            shift 2
            ;;
        --public)
            (($# >= 2)) || { printf '%s\n' "check-site-links.sh: --public needs a directory" >&2; exit 2; }
            public_dir="$(cd -- "$2" && pwd)"
            shift 2
            ;;
        *)
            printf '%s\n' "check-site-links.sh: unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

content="${root}/content"
repo="$(cd -- "${root}/.." && pwd)"
failures=0

if [[ ! -d "$content" ]]; then
    printf '%s\n' "missing website content directory: ${content}" >&2
    exit 1
fi

if [[ -n "$public_dir" && ! -d "$public_dir" ]]; then
    printf '%s\n' "missing built site directory: ${public_dir}" >&2
    exit 1
fi

path_ok() {
    local path="$1"
    [[ "$path" == /* ]] || return 1
    [[ "$path" != //* ]] || return 1
    [[ "$path" != *..* ]] || return 1
    return 0
}

source_exists() {
    local path="$1"
    local slug name

    if [[ "$path" == "/" ]]; then
        [[ -f "${content}/_index.md" ]]
        return
    fi

    path="${path%/}"
    case "$path" in
        /docs)
            [[ -f "${content}/docs/_index.md" ]]
            ;;
        /docs/*)
            slug="${path#/docs/}"
            [[ -f "${content}/docs/${slug}.md" ]] || [[ -f "${content}/docs/${slug}/_index.md" ]]
            ;;
        /images/screens/*)
            name="${path#/images/screens/}"
            [[ -f "${repo}/docs/touchscreen-screens/${name}" ]]
            ;;
        /images/branding/*)
            name="${path#/images/branding/}"
            [[ -f "${repo}/assets/branding/${name}" ]]
            ;;
        *)
            [[ -f "${root}/static${path}" ]]
            ;;
    esac
}

public_file() {
    local path="$1"
    local trimmed="${path%/}"

    if [[ "$path" == "/" ]]; then
        if [[ -f "${public_dir}/index.html" ]]; then
            printf '%s\n' "${public_dir}/index.html"
            return 0
        fi
        return 1
    fi
    if [[ -f "${public_dir}${trimmed}/index.html" ]]; then
        printf '%s\n' "${public_dir}${trimmed}/index.html"
        return 0
    fi
    if [[ -f "${public_dir}${trimmed}.html" ]]; then
        printf '%s\n' "${public_dir}${trimmed}.html"
        return 0
    fi
    if [[ -f "${public_dir}${path}" ]]; then
        printf '%s\n' "${public_dir}${path}"
        return 0
    fi
    if [[ -f "${public_dir}${trimmed}" ]]; then
        printf '%s\n' "${public_dir}${trimmed}"
        return 0
    fi
    return 1
}

fragment_exists() {
    local html="$1"
    local frag="$2"
    grep -Eq "id=${frag}([[:space:]>\"'])|id=\"${frag}\"|id='${frag}'" "$html"
}

emit_targets() {
    local file="$1"
    grep -oE '\[[^][]*\]\(/[^)]+\)' "$file" 2>/dev/null | sed 's/.*](//; s/)$//' || true
    grep -oE '(href|src|link)="/[^"]+"' "$file" 2>/dev/null | sed 's/.*="//; s/"$//' || true
}

relpath() {
    local file="$1"
    printf '%s\n' "${file#"${repo}/"}"
}

fail() {
    printf '%s\n' "$1" >&2
    failures=$((failures + 1))
}

while IFS= read -r -d '' file; do
    while IFS= read -r raw; do
        [[ -n "$raw" ]] || continue
        path="${raw%%#*}"
        path="${path%%\?*}"
        frag=""
        if [[ "$raw" == *#* ]]; then
            frag="${raw#*#}"
            frag="${frag%%\?*}"
        fi
        path_ok "$path" || continue

        if ! source_exists "$path"; then
            fail "$(relpath "$file"): missing site-root target '${path}'"
            continue
        fi

        if [[ -n "$public_dir" ]]; then
            html=""
            if ! html="$(public_file "$path")"; then
                fail "$(relpath "$file"): site-root target '${path}' is not in the built site"
                continue
            fi
            if [[ -n "$frag" ]] && ! fragment_exists "$html" "$frag"; then
                fail "$(relpath "$file"): missing fragment '#${frag}' for '${path}'"
            fi
        fi
    done < <(emit_targets "$file")
done < <(find "$content" -type f \( -name '*.md' -o -name '*.html' \) -print0)

if ((failures > 0)); then
    printf '%s\n' "Site-root link check: ${failures} missing target(s)" >&2
    exit 1
fi

printf '%s\n' "Site-root wiki links: PASS"
