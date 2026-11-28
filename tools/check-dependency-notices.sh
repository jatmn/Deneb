#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Fail when a Dependabot-managed lockfile or pin changes without the matching
# provenance, notice, and REUSE updates that SECURITY.md requires in the same
# PR. Versions come from the authoritative lock or pin files, so a bump that
# touches only the lockfile fails here.
#
# Each record is checked by a fixed context such as "`globals` ". Every
# occurrence of that context followed by a version is read as the whole field,
# up to whitespace or Markdown/TOML punctuation, and must equal the expected
# field exactly. So 10.0.10 does not satisfy 10.0.1, eslint-1.0.0.zip does not
# satisfy eslint-1.0.0.tgz, and a stale second reference fails.
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

SHA256_RE='[0-9a-f]{64}'
# Markdown code-span delimiter, kept in a variable so contexts can be quoted.
BT='`'
# The rest of a field: everything up to whitespace or a Markdown/TOML
# delimiter. A version or digest field ends where this stops.
FIELD_RE="[^][:space:]${BT}'\"|,;()<>]*"
# First character of a field that records a version or digest.
DIGIT='[0-9]'
VDIGIT='v[0-9]'
HEX='[0-9A-Fa-f]'

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

# require_values DEPENDENCY WHERE TEXT CONTEXT START EXPECTED
# TEXT must contain CONTEXT followed by a field that begins with START at least
# once. Each such field, read to its end, must equal EXPECTED exactly. One
# trailing period is treated as sentence punctuation.
require_values() {
    local dependency=$1 where=$2 text=$3 context=$4 start=$5 expected=$6
    local found=0 match value
    while IFS= read -r match; do
        [ -n "$match" ] || continue
        found=1
        value=${match#"$context"}
        value=${value%.}
        if [ "$value" != "$expected" ]; then
            fail "$dependency: $where has '$context$value'; expected '$context$expected' from the lock or pin"
        fi
    done <<EOF
$(printf '%s\n' "$text" | grep -oE -- "$(ere_escape "$context")$start$FIELD_RE" || true)
EOF
    if [ "$found" -eq 0 ]; then
        fail "$dependency: $where does not mention '$context$expected'; update it with the lock or pin"
    fi
}

# require_row DEPENDENCY LABEL CONTEXT START EXPECTED
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

# require_file DEPENDENCY PATH CONTEXT START EXPECTED
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
    # The whole path must match, so .txt.bak or a prerelease cannot slip by.
    require_file "$1" THIRD_PARTY_NOTICES.md "notices/$2-" "$DIGIT" "$3-LICENSE.txt"
    require_file "$1" REUSE.toml "path = \"notices/$2-" "$DIGIT" "$3-LICENSE.txt"
}

check_tree() {
    local lock version pin bundle_sha

    # ESLint host checker (tools/eslint/package-lock.json).
    lock="$root/tools/eslint/package-lock.json"
    if version=$(npm_locked "$lock" eslint); then
        require_row eslint "ESLint host checker" "${BT}eslint${BT} " "$DIGIT" "$version"
        require_row eslint "ESLint host checker" 'eslint-' "$DIGIT" "$version.tgz"
        require_file eslint THIRD_PARTY_NOTICES.md 'ESLint ' "$DIGIT" "$version"
        require_file eslint THIRD_PARTY_NOTICES.md 'eslint/-/eslint-' "$DIGIT" "$version.tgz"
        require_notice eslint ESLINT "$version"
    else
        fail "eslint: not locked in tools/eslint/package-lock.json"
    fi
    if version=$(npm_locked "$lock" @eslint/js); then
        require_row @eslint/js "ESLint host checker" "${BT}@eslint/js${BT} " "$DIGIT" "$version"
        require_file @eslint/js THIRD_PARTY_NOTICES.md "${BT}@eslint/js${BT} " "$DIGIT" "$version"
    else
        fail "@eslint/js: not locked in tools/eslint/package-lock.json"
    fi
    if version=$(npm_locked "$lock" globals); then
        require_row globals "ESLint host checker" "${BT}globals${BT} " "$DIGIT" "$version"
        require_file globals THIRD_PARTY_NOTICES.md "${BT}globals${BT} " "$DIGIT" "$version"
        require_notice globals GLOBALS "$version"
    else
        fail "globals: not locked in tools/eslint/package-lock.json"
    fi

    # FlexSearch (website/package-lock.json and the bundle checksum pin).
    if version=$(npm_locked "$root/website/package-lock.json" flexsearch); then
        require_row flexsearch FlexSearch '| ' "$DIGIT" "$version"
        require_file flexsearch THIRD_PARTY_NOTICES.md 'FlexSearch ' "$DIGIT" "$version"
    else
        fail "flexsearch: not locked in website/package-lock.json"
    fi
    pin="$root/website/vendor-pins/flexsearch.bundle.min.js.sha256"
    bundle_sha=$(awk 'NR == 1 { print $1 }' "$pin" 2>/dev/null || true)
    if ! printf '%s\n' "$bundle_sha" | grep -Eqx "$SHA256_RE"; then
        fail "flexsearch: website/vendor-pins/flexsearch.bundle.min.js.sha256 has no SHA-256"
    else
        require_row flexsearch FlexSearch "bundle SHA-256 ${BT}" "$HEX" "$bundle_sha"
    fi

    # Hextra (website/go.mod, confirmed by website/go.sum). A replace
    # directive would make the build use a source the provenance row does not
    # describe, so fail closed on any. The left side of each replace, in a
    # single-line directive or a replace ( ... ) block, must be a plain module
    # path, optionally in one pair of double quotes, optionally followed by a
    # version. Quoted escapes such as \x78 are not decoded: a backslash, any
    # other quoting, or a line that does not parse is an error.
    replace_report=$(awk -v target=github.com/imfing/hextra '
        function check(spec,   arrow, n, fields, module) {
            sub(/\/\/.*/, "", spec)
            if (spec ~ /^[ \t]*$/) return
            arrow = index(spec, "=>")
            if (arrow == 0) { print "bad\t" $0; return }
            n = split(substr(spec, 1, arrow - 1), fields, " ")
            if (n < 1 || n > 2) { print "bad\t" $0; return }
            module = fields[1]
            if (module ~ /^".*"$/) module = substr(module, 2, length(module) - 2)
            if (module !~ /^[A-Za-z0-9._~\/-]+$/) { print "bad\t" $0; return }
            if (n == 2 && fields[2] !~ /^v[0-9A-Za-z.+-]+$/) { print "bad\t" $0; return }
            if (module == target) print "hextra\t" $0
        }
        block && /^[ \t]*\)[ \t]*(\/\/.*)?$/ { block = 0; next }
        block { check($0); next }
        /^[ \t]*replace[ \t]*\(/ {
            rest = $0
            sub(/^[ \t]*replace[ \t]*\(/, "", rest)
            block = 1
            check(rest)
            next
        }
        /^[ \t]*replace([^A-Za-z0-9_]|$)/ {
            rest = $0
            sub(/^[ \t]*replace/, "", rest)
            check(rest)
        }
    ' "$root/website/go.mod")
    while IFS=$'\t' read -r kind line; do
        case "$kind" in
            hextra) fail "hextra: website/go.mod replaces github.com/imfing/hextra; record the replacement source in docs/SOURCE_PROVENANCE.md and this check first" ;;
            bad) fail "hextra: website/go.mod has a replace this check cannot verify as a plain module path: $line" ;;
        esac
    done <<EOF
$replace_report
EOF
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
        require_row hextra Hextra '| ' "$VDIGIT" "$version"
        require_file hextra THIRD_PARTY_NOTICES.md 'Hextra ' "$VDIGIT" "$version"
    fi

    # Pillow (tools/bootstrap-requirements.txt).
    version=$(sed -n 's/^Pillow==\([^[:space:]\\]*\).*/\1/p' "$root/tools/bootstrap-requirements.txt" | head -n 1)
    if [ -z "$version" ]; then
        fail "Pillow: no Pillow== pin in tools/bootstrap-requirements.txt"
    else
        require_row Pillow "Pillow bootstrap host tool" '| ' "$DIGIT" "$version"
        require_row Pillow "Pillow bootstrap host tool" 'pillow-' "$DIGIT" "$version.tar.gz"
        require_file Pillow THIRD_PARTY_NOTICES.md 'Pillow ' "$DIGIT" "$version"
        require_notice Pillow PILLOW "$version"
    fi
}

selftest() {
    local fixture status version old
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

    # locked_version FILE PACKAGE: the version locked in the fixture.
    locked_version() {
        node -p 'require(process.argv[1]).packages["node_modules/" + process.argv[2]].version' \
            "$fixture/$1" "$2"
    }

    # sed_escape TEXT: TEXT as a literal in a sed pattern delimited by #.
    sed_escape() {
        printf '%s' "$1" | sed 's/[][\.*^$/#]/\\&/g'
    }

    # mutate_eslint_archive: change the notice URL archive from .tgz to .zip,
    # using the locked version so prerelease versions match too.
    mutate_eslint_archive() {
        local locked
        locked=$(sed_escape "$(locked_version tools/eslint/package-lock.json eslint)")
        sed -i "s#eslint/-/eslint-$locked\\.tgz#eslint/-/eslint-$locked.zip#" "$fixture/THIRD_PARTY_NOTICES.md"
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
    expect_failure "record version that extends the locked version" "has '${BT}@eslint/js${BT} 7.0.10'; expected '${BT}@eslint/js${BT} 7.0.1'"

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
    expect_failure "requirements-only Pillow bump" "99.0.0' from the lock or pin"

    # A go.mod-only Hextra bump: go.sum still has only the old hash.
    seed hextra
    sed -i 's#github.com/imfing/hextra v[^ ]*#github.com/imfing/hextra v99.0.0#' "$fixture/website/go.mod"
    expect_failure "go.mod-only Hextra bump" "website/go.sum has no hash for v99.0.0"

    # A replace directive that swaps the Hextra source.
    seed hextra-replace
    printf '\nreplace github.com/imfing/hextra => ../hextra-fork\n' >> "$fixture/website/go.mod"
    expect_failure "Hextra replace directive" "website/go.mod replaces github.com/imfing/hextra"

    # A quoted module path on the left side of a single-line replace.
    seed hextra-replace-quoted
    printf '\nreplace "github.com/imfing/hextra" => ../hextra-fork\n' >> "$fixture/website/go.mod"
    expect_failure "quoted Hextra replace" "website/go.mod replaces github.com/imfing/hextra"

    # A replace block with a versioned left side.
    seed hextra-replace-block
    printf '\nreplace (\n\texample.com/other v1.0.0 => ../other\n\tgithub.com/imfing/hextra v0.13.0 => ../hextra-fork\n)\n' >> "$fixture/website/go.mod"
    expect_failure "Hextra replace block with a version" "website/go.mod replaces github.com/imfing/hextra"

    # A single-line replace with a quoted, versioned left side.
    seed hextra-replace-versioned
    printf '\nreplace "github.com/imfing/hextra" v0.13.0 => example.com/hextra-fork v0.13.1\n' >> "$fixture/website/go.mod"
    expect_failure "versioned Hextra replace" "website/go.mod replaces github.com/imfing/hextra"

    # Go decodes escapes in a quoted path; this check fails closed instead.
    seed hextra-replace-escape
    printf '\nreplace "github.com/imfing/he\\x78tra" => ../hextra-fork\n' >> "$fixture/website/go.mod"
    expect_failure "escaped module path in a replace" "cannot verify as a plain module path"

    # A replace line that does not parse.
    seed replace-unparsed
    printf '\nreplace github.com/imfing/hextra\n' >> "$fixture/website/go.mod"
    expect_failure "unparseable replace" "cannot verify as a plain module path"

    # A replace of another module is allowed.
    seed other-replace
    printf '\nreplace example.com/other => ../other\n' >> "$fixture/website/go.mod"
    if ! "$SCRIPT_DIR/check-dependency-notices.sh" --root "$fixture" > "$scratch/other-replace.log" 2>&1; then
        cat "$scratch/other-replace.log" >&2
        printf '%s\n' "FAIL: selftest: a replace of another module was rejected" >&2
        exit 1
    fi
    printf '%s\n' "PASS: selftest accepts a replace of another module"

    # The registry archive in the notice URL changed type but kept the version.
    seed eslint-archive
    mutate_eslint_archive
    expect_failure "non-tgz ESLint archive in the notice URL" ".zip'; expected 'eslint/-/eslint-"

    # A REUSE.toml license path with an extra extension.
    seed reuse-extension
    version=$(locked_version tools/eslint/package-lock.json globals)
    sed -i "s#\\(path = \"notices/GLOBALS-$(sed_escape "$version")-LICENSE\\.txt\\)\"#\\1.bak\"#" "$fixture/REUSE.toml"
    expect_failure "REUSE.toml license path with an extra extension" "-LICENSE.txt.bak'; expected 'path = \"notices/GLOBALS-"

    # A synchronized prerelease ESLint pin passes, and the archive mutation
    # still applies to it.
    seed prerelease
    version=$(locked_version tools/eslint/package-lock.json eslint)
    set_npm_lock "$fixture/tools/eslint/package-lock.json" eslint 10.16.0-rc.1
    old=$(sed_escape "$version")
    sed -i -e "s#${BT}eslint${BT} $old#${BT}eslint${BT} 10.16.0-rc.1#" -e "s#eslint-$old\\.tgz#eslint-10.16.0-rc.1.tgz#" \
        "$fixture/docs/SOURCE_PROVENANCE.md"
    sed -i -e "s#ESLint $old,#ESLint 10.16.0-rc.1,#" -e "s#eslint-$old\\.tgz#eslint-10.16.0-rc.1.tgz#" \
        -e "s#notices/ESLINT-$old-#notices/ESLINT-10.16.0-rc.1-#" "$fixture/THIRD_PARTY_NOTICES.md"
    sed -i "s#notices/ESLINT-$old-#notices/ESLINT-10.16.0-rc.1-#" "$fixture/REUSE.toml"
    mv "$fixture/notices/ESLINT-$version-LICENSE.txt" "$fixture/notices/ESLINT-10.16.0-rc.1-LICENSE.txt"
    if ! "$SCRIPT_DIR/check-dependency-notices.sh" --root "$fixture" > "$scratch/prerelease.log" 2>&1; then
        cat "$scratch/prerelease.log" >&2
        printf '%s\n' "FAIL: selftest: a synchronized prerelease ESLint pin was rejected" >&2
        exit 1
    fi
    printf '%s\n' "PASS: selftest accepts a synchronized prerelease ESLint pin"
    cp -R "$fixture" "$scratch/prerelease-archive"
    fixture="$scratch/prerelease-archive"
    mutate_eslint_archive
    expect_failure "non-tgz prerelease ESLint archive" "eslint-10.16.0-rc.1.zip'; expected 'eslint/-/eslint-10.16.0-rc.1.tgz'"

    # A provenance checksum that starts with the pinned digest but is longer.
    seed flexsearch-long-sha
    sed -i "s/\(bundle SHA-256 ${BT}[0-9a-f]*\)${BT}/\10${BT}/" "$fixture/docs/SOURCE_PROVENANCE.md"
    expect_failure "65-character FlexSearch checksum" "0'; expected 'bundle SHA-256 ${BT}"

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
