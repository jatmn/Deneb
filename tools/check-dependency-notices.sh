#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Fail when a Dependabot-managed lockfile or pin changes without the matching
# provenance, notice, and REUSE updates that SECURITY.md requires in the same
# PR. Versions come from the authoritative lock or pin files, so a bump that
# touches only the lockfile fails here.
#
# usage: tools/check-dependency-notices.sh [--root DIR]
#        tools/check-dependency-notices.sh --selftest

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$SCRIPT_DIR/.." && pwd)"

usage() {
    printf '%s\n' "usage: $0 [--root DIR] | --selftest" >&2
    exit 2
}

failures=0
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    failures=$((failures + 1))
}

# npm lockfile v3: the installed version of a top-level package.
npm_locked() {
    node -e '
        const lock = require(process.argv[1]);
        const entry = (lock.packages || {})["node_modules/" + process.argv[2]];
        if (!entry || !entry.version) process.exit(1);
        console.log(entry.version);
    ' "$1" "$2"
}

# The Markdown table row whose first cell is the given label.
provenance_row() {
    awk -v label="$1" -F'|' '
        { cell = $2; gsub(/^[ \t]+|[ \t]+$/, "", cell) }
        cell == label { print; found = 1; exit }
        END { exit !found }
    ' "$root/docs/SOURCE_PROVENANCE.md"
}

# require_text FILE TEXT DEPENDENCY: TEXT must appear in FILE.
require_text() {
    if ! grep -Fq -- "$2" "$root/$1"; then
        fail "$3: $1 does not mention '$2'; update it with the lockfile"
    fi
}

# require_row LABEL TEXT DEPENDENCY: the provenance row must contain TEXT.
require_row() {
    local row
    if ! row=$(provenance_row "$1"); then
        fail "$3: docs/SOURCE_PROVENANCE.md has no '$1' row"
        return
    fi
    case "$row" in
        *"$2"*) ;;
        *) fail "$3: docs/SOURCE_PROVENANCE.md '$1' row does not mention '$2'; update it with the lockfile" ;;
    esac
}

# require_notice PREFIX VERSION DEPENDENCY: exactly the current notice file
# exists, and THIRD_PARTY_NOTICES.md and REUSE.toml name it.
require_notice() {
    local notice="notices/$1-$2-LICENSE.txt" other
    if [ ! -f "$root/$notice" ]; then
        fail "$3: missing $notice; copy the license from the locked $2 package"
    fi
    for other in "$root"/notices/"$1"-*-LICENSE.txt; do
        [ -e "$other" ] || continue
        if [ "${other#"$root/"}" != "$notice" ]; then
            fail "$3: stale notice ${other#"$root/"}; rename it to $notice"
        fi
    done
    require_text THIRD_PARTY_NOTICES.md "$notice" "$3"
    require_text REUSE.toml "path = \"$notice\"" "$3"
}

check_tree() {
    local lock version pin bundle_sha

    # ESLint host checker (tools/eslint/package-lock.json).
    lock="$root/tools/eslint/package-lock.json"
    version=$(npm_locked "$lock" eslint) || { fail "eslint: not locked in tools/eslint/package-lock.json"; version=; }
    if [ -n "$version" ]; then
        require_row "ESLint host checker" "\`eslint\` $version" eslint
        require_row "ESLint host checker" "eslint-$version.tgz" eslint
        require_text THIRD_PARTY_NOTICES.md "ESLint $version," eslint
        require_text THIRD_PARTY_NOTICES.md "eslint/-/eslint-$version.tgz" eslint
        require_notice ESLINT "$version" eslint
    fi
    version=$(npm_locked "$lock" @eslint/js) || { fail "@eslint/js: not locked in tools/eslint/package-lock.json"; version=; }
    if [ -n "$version" ]; then
        require_row "ESLint host checker" "\`@eslint/js\` $version" @eslint/js
        require_text THIRD_PARTY_NOTICES.md "\`@eslint/js\` $version" @eslint/js
    fi
    version=$(npm_locked "$lock" globals) || { fail "globals: not locked in tools/eslint/package-lock.json"; version=; }
    if [ -n "$version" ]; then
        require_row "ESLint host checker" "\`globals\` $version" globals
        require_text THIRD_PARTY_NOTICES.md "\`globals\` $version" globals
        require_notice GLOBALS "$version" globals
    fi

    # FlexSearch (website/package-lock.json and the bundle checksum pin).
    version=$(npm_locked "$root/website/package-lock.json" flexsearch) || { fail "flexsearch: not locked in website/package-lock.json"; version=; }
    if [ -n "$version" ]; then
        require_row FlexSearch "$version in \`website/package-lock.json\`" flexsearch
        require_text THIRD_PARTY_NOTICES.md "FlexSearch $version," flexsearch
    fi
    pin="$root/website/vendor-pins/flexsearch.bundle.min.js.sha256"
    bundle_sha=$(awk 'NR == 1 { print $1 }' "$pin" 2>/dev/null || true)
    if [ "${#bundle_sha}" -ne 64 ]; then
        fail "flexsearch: website/vendor-pins/flexsearch.bundle.min.js.sha256 has no SHA-256"
    else
        require_row FlexSearch "bundle SHA-256 \`$bundle_sha\`" flexsearch
    fi

    # Hextra (website/go.mod, confirmed by website/go.sum).
    version=$(awk '
        $1 == "require" && $2 == "github.com/imfing/hextra" { print $3; exit }
        $1 == "github.com/imfing/hextra" { print $2; exit }
    ' "$root/website/go.mod")
    if [ -z "$version" ]; then
        fail "hextra: not required in website/go.mod"
    else
        if ! grep -Fq "github.com/imfing/hextra $version h1:" "$root/website/go.sum"; then
            fail "hextra: website/go.sum has no hash for $version"
        fi
        require_row Hextra "$version, locked by \`website/go.sum\`" hextra
        require_text THIRD_PARTY_NOTICES.md "Hextra $version," hextra
    fi

    # Pillow (tools/bootstrap-requirements.txt).
    version=$(sed -n 's/^Pillow==\([^[:space:]\\]*\).*/\1/p' "$root/tools/bootstrap-requirements.txt" | head -n 1)
    if [ -z "$version" ]; then
        fail "Pillow: no Pillow== pin in tools/bootstrap-requirements.txt"
    else
        require_row "Pillow bootstrap host tool" "| $version / " Pillow
        require_row "Pillow bootstrap host tool" "pillow-$version.tar.gz" Pillow
        require_text THIRD_PARTY_NOTICES.md "Pillow $version is used" Pillow
        require_notice PILLOW "$version" Pillow
    fi
}

selftest() {
    local fixture status
    # Global so the EXIT trap can still see it after this function returns.
    scratch=$(mktemp -d)
    trap 'rm -rf "$scratch"' EXIT

    seed() {
        fixture="$scratch/$1"
        mkdir -p "$fixture/tools/eslint" "$fixture/website/vendor-pins" "$fixture/docs"
        cp "$root/tools/eslint/package-lock.json" "$fixture/tools/eslint/"
        cp "$root/tools/bootstrap-requirements.txt" "$fixture/tools/"
        cp "$root/website/package-lock.json" "$root/website/go.mod" "$root/website/go.sum" "$fixture/website/"
        cp "$root/website/vendor-pins/flexsearch.bundle.min.js.sha256" "$fixture/website/vendor-pins/"
        cp "$root/docs/SOURCE_PROVENANCE.md" "$fixture/docs/"
        cp "$root/THIRD_PARTY_NOTICES.md" "$root/REUSE.toml" "$fixture/"
        cp -R "$root/notices" "$fixture/"
    }

    # expect_failure CASE PATTERN: the check must fail and name the drift.
    expect_failure() {
        status=0
        "$SCRIPT_DIR/check-dependency-notices.sh" --root "$fixture" > "$scratch/$1.log" 2>&1 || status=$?
        if [ "$status" -eq 0 ]; then
            cat "$scratch/$1.log" >&2
            printf 'FAIL: selftest %s: drift was accepted\n' "$1" >&2
            exit 1
        fi
        if ! grep -Fq -- "$2" "$scratch/$1.log"; then
            cat "$scratch/$1.log" >&2
            printf 'FAIL: selftest %s: missing diagnostic %s\n' "$1" "$2" >&2
            exit 1
        fi
        printf 'PASS: selftest rejects %s\n' "$1"
    }

    seed clean
    if ! "$SCRIPT_DIR/check-dependency-notices.sh" --root "$fixture" > "$scratch/clean.log" 2>&1; then
        cat "$scratch/clean.log" >&2
        printf '%s\n' "FAIL: selftest: unchanged copy of the tree failed" >&2
        exit 1
    fi
    printf '%s\n' "PASS: selftest accepts the current tree"

    # A Dependabot-style lockfile-only npm bump.
    seed globals
    node -e '
        const fs = require("fs");
        const path = process.argv[1];
        const lock = JSON.parse(fs.readFileSync(path, "utf8"));
        lock.packages["node_modules/globals"].version = "99.0.0";
        fs.writeFileSync(path, JSON.stringify(lock, null, 2) + "\n");
    ' "$fixture/tools/eslint/package-lock.json"
    expect_failure "lockfile-only globals bump" "notices/GLOBALS-99.0.0-LICENSE.txt"

    # A pip pin bump without provenance or notice updates.
    seed pillow
    sed -i 's/^Pillow==[^ ]*/Pillow==99.0.0/' "$fixture/tools/bootstrap-requirements.txt"
    expect_failure "requirements-only Pillow bump" "'| 99.0.0 / '"

    # A Go module bump without provenance or notices.
    seed hextra
    sed -i 's#github.com/imfing/hextra v[^ ]*#github.com/imfing/hextra v99.0.0#' "$fixture/website/go.mod" "$fixture/website/go.sum"
    expect_failure "go.mod-only Hextra bump" "Hextra v99.0.0,"

    # A FlexSearch bundle pin change without the provenance checksum.
    seed flexsearch
    printf '%064d  flexsearch.bundle.min.js\n' 0 > "$fixture/website/vendor-pins/flexsearch.bundle.min.js.sha256"
    expect_failure "FlexSearch pin-only change" "bundle SHA-256"

    # A renamed notice with the old file left behind.
    seed stale
    cp "$fixture/notices/GLOBALS-"*-LICENSE.txt "$fixture/notices/GLOBALS-0.0.1-LICENSE.txt"
    expect_failure "stale notice file" "stale notice notices/GLOBALS-0.0.1-LICENSE.txt"

    printf '%s\n' "Dependency notice self-test: PASS"
}

case "${1:-}" in
    --selftest)
        [ "$#" -eq 1 ] || usage
        selftest
        exit 0
        ;;
    --root)
        [ "$#" -eq 2 ] || usage
        root="$(cd "$2" && pwd)"
        ;;
    "") ;;
    *) usage ;;
esac

if ! command -v node >/dev/null 2>&1; then
    printf '%s\n' "FAIL: node is required to read npm lockfiles" >&2
    exit 1
fi

check_tree
if [ "$failures" -ne 0 ]; then
    printf 'FAIL: %d dependency notice or provenance mismatch(es). Update docs/SOURCE_PROVENANCE.md, THIRD_PARTY_NOTICES.md, notices/, and REUSE.toml with the lockfile, as SECURITY.md requires.\n' "$failures" >&2
    exit 1
fi
printf '%s\n' "PASS: Dependabot-managed dependency versions match provenance and notices"
