#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Report pins that Dependabot cannot update: native sources, the OpenWrt
# toolchain, the LVGL submodule, Hugo, Gitleaks, and reuse. Each current
# version is read from the file that pins it. Prints a Markdown table.
#
#   bash tools/check-upstream-pins.sh             # live lookups
#   bash tools/check-upstream-pins.sh --selftest  # offline parser checks
#
# Exit status: 0 when every pin is current; 3 when any pin is behind, or a
# local pin is missing, malformed, or disagrees with another copy; 1 when an
# upstream lookup for a tracked pin fails or returns a value that is not a
# plain version. A failed lookup is never reported as current.
# GitHub lookups use `gh api` (set GH_TOKEN in CI).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION_RE='^[0-9]+(\.[0-9]+)*$'

# --- Parsers: each reads an upstream response on stdin and fails when it
# finds no usable version. ---

# Highest non-draft, non-prerelease GitHub release whose tag is
# PREFIX<version> and whose version matches LINE (an ERE). Accepts the
# concatenated pages that `gh api --paginate` prints.
latest_github_release() {
    local prefix=$1 line=$2
    jq -r --arg prefix "$prefix" '
        .[] | select(.draft == false and .prerelease == false) | .tag_name
        | select(startswith($prefix)) | ltrimstr($prefix)' |
        grep -E "$VERSION_RE" | grep -E "$line" | sort -V | tail -n 1 | grep .
}

# Highest stable version in an HTML index for links named NAME-<version>SUFFIX.
latest_index_version() {
    local name=$1 suffix=$2
    grep -oE "${name}-[0-9]+(\.[0-9]+)+${suffix//./\\.}" |
        sed -E "s/^${name}-//; s/${suffix//./\\.}\$//" | sort -uV | tail -n 1 | grep .
}

# Highest final OpenWrt release directory (skips -rc and packages-* entries).
latest_openwrt_release() {
    grep -oE 'href="[0-9]+\.[0-9]+\.[0-9]+/"' | sed -E 's/^href="//; s/\/"$//' |
        sort -uV | tail -n 1 | grep .
}

# GCC version in an OpenWrt target directory's toolchain archive name.
openwrt_toolchain_gcc() {
    grep -oE 'openwrt-toolchain-[^"<>]*_gcc-[0-9]+(\.[0-9]+)+_musl' |
        sed -E 's/.*_gcc-//; s/_musl$//' | sort -uV | tail -n 1 | grep .
}

# PKG_VERSION from an OpenWrt package Makefile.
makefile_pkg_version() {
    sed -nE 's/^PKG_VERSION:=([0-9][0-9.]*)[[:space:]]*$/\1/p' | head -n 1 | grep -E "$VERSION_RE"
}

# Newest final PyPI release that has files and is not fully yanked. PEP 440
# pre-, post-, and dev-releases contain letters, so the numeric filter skips them.
latest_pypi_release() {
    jq -r '.releases | to_entries[]
        | select((.value | length) > 0 and any(.value[]; .yanked != true)) | .key' |
        grep -E "$VERSION_RE" | sort -V | tail -n 1 | grep .
}

# GitHub compare API result as "STATUS AHEAD BEHIND"; fails on an error body.
compare_status() {
    jq -er 'select((.status | IN("identical", "ahead", "behind", "diverged"))
        and (.ahead_by | type) == "number" and (.behind_by | type) == "number")
        | "\(.status) \(.ahead_by) \(.behind_by)"'
}

valid_version() {
    [[ $1 =~ $VERSION_RE ]]
}

# "invalid" for a malformed pin or upstream version, "current" when
# CURRENT >= LATEST in version order, otherwise "behind".
version_status() {
    local current=$1 latest=$2
    if ! valid_version "$current" || ! valid_version "$latest"; then
        echo invalid
    elif [ "$current" = "$latest" ] ||
        [ "$(printf '%s\n%s\n' "$current" "$latest" | sort -V | tail -n 1)" = "$current" ]
    then
        echo current
    else
        echo behind
    fi
}

# Every reuse requirement in FILE, one per line (may be empty). A requirement
# runs to the next whitespace, quote, or backtick. Exactly reuse==X.Y.Z or
# reuse[extras]==X.Y.Z yields X.Y.Z; anything else (6.2.0x, >=6.2.0,
# 6.2.0,==6.3.0) is kept as text and later reported as invalid. A missing
# file yields nothing.
reuse_pins_in() {
    { grep -ohE "reuse(\[[^]]*\])?[=<>!~][^[:space:]\"'\`]*" -- "$1" 2>/dev/null || true; } |
        sed -E 's/^reuse(\[[^]]*\])?==//' | sort -u
}

# Resolve the reuse pin across files: "ok VERSION", "missing FILE",
# "invalid VALUE ...", or "disagree V1 V2 ...". Every file must pin a plain
# version, and all files must agree. An invalid requirement takes precedence.
resolve_reuse_pin() {
    local file pins all=
    for file in "$@"; do
        pins=$(reuse_pins_in "$file")
        if [ -z "$pins" ]; then
            echo "missing ${file#"$REPO_ROOT"/}"
            return
        fi
        all="$all$pins"$'\n'
    done
    all=$(printf '%s' "$all" | sort -u)
    local bad
    bad=$(printf '%s\n' "$all" | grep -vE "$VERSION_RE" || true)
    if [ -n "$bad" ]; then
        echo "invalid $(printf '%s' "$bad" | tr '\n' ' ')"
    elif [ "$(printf '%s\n' "$all" | wc -l)" -gt 1 ]; then
        echo "disagree $(printf '%s' "$all" | tr '\n' ' ')"
    else
        echo "ok $all"
    fi
}

# --- Report rows ---

ROWS=()
behind=0
failed=0

add_row() {
    local name=$1 current=$2 latest=$3 source=$4 status=$5
    case "$status" in
        behind|disagree|invalid|missing) behind=1 ;;
        "lookup failed") failed=1 ;;
    esac
    ROWS+=("| $name | $current | $latest | $source | $status |")
}

# --- Live lookups ---

fetch_url() {
    curl -fsSL --retry 2 --max-time 30 -- "$1"
}

fetch_gh() {
    gh api -- "$1"
}

# Compare one pin against a lookup command that prints the latest version.
# An empty local pin is missing and a malformed one is invalid (exit 3); an
# upstream value that is not a plain version is a failed lookup (exit 1).
check_version() {
    local name=$1 current=$2 source=$3
    shift 3
    local latest
    if [ -z "$current" ]; then
        add_row "$name" "?" "-" "$source" missing
    elif ! valid_version "$current"; then
        add_row "$name" "$current" "-" "$source" invalid
    elif latest=$("$@") && valid_version "$latest"; then
        add_row "$name" "$current" "$latest" "$source" "$(version_status "$current" "$latest")"
    else
        add_row "$name" "$current" "${latest:-?}" "$source" "lookup failed"
    fi
}

# Trimmed contents of a one-line pin file, or nothing when it is missing.
pin_from_file() {
    { tr -d '[:space:]' < "$1"; } 2>/dev/null || true
}

# Source the native pin file when present. A missing file or variable leaves
# that pin empty, so it reports as missing instead of aborting: nounset is off
# while the file is read, and an error inside it does not stop the report.
load_native_pins() {
    if [ -f "$1" ]; then
        set +u
        # shellcheck source=tools/native-dependency-pins.sh
        # shellcheck disable=SC1091
        . "$1" || true
        set -u
    fi
}

# All release pages, so an old release cannot hide a newer one past page 1.
gh_latest() {
    gh api --paginate -- "repos/$1/releases?per_page=100" | latest_github_release "$2" "$3"
}

index_latest() {
    fetch_url "$1" | latest_index_version "$2" "$3"
}

openwrt_latest() {
    fetch_url https://downloads.openwrt.org/releases/ | latest_openwrt_release
}

openwrt_gcc() {
    fetch_url "https://downloads.openwrt.org/releases/$1/targets/ramips/mt76x8/" | openwrt_toolchain_gcc
}

openwrt_musl() {
    fetch_url "https://raw.githubusercontent.com/openwrt/openwrt/v$1/toolchain/musl/common.mk" |
        makefile_pkg_version
}

pypi_latest() {
    fetch_url "https://pypi.org/pypi/$1/json" | latest_pypi_release
}

check_lvgl() {
    local revision line major short base latest result branch state ahead
    revision=$(git -C "$REPO_ROOT" ls-tree HEAD ui/lib/lvgl 2>/dev/null | awk '$2 == "commit" { print $3 }') || revision=
    # SOURCE_PROVENANCE.md records the maintenance line, e.g. "v9.6 maintenance".
    # The backticks are literal Markdown, not command substitution.
    # shellcheck disable=SC2016
    line=$(sed -nE 's/^\| LVGL \|.*\| v([0-9]+\.[0-9]+) maintenance \/ `?([0-9a-f]{40})`? \|.*/\1 \2/p' \
        "$REPO_ROOT/docs/SOURCE_PROVENANCE.md" 2>/dev/null | head -n 1) || line=
    short=${revision:0:7}
    if [ -z "$revision" ]; then
        add_row "LVGL submodule" "?" "-" "ui/lib/lvgl gitlink" missing
        return
    fi
    if [ -z "$line" ]; then
        add_row "LVGL submodule" "$short" "-" "docs/SOURCE_PROVENANCE.md LVGL row" missing
        return
    fi
    if [ "${line#* }" != "$revision" ]; then
        add_row "LVGL submodule" "$short" "${line#* }" "docs/SOURCE_PROVENANCE.md" disagree
    fi
    base=${line%% *}
    major=${base%%.*}
    # Scoped to the documented major line: the pin must contain its newest release.
    if latest=$(gh_latest lvgl/lvgl v "^${major}\\.") &&
        result=$(fetch_gh "repos/lvgl/lvgl/compare/v$latest...$revision" | compare_status)
    then
        case "${result%% *}" in
            identical|ahead) add_row "LVGL release (v$major)" "v$base maintenance @ $short" "v$latest" "lvgl/lvgl releases" current ;;
            *) add_row "LVGL release (v$major)" "v$base maintenance @ $short" "v$latest" "lvgl/lvgl releases" behind ;;
        esac
    else
        add_row "LVGL release (v$major)" "v$base maintenance @ $short" "?" "lvgl/lvgl releases" "lookup failed"
    fi
    # Maintenance fixes land on release/vX.Y after the last tag.
    if branch=$(fetch_gh "repos/lvgl/lvgl/branches/release/v$base" | jq -er '.commit.sha') &&
        result=$(fetch_gh "repos/lvgl/lvgl/compare/$revision...$branch" | compare_status)
    then
        read -r state ahead _ <<< "$result"
        case "$state" in
            identical|behind) add_row "LVGL release/v$base branch" "$short" "${branch:0:7}" "lvgl/lvgl branch" current ;;
            *) add_row "LVGL release/v$base branch" "$short" "${branch:0:7} (+$ahead)" "lvgl/lvgl branch" behind ;;
        esac
    else
        add_row "LVGL release/v$base branch" "$short" "?" "lvgl/lvgl branch" "lookup failed"
    fi
}

check_reuse() {
    local result
    result=$(resolve_reuse_pin "$REPO_ROOT/.github/workflows/ci.yml" "$REPO_ROOT/AGENTS.md")
    case "$result" in
        ok\ *) check_version reuse "${result#ok }" "PyPI" pypi_latest reuse ;;
        missing\ *) add_row reuse "?" "-" "${result#missing }" missing ;;
        invalid\ *) add_row reuse "${result#invalid }" "-" "ci.yml, AGENTS.md" invalid ;;
        *) add_row reuse "${result#disagree }" "-" "ci.yml, AGENTS.md" disagree ;;
    esac
}

# GCC and musl come from the OpenWrt toolchain archive, so they are compared
# with the newest OpenWrt release, not with GCC or musl upstream. When the
# OpenWrt index is down, check_version still reports a missing or malformed
# local pin first; only a valid pin becomes a failed lookup.
check_toolchain_pins() {
    local openwrt
    if openwrt=$(openwrt_latest); then
        check_version "GCC (OpenWrt $openwrt toolchain)" "${MIPS_TOOLCHAIN_GCC_VERSION:-}" \
            "OpenWrt $openwrt ramips/mt76x8" openwrt_gcc "$openwrt"
        check_version "musl (OpenWrt $openwrt toolchain)" "${MIPS_TOOLCHAIN_MUSL_VERSION:-}" \
            "openwrt v$openwrt toolchain/musl" openwrt_musl "$openwrt"
    else
        check_version "GCC (OpenWrt toolchain)" "${MIPS_TOOLCHAIN_GCC_VERSION:-}" "downloads.openwrt.org" false
        check_version "musl (OpenWrt toolchain)" "${MIPS_TOOLCHAIN_MUSL_VERSION:-}" "downloads.openwrt.org" false
    fi
}

run_live() {
    load_native_pins "$REPO_ROOT/tools/native-dependency-pins.sh"
    local hugo gitleaks musl_upstream
    hugo=$(pin_from_file "$REPO_ROOT/website/vendor-pins/hugo.version")
    gitleaks=$(sed -nE 's/^[[:space:]]*GITLEAKS_VERSION:[[:space:]]*"?([^"[:space:]]+)"?[[:space:]]*$/\1/p' \
        "$REPO_ROOT/.github/workflows/ci.yml" 2>/dev/null | head -n 1) || gitleaks=

    # Mbed TLS stays on the 4.x line that the printer build and notices use.
    # Every other GitHub-released pin is compared with the newest release of
    # any major version.
    check_version "Mbed TLS (4.x)" "${MBEDTLS_VERSION:-}" "Mbed-TLS/mbedtls releases" \
        gh_latest Mbed-TLS/mbedtls mbedtls- '^4\.'
    check_version "TF-PSA-Crypto (bundled with Mbed TLS)" "${TF_PSA_CRYPTO_VERSION:-}" \
        "Mbed-TLS/TF-PSA-Crypto releases" gh_latest Mbed-TLS/TF-PSA-Crypto tf-psa-crypto- .
    check_version libzmq "${ZMQ_VERSION:-}" "zeromq/libzmq releases" gh_latest zeromq/libzmq v .
    check_version "lighttpd (1.4.x)" "${LIGHTTPD_VERSION:-}" "download.lighttpd.net" \
        index_latest https://download.lighttpd.net/lighttpd/releases-1.4.x/ lighttpd .tar.xz
    check_version "OpenWrt toolchain" "${MIPS_TOOLCHAIN_RELEASE:-}" "downloads.openwrt.org" openwrt_latest

    check_toolchain_pins
    # Informational only: its outage must not fail the report.
    if musl_upstream=$(index_latest https://musl.libc.org/releases.html musl .tar.gz); then
        add_row "musl upstream (info)" "${MIPS_TOOLCHAIN_MUSL_VERSION:-}" "$musl_upstream" "musl.libc.org" "info: follows OpenWrt"
    else
        add_row "musl upstream (info)" "${MIPS_TOOLCHAIN_MUSL_VERSION:-}" "?" "musl.libc.org" "info: lookup failed"
    fi

    check_lvgl
    check_version Hugo "$hugo" "gohugoio/hugo releases; pin: website/vendor-pins/hugo.version" gh_latest gohugoio/hugo v .
    check_version Gitleaks "$gitleaks" "gitleaks/gitleaks releases; pin: ci.yml" gh_latest gitleaks/gitleaks v .
    check_reuse

    {
        echo "| Pin | Current | Latest | Source | Status |"
        echo "| --- | --- | --- | --- | --- |"
        printf '%s\n' "${ROWS[@]}"
    }
    if [ "$failed" -ne 0 ]; then
        echo "FAIL: at least one upstream lookup failed" >&2
        exit 1
    fi
    if [ "$behind" -ne 0 ]; then
        exit 3
    fi
}

# --- Offline self-test with canned upstream responses. ---

# Run CMD with INPUT on stdin; require success and exactly EXPECTED on stdout.
expect() {
    local label=$1 expected=$2 input=$3 actual status=0
    shift 3
    actual=$(printf '%s' "$input" | "$@") || status=$?
    if [ "$status" -ne 0 ] || [ "$expected" != "$actual" ]; then
        echo "FAIL: $label: expected '$expected' (exit 0), got '$actual' (exit $status)" >&2
        exit 1
    fi
    echo "PASS: $label"
}

# Run CMD with INPUT on stdin; require a nonzero exit.
expect_failure() {
    local label=$1 input=$2
    shift 2
    if printf '%s' "$input" | "$@" > /dev/null 2>&1; then
        echo "FAIL: $label: command succeeded" >&2
        exit 1
    fi
    echo "PASS: $label"
}

# VAR (default MBEDTLS_VERSION) after loading FILE in a subshell, or "unset".
native_pin_after_load() (
    local var=${2:-MBEDTLS_VERSION}
    unset MBEDTLS_VERSION MIPS_TOOLCHAIN_GCC_VERSION
    load_native_pins "$1"
    echo "${!var-unset}"
)

# Status and counters for the toolchain rows with the OpenWrt index down and
# no native pins loaded.
toolchain_rows_with_openwrt_down() (
    openwrt_latest() { return 1; }
    unset MIPS_TOOLCHAIN_GCC_VERSION MIPS_TOOLCHAIN_MUSL_VERSION
    ROWS=() behind=0 failed=0
    check_toolchain_pins
    printf '%s\n' "${ROWS[@]}" | awk -F' [|] ' '{ printf "%s;", $5 }'
    echo " $behind $failed"
)

run_selftest() {
    local releases='[
        {"tag_name": "mbedtls-4.4.0rc1", "draft": false, "prerelease": true},
        {"tag_name": "mbedtls-4.3.0", "draft": false, "prerelease": false},
        {"tag_name": "mbedtls-4.10.0", "draft": true, "prerelease": false},
        {"tag_name": "mbedtls-4.1.2", "draft": false, "prerelease": false},
        {"tag_name": "mbedtls-3.6.8", "draft": false, "prerelease": false},
        {"tag_name": "v9.9.9", "draft": false, "prerelease": false},
        {"tag_name": "qemu-benchmark-latest", "draft": false, "prerelease": true}
    ]'
    expect "GitHub release skips drafts, pre-releases, other lines" 4.3.0 \
        "$releases" latest_github_release mbedtls- '^4\.'
    expect "GitHub release honours the 3.x line" 3.6.8 "$releases" latest_github_release mbedtls- '^3\.'
    expect_failure "GitHub release with no match fails" "$releases" latest_github_release lighttpd- .

    local majors='[
        {"tag_name": "v0.170.1", "draft": false, "prerelease": false},
        {"tag_name": "v1.0.0", "draft": false, "prerelease": false},
        {"tag_name": "v1.1.0-beta", "draft": false, "prerelease": false}
    ]'
    expect "unscoped lookup finds a newer major line" 1.0.0 "$majors" latest_github_release v .
    expect "a pin on the old major line is behind" behind "" version_status 0.170.1 1.0.0

    local pages='[{"tag_name": "v4.3.4", "draft": false, "prerelease": false}]
[{"tag_name": "v4.3.5", "draft": false, "prerelease": false}]'
    expect "paginated release pages are all read" 4.3.5 "$pages" latest_github_release v .

    local lighttpd='<a href="lighttpd-1.4.9.tar.xz">x</a> <a href="lighttpd-1.4.85.tar.xz">x</a>
<a href="lighttpd-1.4.85.tar.xz.asc">x</a> <a href="lighttpd-1.4.84.tar.gz">x</a>'
    expect "lighttpd index orders 1.4.85 above 1.4.9" 1.4.85 "$lighttpd" latest_index_version lighttpd .tar.xz
    local musl='<a href="releases/musl-1.2.5.tar.gz">1.2.5</a> <a href="releases/musl-1.2.6.tar.gz">1.2.6</a>'
    expect "musl index" 1.2.6 "$musl" latest_index_version musl .tar.gz
    expect_failure "an index with no release links fails" "<html></html>" latest_index_version musl .tar.gz

    local openwrt='<a href="25.12.0-rc5/">x</a> <a href="25.12.6/">x</a> <a href="24.10.9/">x</a>
<a href="packages-25.12/">x</a> <a href="faillogs/">x</a>'
    expect "OpenWrt index skips -rc and packages-*" 25.12.6 "$openwrt" latest_openwrt_release
    local target='<a href="openwrt-toolchain-25.12.6-ramips-mt76x8_gcc-14.3.0_musl.Linux-x86_64.tar.zst">t</a>'
    expect "OpenWrt toolchain GCC" 14.3.0 "$target" openwrt_toolchain_gcc
    expect "OpenWrt musl PKG_VERSION" 1.2.5 $'PKG_NAME:=musl\nPKG_VERSION:=1.2.5\nPKG_RELEASE:=1\n' \
        makefile_pkg_version
    expect_failure "a Makefile without PKG_VERSION fails" $'PKG_NAME:=musl\n' makefile_pkg_version

    local pypi='{"info": {"version": "7.0.0a1"}, "releases": {
        "6.2.0": [{"yanked": false}], "6.3.0": [{"yanked": false}],
        "6.4.0": [{"yanked": true}], "6.5.0": [],
        "7.0.0a1": [{"yanked": false}], "7.0.0.dev1": [{"yanked": false}]}}'
    expect "PyPI skips pre-releases, yanked, and empty releases" 6.3.0 "$pypi" latest_pypi_release

    expect "compare status" "ahead 28 0" '{"status":"ahead","ahead_by":28,"behind_by":0}' compare_status
    expect_failure "a compare error body fails" \
        '{"message":"Not Found","status":"404"}' compare_status
    expect_failure "an unknown compare status fails" \
        '{"status":"weird","ahead_by":0,"behind_by":0}' compare_status

    expect "equal versions are current" current "" version_status 1.4.85 1.4.85
    expect "1.4.10 is newer than 1.4.9" behind "" version_status 1.4.9 1.4.10
    expect "a newer pin is current" current "" version_status 4.3.1 4.3.0
    expect "4.1.2 is behind 4.3.0" behind "" version_status 4.1.2 4.3.0
    expect "a malformed pin is invalid, not current" invalid "" version_status 9.9.9-local 4.3.0
    expect "an empty pin is invalid" invalid "" version_status "" 4.3.0

    local dir
    dir=$(mktemp -d)
    # shellcheck disable=SC2064  # Expand now: dir is local to this function.
    trap "rm -rf '$dir'" EXIT
    printf 'run: pipx run --spec reuse==6.2.0 reuse lint\n' > "$dir/ci.yml"
    printf 'pipx run --spec "reuse[charset-normalizer]==6.2.0" reuse lint\n' > "$dir/agents.md"
    printf 'pipx run --spec "reuse[charset-normalizer]==6.1.0" reuse lint\n' > "$dir/older.md"
    printf 'no linter pin here\n' > "$dir/none.md"
    printf 'pipx run --spec reuse==6.2.0x reuse lint\n' > "$dir/bad.yml"
    expect "reuse pins that agree resolve" "ok 6.2.0" "" resolve_reuse_pin "$dir/ci.yml" "$dir/agents.md"
    expect "a file without a reuse pin is missing" "missing $dir/none.md" "" \
        resolve_reuse_pin "$dir/ci.yml" "$dir/none.md"
    expect "reuse pins that differ disagree" "disagree 6.1.0 6.2.0" "" \
        resolve_reuse_pin "$dir/ci.yml" "$dir/older.md"
    expect "a malformed reuse pin is invalid" "invalid 6.2.0x" "" resolve_reuse_pin "$dir/bad.yml"

    # Quote, Markdown, and bare spellings of one pin all agree.
    printf "run: pipx run --spec 'reuse==6.2.0' reuse lint\n" > "$dir/single.yml"
    printf 'run: pipx run --spec "reuse==6.2.0" reuse lint\n' > "$dir/double.yml"
    # The backticks are literal Markdown.
    # shellcheck disable=SC2016
    printf 'Run `pipx run --spec reuse==6.2.0 reuse lint`.\n' > "$dir/markdown.md"
    printf "pipx run --spec 'reuse[charset-normalizer]==6.2.0' reuse lint\n" > "$dir/extra.md"
    expect "single-quoted, double-quoted, bare, Markdown, and extra specs agree" "ok 6.2.0" "" \
        resolve_reuse_pin "$dir/single.yml" "$dir/double.yml" "$dir/ci.yml" "$dir/markdown.md" \
        "$dir/extra.md" "$dir/agents.md"
    printf "pipx run --spec 'reuse==6.2.0x' reuse lint\n" > "$dir/quoted-bad.yml"
    expect "a quoted suffix stays invalid" "invalid 6.2.0x" "" resolve_reuse_pin "$dir/quoted-bad.yml"
    expect "a missing reuse pin file is missing, not an abort" "missing $dir/absent.yml" "" \
        resolve_reuse_pin "$dir/absent.yml"

    # A missing local pin file is a reported pin problem, not an abort.
    printf ' 0.170.1\n' > "$dir/hugo.version"
    expect "a pin file is read and trimmed" 0.170.1 "" pin_from_file "$dir/hugo.version"
    expect "a missing pin file reads as empty" "" "" pin_from_file "$dir/absent.version"
    expect "a missing native pin file loads nothing" "unset" "" native_pin_after_load "$dir/absent.sh"

    ROWS=() behind=0 failed=0
    check_version Hugo "" "pin: website/vendor-pins/hugo.version" false
    expect "an empty local pin is reported missing without a lookup" \
        "| Hugo | ? | - | pin: website/vendor-pins/hugo.version | missing | 1 0" "" \
        echo "${ROWS[0]} $behind $failed"
    ROWS=() behind=0 failed=0
    check_version Gitleaks "8.30.1x" "pin: ci.yml" false
    expect "a malformed local pin is invalid" "| Gitleaks | 8.30.1x | - | pin: ci.yml | invalid | 1 0" "" \
        echo "${ROWS[0]} $behind $failed"

    # A malformed upstream value is a failed lookup, never a local pin problem.
    expect_failure "PKG_VERSION:=1..2 fails the musl parser" $'PKG_VERSION:=1..2\n' makefile_pkg_version
    ROWS=() behind=0 failed=0
    check_version musl 1.2.5 "openwrt musl" echo 1..2
    expect "a malformed upstream value is a lookup failure" \
        "| musl | 1.2.5 | 1..2 | openwrt musl | lookup failed | 0 1" "" echo "${ROWS[0]} $behind $failed"
    ROWS=() behind=0 failed=0
    check_version Hugo 0.170.1 "hugo" echo ""
    expect "an empty upstream value is a lookup failure" "0 1" "" echo "$behind $failed"

    # A pin file that loses a variable it interpolates still loads under nounset.
    # The ${...} is literal fixture text.
    # shellcheck disable=SC2016
    printf 'MBEDTLS_VERSION=4.1.2\nMIPS_TOOLCHAIN_ARCHIVE="toolchain_gcc-${MIPS_TOOLCHAIN_GCC_VERSION}_musl"\n' \
        > "$dir/partial-pins.sh"
    expect "a pin file missing an interpolated variable still loads" 4.1.2 "" \
        native_pin_after_load "$dir/partial-pins.sh"
    expect "the removed variable stays unset" unset "" \
        native_pin_after_load "$dir/partial-pins.sh" MIPS_TOOLCHAIN_GCC_VERSION

    # Missing local toolchain pins win over a failed OpenWrt lookup.
    expect "missing toolchain pins are missing, not lookup failures" "missing |;missing |; 1 0" "" \
        toolchain_rows_with_openwrt_down

    # A reuse requirement that is not exactly ==X.Y.Z is invalid, not truncated.
    printf "run: pipx run --spec 'reuse==6.2.0,==6.3.0' reuse lint\n" > "$dir/comma.yml"
    expect "a compound reuse requirement is invalid" "invalid 6.2.0,==6.3.0" "" \
        resolve_reuse_pin "$dir/comma.yml" "$dir/agents.md"
    printf 'pipx run --spec "reuse>=6.2.0" reuse lint\n' > "$dir/range.md"
    expect "a reuse range is invalid" "invalid reuse>=6.2.0" "" resolve_reuse_pin "$dir/range.md"

    # Row classification: info lookups never fail the run.
    ROWS=() behind=0 failed=0
    add_row "musl upstream (info)" 1.2.5 "?" musl.libc.org "info: lookup failed"
    expect "an informational lookup failure is not fatal" "0 0" "" echo "$behind $failed"
    add_row reuse "?" "-" ci.yml missing
    expect "a missing pin is actionable" "1 0" "" echo "$behind $failed"
    add_row Hugo 0.170.1 "?" gohugoio "lookup failed"
    expect "a tracked lookup failure is fatal" "1 1" "" echo "$behind $failed"

    echo "Upstream pin self-test: PASS"
}

case "${1:-}" in
    --selftest) run_selftest ;;
    "") run_live ;;
    *)
        echo "usage: $0 [--selftest]" >&2
        exit 64
        ;;
esac
