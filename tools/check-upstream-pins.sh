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
# Exit status: 0 when every pin is current, 3 when any pin is behind or two
# pins disagree, and 1 when an upstream lookup fails. A failed lookup is
# never reported as current. GitHub lookups use `gh api` (set GH_TOKEN in CI).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# --- Parsers: each reads an upstream response on stdin. ---

# Highest non-draft, non-prerelease GitHub release whose tag is
# PREFIX<version> and whose version matches LINE (an ERE).
latest_github_release() {
    local prefix=$1 line=$2
    jq -r --arg prefix "$prefix" '
        .[] | select(.draft == false and .prerelease == false) | .tag_name
        | select(startswith($prefix)) | ltrimstr($prefix)' |
        grep -E "^[0-9]+(\.[0-9]+)*$" | grep -E "$line" | sort -V | tail -n 1
}

# Highest stable version in an HTML index for links named NAME-<version>SUFFIX.
latest_index_version() {
    local name=$1 suffix=$2
    grep -oE "${name}-[0-9]+(\.[0-9]+)+${suffix//./\\.}" |
        sed -E "s/^${name}-//; s/${suffix//./\\.}\$//" | sort -uV | tail -n 1
}

# Highest final OpenWrt release directory (skips -rc and packages-* entries).
latest_openwrt_release() {
    grep -oE 'href="[0-9]+\.[0-9]+\.[0-9]+/"' | sed -E 's/^href="//; s/\/"$//' |
        sort -uV | tail -n 1
}

# GCC version in an OpenWrt target directory's toolchain archive name.
openwrt_toolchain_gcc() {
    grep -oE 'openwrt-toolchain-[^"<>]*_gcc-[0-9]+(\.[0-9]+)+_musl' |
        sed -E 's/.*_gcc-//; s/_musl$//' | sort -uV | tail -n 1
}

# PKG_VERSION from an OpenWrt package Makefile.
makefile_pkg_version() {
    sed -nE 's/^PKG_VERSION:=([0-9][0-9.]*)[[:space:]]*$/\1/p' | head -n 1
}

# GitHub compare API status: identical, ahead, behind, or diverged.
compare_status() {
    jq -r '"\(.status) \(.ahead_by) \(.behind_by)"'
}

# "current" when CURRENT >= LATEST in version order, otherwise "behind".
version_status() {
    local current=$1 latest=$2
    if [ "$current" = "$latest" ] ||
        [ "$(printf '%s\n%s\n' "$current" "$latest" | sort -V | tail -n 1)" = "$current" ]
    then
        echo current
    else
        echo behind
    fi
}

# --- Live lookups ---

fetch_url() {
    curl -fsSL --retry 2 --max-time 30 -- "$1"
}

fetch_gh() {
    gh api -- "$1"
}

ROWS=()
behind=0
failed=0

add_row() {
    local name=$1 current=$2 latest=$3 source=$4 status=$5
    case "$status" in
        behind|disagree) behind=1 ;;
        "lookup failed") failed=1 ;;
    esac
    ROWS+=("| $name | $current | $latest | $source | $status |")
}

# Compare one pin against a lookup command that prints the latest version.
check_version() {
    local name=$1 current=$2 source=$3
    shift 3
    local latest
    if latest=$("$@") && [ -n "$latest" ]; then
        add_row "$name" "$current" "$latest" "$source" "$(version_status "$current" "$latest")"
    else
        add_row "$name" "$current" "?" "$source" "lookup failed"
    fi
}

gh_latest() {
    fetch_gh "repos/$1/releases?per_page=100" | latest_github_release "$2" "$3"
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
    fetch_url "https://pypi.org/pypi/$1/json" | jq -er '.info.version'
}

check_lvgl() {
    local revision line major short base latest result branch
    revision=$(git -C "$REPO_ROOT" ls-tree HEAD ui/lib/lvgl | awk '$2 == "commit" { print $3 }')
    # SOURCE_PROVENANCE.md records the maintenance line, e.g. "v9.6 maintenance".
    # The backticks are literal Markdown, not command substitution.
    # shellcheck disable=SC2016
    line=$(sed -nE 's/^\| LVGL \|.*\| v([0-9]+\.[0-9]+) maintenance \/ `?([0-9a-f]{40})`? \|.*/\1 \2/p' \
        "$REPO_ROOT/docs/SOURCE_PROVENANCE.md" | head -n 1)
    short=${revision:0:7}
    if [ -z "$revision" ] || [ -z "$line" ]; then
        add_row "LVGL release" "?" "?" "ui/lib/lvgl, docs/SOURCE_PROVENANCE.md" "lookup failed"
        return
    fi
    if [ "${line#* }" != "$revision" ]; then
        add_row "LVGL submodule" "$short" "${line#* }" "docs/SOURCE_PROVENANCE.md" disagree
    fi
    base=${line%% *}
    major=${base%%.*}
    # The pin must contain the newest release in its major line.
    if latest=$(gh_latest lvgl/lvgl v "^${major}\\.") && [ -n "$latest" ] &&
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
        local state ahead
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
    local versions
    versions=$(grep -ohE 'reuse(\[[^]]*\])?==[0-9]+(\.[0-9]+)+' \
        "$REPO_ROOT/.github/workflows/ci.yml" "$REPO_ROOT/AGENTS.md" | sed 's/.*==//' | sort -uV)
    if [ -z "$versions" ]; then
        add_row reuse "?" "?" "PyPI" "lookup failed"
    elif [ "$(printf '%s\n' "$versions" | wc -l)" -gt 1 ]; then
        add_row reuse "$(printf '%s' "$versions" | tr '\n' ' ')" "-" "ci.yml, AGENTS.md" disagree
    else
        check_version reuse "$versions" "PyPI" pypi_latest reuse
    fi
}

run_live() {
    # shellcheck source=tools/native-dependency-pins.sh
    # shellcheck disable=SC1091
    . "$REPO_ROOT/tools/native-dependency-pins.sh"
    local hugo gitleaks openwrt
    hugo=$(tr -d '[:space:]' < "$REPO_ROOT/website/vendor-pins/hugo.version")
    gitleaks=$(sed -nE 's/^[[:space:]]*GITLEAKS_VERSION:[[:space:]]*"?([0-9.]+)"?[[:space:]]*$/\1/p' \
        "$REPO_ROOT/.github/workflows/ci.yml" | head -n 1)

    check_version "Mbed TLS (4.x)" "$MBEDTLS_VERSION" "Mbed-TLS/mbedtls releases" \
        gh_latest Mbed-TLS/mbedtls mbedtls- '^4\.'
    check_version "TF-PSA-Crypto (bundled with Mbed TLS)" "$TF_PSA_CRYPTO_VERSION" \
        "Mbed-TLS/TF-PSA-Crypto releases" gh_latest Mbed-TLS/TF-PSA-Crypto tf-psa-crypto- '^1\.'
    check_version libzmq "$ZMQ_VERSION" "zeromq/libzmq releases" gh_latest zeromq/libzmq v '^4\.'
    check_version "lighttpd (1.4.x)" "$LIGHTTPD_VERSION" "download.lighttpd.net" \
        index_latest https://download.lighttpd.net/lighttpd/releases-1.4.x/ lighttpd .tar.xz
    check_version "OpenWrt toolchain" "$MIPS_TOOLCHAIN_RELEASE" "downloads.openwrt.org" openwrt_latest

    # GCC and musl come from the OpenWrt toolchain archive, so they are
    # compared with the newest OpenWrt release, not with GCC or musl upstream.
    if openwrt=$(openwrt_latest) && [ -n "$openwrt" ]; then
        check_version "GCC (OpenWrt $openwrt toolchain)" "$MIPS_TOOLCHAIN_GCC_VERSION" \
            "OpenWrt $openwrt ramips/mt76x8" openwrt_gcc "$openwrt"
        check_version "musl (OpenWrt $openwrt toolchain)" "$MIPS_TOOLCHAIN_MUSL_VERSION" \
            "openwrt v$openwrt toolchain/musl" openwrt_musl "$openwrt"
    else
        add_row "GCC (OpenWrt toolchain)" "$MIPS_TOOLCHAIN_GCC_VERSION" "?" "downloads.openwrt.org" "lookup failed"
        add_row "musl (OpenWrt toolchain)" "$MIPS_TOOLCHAIN_MUSL_VERSION" "?" "downloads.openwrt.org" "lookup failed"
    fi
    local musl_upstream
    if musl_upstream=$(index_latest https://musl.libc.org/releases.html musl .tar.gz) && [ -n "$musl_upstream" ]; then
        add_row "musl upstream (info)" "$MIPS_TOOLCHAIN_MUSL_VERSION" "$musl_upstream" "musl.libc.org" "info: follows OpenWrt"
    else
        add_row "musl upstream (info)" "$MIPS_TOOLCHAIN_MUSL_VERSION" "?" "musl.libc.org" "lookup failed"
    fi

    check_lvgl
    check_version Hugo "$hugo" "gohugoio/hugo releases" gh_latest gohugoio/hugo v '^0\.'
    if [ -n "$gitleaks" ]; then
        check_version Gitleaks "$gitleaks" "gitleaks/gitleaks releases" gh_latest gitleaks/gitleaks v '^8\.'
    else
        add_row Gitleaks "?" "?" ".github/workflows/ci.yml" "lookup failed"
    fi
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

expect() {
    local label=$1 expected=$2 actual=$3
    if [ "$expected" != "$actual" ]; then
        echo "FAIL: $label: expected '$expected', got '$actual'" >&2
        exit 1
    fi
    echo "PASS: $label"
}

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
        "$(printf '%s' "$releases" | latest_github_release mbedtls- '^4\.')"
    expect "GitHub release honours the 3.x line" 3.6.8 \
        "$(printf '%s' "$releases" | latest_github_release mbedtls- '^3\.')"
    expect "GitHub release with no match is empty" "" \
        "$(printf '%s' "$releases" | latest_github_release lighttpd- '^1\.')"

    local lighttpd='<a href="lighttpd-1.4.9.tar.xz">x</a> <a href="lighttpd-1.4.85.tar.xz">x</a>
<a href="lighttpd-1.4.85.tar.xz.asc">x</a> <a href="lighttpd-1.4.84.tar.gz">x</a>'
    expect "lighttpd index orders 1.4.85 above 1.4.9" 1.4.85 \
        "$(printf '%s' "$lighttpd" | latest_index_version lighttpd .tar.xz)"
    local musl='<a href="releases/musl-1.2.5.tar.gz">1.2.5</a> <a href="releases/musl-1.2.6.tar.gz">1.2.6</a>'
    expect "musl index" 1.2.6 "$(printf '%s' "$musl" | latest_index_version musl .tar.gz)"

    local openwrt='<a href="25.12.0-rc5/">x</a> <a href="25.12.6/">x</a> <a href="24.10.9/">x</a>
<a href="packages-25.12/">x</a> <a href="faillogs/">x</a>'
    expect "OpenWrt index skips -rc and packages-*" 25.12.6 \
        "$(printf '%s' "$openwrt" | latest_openwrt_release)"
    local target='<a href="openwrt-toolchain-25.12.6-ramips-mt76x8_gcc-14.3.0_musl.Linux-x86_64.tar.zst">t</a>'
    expect "OpenWrt toolchain GCC" 14.3.0 "$(printf '%s' "$target" | openwrt_toolchain_gcc)"
    expect "OpenWrt musl PKG_VERSION" 1.2.5 \
        "$(printf 'PKG_NAME:=musl\nPKG_VERSION:=1.2.5\nPKG_RELEASE:=1\n' | makefile_pkg_version)"

    expect "compare status" "ahead 28 0" \
        "$(printf '{"status":"ahead","ahead_by":28,"behind_by":0}' | compare_status)"

    expect "equal versions are current" current "$(version_status 1.4.85 1.4.85)"
    expect "1.4.10 is newer than 1.4.9" behind "$(version_status 1.4.9 1.4.10)"
    expect "a newer pin is current" current "$(version_status 4.3.1 4.3.0)"
    expect "4.1.2 is behind 4.3.0" behind "$(version_status 4.1.2 4.3.0)"

    if ! printf 'not json' | latest_github_release v '^1\.' > /dev/null 2>&1; then
        echo "PASS: malformed release JSON fails the lookup"
    else
        echo "FAIL: malformed release JSON was accepted" >&2
        exit 1
    fi
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
