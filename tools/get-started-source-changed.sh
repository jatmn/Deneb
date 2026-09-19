#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Report whether Deneb_get_started.img inputs changed between two commits.
# Prints changed=true|false. Exit 0 on success, 2 on usage/input errors.

set -euo pipefail

usage() {
    cat <<'USAGE'
usage: tools/get-started-source-changed.sh --since COMMIT [options]

Compare get-started image inputs since COMMIT.

Options:
  --since COMMIT     Base commit, tag, or SHA (required except --selftest)
  --until COMMIT     Tip commit (default: HEAD)
  --selftest         Run embedded checks and exit
  -h, --help         Show this help
USAGE
}

die() {
    echo "$*" >&2
    exit 2
}

script_dir=$(cd "$(dirname "$0")" && pwd)
paths_file=$script_dir/get-started-source-paths.txt
since=
until_rev=HEAD
selftest=false

while [ "$#" -gt 0 ]; do
    case "$1" in
        --since)
            since=${2:?missing value}
            shift 2
            ;;
        --until)
            until_rev=${2:?missing value}
            shift 2
            ;;
        --selftest)
            selftest=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown option: $1"
            ;;
    esac
done

write_git_pathspecs() {
    dest=$1
    spec=
    : > "$dest"
    [ -f "$paths_file" ] || die "Missing get-started path list: $paths_file"
    while IFS= read -r spec || [ -n "$spec" ]; do
        case "$spec" in
            ''|'#'*)
                continue
                ;;
        esac
        printf '%s\n' "$spec" >> "$dest"
    done < "$paths_file"
    [ -s "$dest" ] || die "No get-started paths listed in $paths_file"
}

matches_get_started_source() {
    path=$1
    spec=
    while IFS= read -r spec || [ -n "$spec" ]; do
        case "$spec" in
            ''|'#'*)
                continue
                ;;
        esac
        case "$path" in
            "$spec"|"$spec"/*)
                return 0
                ;;
        esac
    done < "$paths_file"
    return 1
}

report_changed() {
    value=$1
    printf 'changed=%s\n' "$value"
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        printf 'changed=%s\n' "$value" >> "$GITHUB_OUTPUT"
    fi
}

run_selftest() {
    tmp=$(mktemp -d)
    trap 'rm -rf "$tmp"' EXIT
    repo=$tmp/repo
    git init -q "$repo"
    git -C "$repo" config user.email ci-selftest@example.invalid
    git -C "$repo" config user.name "CI self-test"
    mkdir -p "$repo/packages/ssh-bootstrap" "$repo/assets/branding" "$repo/tools" "$repo/docs"
    printf 'overlay\n' > "$repo/packages/ssh-bootstrap/update.sh"
    printf 'brand\n' > "$repo/assets/branding/splash.png"
    printf 'docs\n' > "$repo/docs/guide.md"
    git -C "$repo" add .
    git -C "$repo" commit -qm base
    before=$(git -C "$repo" rev-parse HEAD)

    printf 'docs2\n' > "$repo/docs/guide.md"
    git -C "$repo" add .
    git -C "$repo" commit -qm docs-only
    output=$(cd "$repo" && bash "$script_dir/get-started-source-changed.sh" --since "$before")
    printf '%s\n' "$output" | grep -qx 'changed=false'

    after=$(git -C "$repo" rev-parse HEAD)
    printf 'overlay2\n' > "$repo/packages/ssh-bootstrap/update.sh"
    git -C "$repo" add .
    git -C "$repo" commit -qm overlay
    output=$(cd "$repo" && bash "$script_dir/get-started-source-changed.sh" --since "$after")
    printf '%s\n' "$output" | grep -qx 'changed=true'

    grep -Fq 'get-started-source-paths.txt' "$script_dir/select-ci-validation.sh"
    matches_get_started_source 'packages/ssh-bootstrap/manifest.txt'
    matches_get_started_source 'assets/branding/deneb_splash_480x272.png'
    matches_get_started_source 'tools/build-get-started.sh'
    if matches_get_started_source 'printsvc/src/main.c'; then
        echo 'printsvc must not count as get-started source' >&2
        exit 1
    fi

    printf 'Get-started source-changed self-test: PASS\n'
}

if [ "$selftest" = true ]; then
    run_selftest
    exit 0
fi

[ -n "$since" ] || die "Missing --since"

git cat-file -e "${since}^{commit}" 2>/dev/null || die "Unknown --since revision: $since"
git cat-file -e "${until_rev}^{commit}" 2>/dev/null || die "Unknown --until revision: $until_rev"

pathspec_file=$(mktemp)
trap 'rm -f "$pathspec_file"' EXIT
write_git_pathspecs "$pathspec_file"

# Word-split is intentional: get-started pathspecs do not contain spaces.
# shellcheck disable=SC2046
if git diff --quiet "$since" "$until_rev" -- $(cat "$pathspec_file"); then
    report_changed false
else
    status=$?
    [ "$status" -eq 1 ] || die "git diff failed with status $status"
    report_changed true
fi
