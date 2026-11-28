#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Fail when a Dependabot-managed lockfile or pin changes without the matching
# provenance, notice, and REUSE updates that SECURITY.md requires in the same
# PR. Versions come from the authoritative lock or pin files, so a bump that
# touches only the lockfile fails here.
#
# Each record is checked by a fixed context such as "`globals` ". Every
# occurrence of that context followed by a version must name the locked
# version exactly, so 10.0.10 does not satisfy 10.0.1 and a stale second
# reference fails.
#
# Not checked here, and still reviewed by hand: archive SHA-256 values in
# docs/SOURCE_PROVENANCE.md, wheel hashes, source revisions, license text
# content, and GitHub Actions pins.
#
# usage: tools/check-dependency-notices.sh [--root DIR]
#        tools/check-dependency-notices.sh --selftest

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$SCRIPT_DIR/.." && pwd)"

# A dotted numeric version with an optional prerelease or build suffix. It
# stops before a dot that is not followed by a digit, as in eslint-1.0.0.tgz.
VERSION_RE='[0-9]+(\.[0-9]+)*([-+][0-9A-Za-z]+(\.[0-9A-Za-z]+)*)?'
SHA256_RE='[0-9a-f]{64}'
NOTICE_RE='[0-9A-Za-z.+-]*-LICENSE\.txt'
# Markdown code-span delimiter, kept in a variable so contexts can be quoted.
BT='`'

usage() {
    printf '%s\n' "usage: $0 [--root DIR] | --selftest" >&2
    exit 2
}

failures=0
fail() {
    printf 'FAIL: %s\n' "$1" >&2
    failures=$((failures + 1))
}

ere_escape() {
    printf '%s' "$1" | sed 's/[][\.*^$+?(){}|/]/\\&/g'
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

# The Markdown table rows whose first cell is the given label.
provenance_rows() {
    awk -v label="$1" -F'|' '
        { cell = $2; gsub(/^[ \t]+|[ \t]+$/, "", cell) }
        cell == label { print }
    ' "$root/docs/SOURCE_PROVENANCE.md"
}

# require_values DEPENDENCY WHERE TEXT CONTEXT VALUE_RE EXPECTED
# TEXT must contain CONTEXT followed by a value at least once, and every such
# value must equal EXPECTED exactly.
require_values() {
    local dependency=$1 where=$2 text=$3 context=$4 value_re=$5 expected=$6
    local found=0 match value
    while IFS= read -r match; do
        [ -n "$match" ] || continue
        found=1
        value=${match#"$context"}
        if [ "$value" != "$expected" ]; then
            fail "$dependency: $where has '$context$value'; the lock or pin is '$expected'"
        fi
    done <<EOF
$(printf '%s\n' "$text" | grep -oE -- "$(ere_escape "$context")$value_re" || true)
EOF
    if [ "$found" -eq 0 ]; then
        fail "$dependency: $where does not mention '$context$expected'; update it with the lock or pin"
    fi
}

# require_row DEPENDENCY LABEL CONTEXT VALUE_RE EXPECTED
# Exactly one provenance row carries LABEL, and it records EXPECTED.
require_row() {
    local rows count
    rows=$(provenance_rows "$2")
    count=$(printf '%s' "$rows" | grep -c '' || true)
    if [ "$count" -ne 1 ]; then
        fail "$1: docs/SOURCE_PROVENANCE.md has $count '$2' rows; expected exactly one"
        return
    fi
    require_values "$1" "docs/SOURCE_PROVENANCE.md '$2' row" "$rows" "$3" "$4" "$5"
}

# require_file DEPENDENCY PATH CONTEXT VALUE_RE EXPECTED
require_file() {
    require_values "$1" "$2" "$(cat "$root/$2")" "$3" "$4" "$5"
}

# require_notice DEPENDENCY PREFIX VERSION: exactly the current notice file
# exists, and THIRD_PARTY_NOTICES.md and REUSE.toml name only that version.
require_notice() {
    local notice="notices/$2-$3-LICENSE.txt" other
    if [ ! -f "$root/$notice" ]; then
        fail "$1: missing $notice; copy the license from the locked $3 package"
    fi
    for other in "$root"/notices/"$2"-*-LICENSE.txt; do
        [ -e "$other" ] || continue
        if [ "${other#"$root/"}" != "$notice" ]; then
            fail "$1: stale notice ${other#"$root/"}; rename it to $notice"
        fi
    done
    # Match the whole file name so a prerelease suffix cannot blur the version.
    require_file "$1" THIRD_PARTY_NOTICES.md "notices/$2-" "$NOTICE_RE" "$3-LICENSE.txt"
    require_file "$1" REUSE.toml "path = \"notices/$2-" "$NOTICE_RE" "$3-LICENSE.txt"
}

check_tree() {
    local lock version pin bundle_sha

    # ESLint host checker (tools/eslint/package-lock.json).
    lock="$root/tools/eslint/package-lock.json"
    if version=$(npm_locked "$lock" eslint); then
        require_row eslint "ESLint host checker" "${BT}eslint${BT} " "$VERSION_RE" "$version"
        require_row eslint "ESLint host checker" 'eslint-' "$VERSION_RE" "$version"
        require_file eslint THIRD_PARTY_NOTICES.md 'ESLint ' "$VERSION_RE" "$version"
        require_file eslint THIRD_PARTY_NOTICES.md 'eslint/-/eslint-' "$VERSION_RE" "$version"
        require_notice eslint ESLINT "$version"
    else
        fail "eslint: not locked in tools/eslint/package-lock.json"
    fi
    if version=$(npm_locked "$lock" @eslint/js); then
        require_row @eslint/js "ESLint host checker" "${BT}@eslint/js${BT} " "$VERSION_RE" "$version"
        require_file @eslint/js THIRD_PARTY_NOTICES.md "${BT}@eslint/js${BT} " "$VERSION_RE" "$version"
    else
        fail "@eslint/js: not locked in tools/eslint/package-lock.json"
    fi
    if version=$(npm_locked "$lock" globals); then
        require_row globals "ESLint host checker" "${BT}globals${BT} " "$VERSION_RE" "$version"
        require_file globals THIRD_PARTY_NOTICES.md "${BT}globals${BT} " "$VERSION_RE" "$version"
        require_notice globals GLOBALS "$version"
    else
        fail "globals: not locked in tools/eslint/package-lock.json"
    fi

    # FlexSearch (website/package-lock.json and the bundle checksum pin).
    if version=$(npm_locked "$root/website/package-lock.json" flexsearch); then
        require_row flexsearch FlexSearch '| ' "$VERSION_RE" "$version"
        require_file flexsearch THIRD_PARTY_NOTICES.md 'FlexSearch ' "$VERSION_RE" "$version"
    else
        fail "flexsearch: not locked in website/package-lock.json"
    fi
    pin="$root/website/vendor-pins/flexsearch.bundle.min.js.sha256"
    bundle_sha=$(awk 'NR == 1 { print $1 }' "$pin" 2>/dev/null || true)
    if ! printf '%s\n' "$bundle_sha" | grep -Eqx "$SHA256_RE"; then
        fail "flexsearch: website/vendor-pins/flexsearch.bundle.min.js.sha256 has no SHA-256"
    else
        require_row flexsearch FlexSearch "bundle SHA-256 ${BT}" "$SHA256_RE" "$bundle_sha"
    fi

    # Hextra (website/go.mod, confirmed by website/go.sum). A replace
    # directive would make the build use a source the provenance row does not
    # describe, so fail closed on any.
    if awk '
        /^[[:space:]]*replace[[:space:]]*\(/ { block = 1; next }
        block && /^[[:space:]]*\)/ { block = 0; next }
        block && $1 == "github.com/imfing/hextra" { found = 1 }
        $1 == "replace" && $2 == "github.com/imfing/hextra" { found = 1 }
        END { exit !found }
    ' "$root/website/go.mod"; then
        fail "hextra: website/go.mod replaces github.com/imfing/hextra; record the replacement source in docs/SOURCE_PROVENANCE.md and this check first"
    fi
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
        require_row hextra Hextra '| ' "v$VERSION_RE" "$version"
        require_file hextra THIRD_PARTY_NOTICES.md 'Hextra ' "v$VERSION_RE" "$version"
    fi

    # Pillow (tools/bootstrap-requirements.txt).
    version=$(sed -n 's/^Pillow==\([^[:space:]\\]*\).*/\1/p' "$root/tools/bootstrap-requirements.txt" | head -n 1)
    if [ -z "$version" ]; then
        fail "Pillow: no Pillow== pin in tools/bootstrap-requirements.txt"
    else
        require_row Pillow "Pillow bootstrap host tool" '| ' "$VERSION_RE" "$version"
        require_row Pillow "Pillow bootstrap host tool" 'pillow-' "$VERSION_RE" "$version"
        require_file Pillow THIRD_PARTY_NOTICES.md 'Pillow ' "$VERSION_RE" "$version"
        require_notice Pillow PILLOW "$version"
    fi
}

selftest() {
    local fixture status version
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

    # set_npm_lock FILE PACKAGE VERSION: a Dependabot-style lockfile edit.
    set_npm_lock() {
        node -e '
            const fs = require("fs");
            const [path, name, version] = process.argv.slice(1);
            const lock = JSON.parse(fs.readFileSync(path, "utf8"));
            lock.packages["node_modules/" + name].version = version;
            fs.writeFileSync(path, JSON.stringify(lock, null, 2) + "\n");
        ' "$@"
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
    set_npm_lock "$fixture/tools/eslint/package-lock.json" globals 99.0.0
    expect_failure "lockfile-only globals bump" "notices/GLOBALS-99.0.0-LICENSE.txt"

    # A record that extends the locked version as a prefix (10.0.1 vs 10.0.10).
    seed prefix
    set_npm_lock "$fixture/tools/eslint/package-lock.json" @eslint/js 7.0.1
    sed -i "s#${BT}@eslint/js${BT} [0-9.]*#${BT}@eslint/js${BT} 7.0.10#" "$fixture/docs/SOURCE_PROVENANCE.md" "$fixture/THIRD_PARTY_NOTICES.md"
    expect_failure "record version that extends the locked version" "has '${BT}@eslint/js${BT} 7.0.10'; the lock or pin is '7.0.1'"

    # A record that ends with the locked version (110.0.1 vs 10.0.1).
    seed suffix
    version=$(node -p 'require(process.argv[1]).packages["node_modules/eslint"].version' "$fixture/tools/eslint/package-lock.json")
    sed -i "s/${BT}eslint${BT} $version/${BT}eslint${BT} 1$version/" "$fixture/docs/SOURCE_PROVENANCE.md"
    expect_failure "record version that ends with the locked version" "has '${BT}eslint${BT} 1$version'"

    # A stale second reference left beside the new one.
    seed stale-reference
    sed -i "s/\\(| ESLint host checker |[^|]*\\)|/\\1and ${BT}globals${BT} 0.0.1 |/" "$fixture/docs/SOURCE_PROVENANCE.md"
    expect_failure "stale extra version reference" "has '${BT}globals${BT} 0.0.1'"

    # A duplicated provenance row whose second copy is stale.
    seed duplicate-row
    grep -F '| FlexSearch |' "$fixture/docs/SOURCE_PROVENANCE.md" | sed 's/| [0-9][0-9.]* in/| 0.0.1 in/' > "$scratch/row"
    cat "$scratch/row" >> "$fixture/docs/SOURCE_PROVENANCE.md"
    expect_failure "duplicate provenance row" "has 2 'FlexSearch' rows; expected exactly one"

    # A pip pin bump without provenance or notice updates.
    seed pillow
    sed -i 's/^Pillow==[^ ]*/Pillow==99.0.0/' "$fixture/tools/bootstrap-requirements.txt"
    expect_failure "requirements-only Pillow bump" "the lock or pin is '99.0.0'"

    # A go.mod-only Hextra bump: go.sum still has only the old hash.
    seed hextra
    sed -i 's#github.com/imfing/hextra v[^ ]*#github.com/imfing/hextra v99.0.0#' "$fixture/website/go.mod"
    expect_failure "go.mod-only Hextra bump" "website/go.sum has no hash for v99.0.0"

    # A replace directive that swaps the Hextra source.
    seed hextra-replace
    printf '\nreplace github.com/imfing/hextra => ../hextra-fork\n' >> "$fixture/website/go.mod"
    expect_failure "Hextra replace directive" "website/go.mod replaces github.com/imfing/hextra"

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
