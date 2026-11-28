#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Syntax-check and shellcheck every tracked shell script at warning severity.
# A tracked file is a shell script when it ends in .sh or starts with an sh or
# bash shebang, so init scripts and new scripts are found without a list.
# /bin/sh scripts are checked as dash: Debian runs them with dash and the
# printer runs them with BusyBox ash, and both support `local`.
#
# usage: tools/check-shell-scripts.sh [--selftest | --classify PATH]
#   --classify prints sh or bash for a shell script, unknown for a .sh file
#   with another interpreter, and nothing otherwise.

set -eu

# Exact bytes of earlier releases that installer tests match; never edited.
FIXTURE_PREFIX=tools/fixtures/

# Print "sh" or "bash" for a shell script, nothing for other files, and
# "unknown" for a .sh file with another interpreter.
script_shell() {
    first=$(head -c 256 -- "$1" | head -n 1 | tr -d '\000')
    case "$first" in
        '#!'*/env\ bash|'#!'*/env\ bash\ *|'#!'*/bash|'#!'*/bash\ *) echo bash ;;
        '#!'*/env\ sh|'#!'*/env\ sh\ *|'#!'*/sh|'#!'*/sh\ *) echo sh ;;
        '#!'*)
            case "$1" in
                *.sh) echo unknown ;;
            esac
            ;;
        *)
            # Sourced helpers such as native-dependency-pins.sh have no shebang.
            case "$1" in
                *.sh) echo sh ;;
            esac
            ;;
    esac
}

check_tree() (
    root=$1
    cd -- "$root"
    list=$(mktemp)
    sh_list=$(mktemp)
    bash_list=$(mktemp)
    trap 'rm -f "$list" "$sh_list" "$bash_list"' EXIT
    git ls-files -z | tr '\000' '\n' > "$list"
    checked=0
    failed=0
    while IFS= read -r path; do
        case "$path" in
            "$FIXTURE_PREFIX"*) continue ;;
        esac
        if [ ! -f "$path" ] || [ -L "$path" ]; then
            continue
        fi
        shell=$(script_shell "$path")
        case "$shell" in
            bash) printf '%s\000' "$path" >> "$bash_list" ;;
            sh) printf '%s\000' "$path" >> "$sh_list" ;;
            unknown)
                echo "FAIL: $path ends in .sh but has a non-shell shebang" >&2
                failed=1
                continue
                ;;
            *) continue ;;
        esac
        checked=$((checked + 1))
        if ! "$shell" -n -- "$path"; then
            echo "FAIL: $shell -n rejected $path" >&2
            failed=1
        fi
    done < "$list"
    for dialect in dash bash; do
        if [ "$dialect" = dash ]; then
            paths=$sh_list
        else
            paths=$bash_list
        fi
        [ -s "$paths" ] || continue
        if ! xargs -0 shellcheck --severity=warning --shell="$dialect" -- < "$paths"; then
            failed=1
        fi
    done
    if [ "$checked" -eq 0 ]; then
        echo "FAIL: found no tracked shell scripts in $root" >&2
        exit 1
    fi
    if [ "$failed" -ne 0 ]; then
        echo "FAIL: shell script checks failed ($checked scripts checked)" >&2
        exit 1
    fi
    echo "PASS: $checked tracked shell scripts pass syntax and shellcheck warning checks"
)

# Fixture scripts are literal text, so their expansions stay single-quoted.
# shellcheck disable=SC2016
run_selftest() (
    fixture=$(mktemp -d)
    trap 'rm -rf "$fixture"' EXIT
    repo=$fixture/repo
    mkdir -p "$repo/tools/fixtures" "$repo/web/init"
    git -C "$repo" init -q

    printf '%s\n' '#!/bin/sh' 'f() {' '    local value=1' '    echo "$value"' '}' 'f' \
        > "$repo/tools/clean.sh"
    printf '%s\n' '#!/usr/bin/env bash' 'values=(a b)' 'echo "${values[@]}"' \
        > "$repo/tools/clean-bash.sh"
    printf '%s\n' 'PIN=1' 'export PIN' > "$repo/tools/sourced.sh"
    printf '%s\n' '#!/bin/sh /etc/rc.common' 'start_service() {' '    echo start' '}' \
        > "$repo/web/init/clean.init"
    # Excluded fixture with a warning: an unused variable.
    printf '%s\n' '#!/bin/sh' 'unused=1' > "$repo/tools/fixtures/legacy"
    git -C "$repo" add -A
    if ! check_tree "$repo" > "$fixture/out" 2>&1; then
        cat "$fixture/out" >&2
        echo "FAIL: selftest rejected clean scripts" >&2
        exit 1
    fi
    grep -Fq 'PASS: 4 tracked shell scripts' "$fixture/out" || {
        cat "$fixture/out" >&2
        echo "FAIL: selftest did not check exactly the 4 non-fixture scripts" >&2
        exit 1
    }

    expect_failure() {
        label=$1
        path=$2
        shift 2
        printf '%s\n' "$@" > "$repo/$path"
        git -C "$repo" add -A
        if check_tree "$repo" > "$fixture/out" 2>&1; then
            cat "$fixture/out" >&2
            echo "FAIL: selftest accepted $label" >&2
            exit 1
        fi
        if ! grep -Fq "$path" "$fixture/out"; then
            cat "$fixture/out" >&2
            echo "FAIL: selftest did not report $path for $label" >&2
            exit 1
        fi
        git -C "$repo" rm -q --cached -- "$path"
        rm -f -- "$repo/$path"
    }

    expect_failure 'a warning in a new .sh script' tools/new.sh \
        '#!/bin/sh' 'unused=1'
    expect_failure 'a warning in an extensionless init script' web/init/new.init \
        '#!/bin/sh /etc/rc.common' 'unused=1'
    expect_failure 'a syntax error in an extensionless script' tools/new-tool \
        '#!/bin/sh' 'if true; then'
    # dash -n accepts [[ as a command name; shellcheck must reject it.
    expect_failure 'a bash-only test in a /bin/sh script' tools/bashism.sh \
        '#!/bin/sh' 'if [[ -n "${1:-}" ]]; then echo set; fi'
    expect_failure 'a .sh file with a non-shell shebang' tools/python.sh \
        '#!/usr/bin/env python3' 'print(1)'
    echo "PASS: shell script check selftest"
)

case "${1:-}" in
    --selftest) run_selftest ;;
    --classify)
        [ "$#" -eq 2 ] || { echo "usage: $0 --classify PATH" >&2; exit 2; }
        script_shell "$2"
        ;;
    '') check_tree "$(cd "$(dirname "$0")/.." && pwd)" ;;
    *)
        echo "usage: $0 [--selftest | --classify PATH]" >&2
        exit 2
        ;;
esac
