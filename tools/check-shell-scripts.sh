#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
#
# Syntax-check and shellcheck every tracked shell script at warning severity.
# A tracked file is a shell script when it ends in .sh or its shebang names a
# shell, so init scripts and new scripts are found without a list.
# sh, dash, ash, and BusyBox scripts are checked as dash: Debian runs /bin/sh
# scripts with dash and the printer runs them with BusyBox ash, and both
# support `local`.
#
# usage: tools/check-shell-scripts.sh [--selftest | --classify PATH]
#   --classify prints sh or bash for a checked shell script, unsupported for
#   another shell or a .sh file with a non-shell interpreter, and nothing for
#   other files.

set -euo pipefail

# Exact bytes of earlier releases that installer tests match; never edited.
FIXTURE_PREFIX=tools/fixtures/

# Print the interpreter's basename from the shebang in $1, followed by its
# first argument for BusyBox. Print nothing when there is no shebang.
shebang_interpreter() {
    local first
    first=$(head -c 256 -- "$1" | head -n 1 | tr -d '\000\r')
    [[ "$first" == '#!'* ]] || return 0
    local -a words
    read -r -a words <<< "${first#\#!}"
    [[ ${#words[@]} -gt 0 ]] || { echo unknown; return 0; }
    local i=0 interpreter=${words[0]##*/}
    if [[ "$interpreter" == env ]]; then
        # Skip env options and NAME=value assignments; -S splits the rest.
        i=1
        while [[ $i -lt ${#words[@]} ]]; do
            case "${words[$i]}" in
                -u|--unset|-C|--chdir) i=$((i + 2)) ;;
                -S*)
                    if [[ "${words[$i]}" != -S ]]; then
                        words[i]=${words[$i]#-S}
                        break
                    fi
                    i=$((i + 1))
                    ;;
                -*|*=*) i=$((i + 1)) ;;
                *) break ;;
            esac
        done
        [[ $i -lt ${#words[@]} ]] || { echo unknown; return 0; }
        interpreter=${words[$i]##*/}
    fi
    if [[ "$interpreter" == busybox ]]; then
        printf 'busybox %s\n' "${words[$((i + 1))]:-}"
    else
        printf '%s\n' "$interpreter"
    fi
}

# Print the dialect for $1: sh, bash, unsupported, or nothing.
script_shell() {
    local interpreter
    interpreter=$(shebang_interpreter "$1")
    case "$interpreter" in
        bash) echo bash ;;
        sh|dash|ash|'busybox sh'|'busybox ash') echo sh ;;
        ksh|mksh|pdksh|zsh|yash|posh|csh|tcsh|fish|unknown)
            echo unsupported
            ;;
        '')
            # Sourced helpers such as native-dependency-pins.sh have no shebang.
            if [[ "$1" == *.sh ]]; then
                echo sh
            fi
            ;;
        *)
            if [[ "$1" == *.sh ]]; then
                echo unsupported
            fi
            ;;
    esac
}

check_tree() (
    local root=$1
    CDPATH='' cd -- "$root"
    local sh_list bash_list
    sh_list=$(mktemp)
    bash_list=$(mktemp)
    trap 'rm -f "$sh_list" "$bash_list"' EXIT
    local checked=0 failed=0 path shell
    while IFS= read -r -d '' path; do
        case "$path" in
            "$FIXTURE_PREFIX"*) continue ;;
        esac
        if [[ -L "$path" ]]; then
            # A link would check a target outside this list or check one
            # script twice; keep exactly one tracked path per script.
            if [[ "$path" == *.sh ]] || { [[ -f "$path" ]] && [[ -n "$(script_shell "$path")" ]]; }; then
                echo "FAIL: $path is a tracked symlink to a shell script; track the script itself" >&2
                failed=1
            fi
            continue
        fi
        [[ -f "$path" ]] || continue
        shell=$(script_shell "$path")
        case "$shell" in
            bash) printf '%s\0' "$path" >> "$bash_list" ;;
            sh) printf '%s\0' "$path" >> "$sh_list" ;;
            unsupported)
                echo "FAIL: $path has a shebang this check does not support: $(shebang_interpreter "$path")" >&2
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
    done < <(git ls-files -z)
    if [[ -s "$sh_list" ]] &&
        ! xargs -0 shellcheck --severity=warning --shell=dash -- < "$sh_list"; then
        failed=1
    fi
    if [[ -s "$bash_list" ]] &&
        ! xargs -0 shellcheck --severity=warning --shell=bash -- < "$bash_list"; then
        failed=1
    fi
    if [[ $checked -eq 0 ]]; then
        echo "FAIL: found no tracked shell scripts in $root" >&2
        exit 1
    fi
    if [[ $failed -ne 0 ]]; then
        echo "FAIL: shell script checks failed ($checked scripts checked)" >&2
        exit 1
    fi
    echo "PASS: $checked tracked shell scripts pass syntax and shellcheck warning checks"
)

# Fixture scripts are literal text, so their expansions stay single-quoted.
# shellcheck disable=SC2016
run_selftest() (
    local self fixture repo
    self=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
    fixture=$(mktemp -d)
    trap 'rm -rf "$fixture"' EXIT
    repo=$fixture/repo
    mkdir -p "$repo/tools/fixtures" "$repo/web/init"
    git -C "$repo" init -q

    printf '%s\n' '#!/bin/sh' 'f() {' '    local value=1' '    echo "$value"' '}' 'f' \
        > "$repo/tools/clean.sh"
    printf '%s\n' '#!/usr/bin/env bash' 'values=(a b)' 'echo "${values[@]}"' \
        > "$repo/tools/clean-bash.sh"
    # The previous CI regex accepted env -S bash, so this must stay checked.
    printf '%s\n' '#!/usr/bin/env -S bash -e' 'values=(a b)' 'echo "${values[@]}"' \
        > "$repo/tools/env-split.sh"
    printf '%s\n' '#!/bin/busybox sh' 'echo busybox' > "$repo/tools/busybox-tool"
    printf '%s\n' 'PIN=1' 'export PIN' > "$repo/tools/sourced.sh"
    printf '%s\n' '#!/bin/sh /etc/rc.common' 'start_service() {' '    echo start' '}' \
        > "$repo/web/init/clean.init"
    # Not shell, so not counted.
    printf '%s\n' '#!/usr/bin/env python3' 'print(1)' > "$repo/tools/python-tool"
    # Excluded fixture with a warning: an unused variable.
    printf '%s\n' '#!/bin/sh' 'unused=1' > "$repo/tools/fixtures/legacy"
    git -C "$repo" add -A
    if ! check_tree "$repo" > "$fixture/out" 2>&1; then
        cat "$fixture/out" >&2
        echo "FAIL: selftest rejected clean scripts" >&2
        exit 1
    fi
    grep -Fq 'PASS: 6 tracked shell scripts' "$fixture/out" || {
        cat "$fixture/out" >&2
        echo "FAIL: selftest did not check exactly the 6 non-fixture shell scripts" >&2
        exit 1
    }

    # The root lookup must not print a CDPATH match into the root path.
    mkdir -p "$repo/tools"
    cp -- "$self" "$repo/tools/check-shell-scripts.sh"
    git -C "$repo" add -A
    if ! (cd "$repo" && CDPATH=. bash tools/check-shell-scripts.sh) > "$fixture/out" 2>&1 ||
        ! grep -Fq 'PASS: 7 tracked shell scripts' "$fixture/out"; then
        cat "$fixture/out" >&2
        echo "FAIL: selftest failed with CDPATH=." >&2
        exit 1
    fi
    git -C "$repo" rm -q --cached -- tools/check-shell-scripts.sh
    rm -f -- "$repo/tools/check-shell-scripts.sh"

    expect_failure() {
        local label=$1 path=$2 report=$3
        shift 3
        git -C "$repo" add -A
        if check_tree "$repo" > "$fixture/out" 2>&1; then
            cat "$fixture/out" >&2
            echo "FAIL: selftest accepted $label" >&2
            exit 1
        fi
        if ! grep -Fq -- "$report" "$fixture/out"; then
            cat "$fixture/out" >&2
            echo "FAIL: selftest did not report $report for $label" >&2
            exit 1
        fi
        git -C "$repo" rm -q --cached -- "$path"
        rm -f -- "${repo:?}/$path"
    }
    add_script() {
        local path=$1
        shift
        printf '%s\n' "$@" > "$repo/$path"
    }

    add_script tools/new.sh '#!/bin/sh' 'unused=1'
    expect_failure 'a warning in a new .sh script' tools/new.sh 'SC2034'
    add_script web/init/new.init '#!/bin/sh /etc/rc.common' 'unused=1'
    expect_failure 'a warning in an extensionless init script' web/init/new.init 'SC2034'
    add_script tools/new-tool '#!/bin/sh' 'if true; then'
    expect_failure 'a syntax error in an extensionless script' tools/new-tool 'sh -n rejected'
    add_script tools/env-tool '#!/usr/bin/env -S bash -e' 'unused=1'
    expect_failure 'a warning in an extensionless env -S script' tools/env-tool 'SC2034'
    add_script tools/dash-tool '#!/bin/dash' 'unused=1'
    expect_failure 'a warning in an extensionless dash script' tools/dash-tool 'SC2034'
    add_script tools/ksh-tool '#!/bin/ksh' 'echo ksh'
    expect_failure 'an unsupported shell' tools/ksh-tool 'does not support'
    # dash -n accepts [[ as a command name; shellcheck must reject it.
    add_script tools/bashism.sh '#!/bin/sh' 'if [[ -n "${1:-}" ]]; then echo set; fi'
    expect_failure 'a bash-only test in a /bin/sh script' tools/bashism.sh 'SC3010'
    add_script tools/python.sh '#!/usr/bin/env python3' 'print(1)'
    expect_failure 'a .sh file with a non-shell shebang' tools/python.sh 'does not support'
    newline_path=$'tools/bad\nname.sh'
    add_script "$newline_path" '#!/bin/sh' 'if true; then'
    expect_failure 'a syntax error in a script whose name has a newline' "$newline_path" 'sh -n rejected'
    add_script tools/helper 'echo helper'
    ln -s helper "$repo/tools/link.sh"
    expect_failure 'a tracked .sh symlink' tools/link.sh 'tracked symlink'
    echo "PASS: shell script check selftest"
)

case "${1:-}" in
    --selftest) run_selftest ;;
    --classify)
        [[ $# -eq 2 ]] || { echo "usage: $0 --classify PATH" >&2; exit 2; }
        script_shell "$2"
        ;;
    '') check_tree "$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)" ;;
    *)
        echo "usage: $0 [--selftest | --classify PATH]" >&2
        exit 2
        ;;
esac
