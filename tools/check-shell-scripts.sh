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

# Split $1 into SHEBANG_WORDS with GNU env -S quoting: single and double
# quotes group words, and # at a word start ends the line. GNU env -S
# escapes such as \_ and \c change word boundaries, so splitting stops at the
# first word with a backslash and sets SHEBANG_ESCAPED=1 instead of
# emulating them.
split_shebang() {
    local text=$1 word='' quote='' have=0 c i
    SHEBANG_WORDS=()
    SHEBANG_ESCAPED=0
    for ((i = 0; i < ${#text}; i++)); do
        c=${text:i:1}
        if [[ $c == "\\" ]]; then
            SHEBANG_ESCAPED=1
            return 0
        elif [[ $quote == "'" ]]; then
            if [[ $c == "'" ]]; then quote=''; else word+=$c; fi
        elif [[ $quote == '"' ]]; then
            if [[ $c == '"' ]]; then quote=''; else word+=$c; fi
        elif [[ $c == "'" || $c == '"' ]]; then
            quote=$c
            have=1
        elif [[ $c == [[:space:]] ]]; then
            if [[ $have == 1 ]]; then SHEBANG_WORDS+=("$word"); fi
            word='' have=0
        elif [[ $c == '#' && $have == 0 ]]; then
            break
        else
            word+=$c
            have=1
        fi
    done
    if [[ $have == 1 ]]; then SHEBANG_WORDS+=("$word"); fi
}

# Print the interpreter's basename from the shebang in $1, followed by its
# applet for BusyBox. Print nothing when there is no shebang, unknown when no
# interpreter remains, and env-escape when a backslash comes before or inside
# the env interpreter word. Escapes in later words are the interpreter's own
# arguments and do not matter. env options follow GNU env.
shebang_interpreter() {
    local first
    first=$(head -c 256 -- "$1" | head -n 1 | tr -d '\000\r')
    [[ "$first" == '#!'* ]] || return 0
    split_shebang "${first#\#!}"
    local -a words=("${SHEBANG_WORDS[@]}")
    local i=1 word flag rest
    if [[ ${#words[@]} -eq 0 ]]; then
        echo unknown
        return 0
    fi
    if [[ "${words[0]##*/}" == env ]]; then
        # Linux passes the rest of the line as one argument; the split above
        # already applied -S quoting to it.
        while [[ $i -lt ${#words[@]} ]]; do
            word=${words[$i]}
            case "$word" in
                --)
                    # env still reads NAME=value operands after --.
                    i=$((i + 1))
                    while [[ $i -lt ${#words[@]} && "${words[$i]}" == *=* ]]; do
                        i=$((i + 1))
                    done
                    break
                    ;;
                --split-string=*) words[i]=${word#--split-string=}; continue ;;
                --unset|--chdir) i=$((i + 2)); continue ;;
                --*|-|*=*) i=$((i + 1)); continue ;;
                -?*) ;;
                *) break ;;
            esac
            # Short options, possibly combined, as getopt reads them.
            rest=${word#-}
            i=$((i + 1))
            while [[ -n $rest ]]; do
                flag=${rest:0:1}
                rest=${rest:1}
                case "$flag" in
                    S)
                        if [[ -n $rest ]]; then
                            i=$((i - 1))
                            words[i]=$rest
                        fi
                        rest=''
                        ;;
                    u|C)
                        if [[ -z $rest ]]; then
                            i=$((i + 1))
                        fi
                        rest=''
                        ;;
                esac
            done
        done
    else
        i=0
    fi
    # BusyBox needs its applet word too to determine the shell.
    local needed=$i
    if [[ $i -lt ${#words[@]} ]]; then
        word=${words[$i]%%[[:space:]]*}
        if [[ "${word##*/}" == busybox ]]; then
            needed=$((i + 1))
        fi
    fi
    if [[ $needed -ge ${#words[@]} && $SHEBANG_ESCAPED == 1 ]] || [[ $i -ge ${#words[@]} ]]; then
        # No tracked script needs escapes, so fail closed rather than guess.
        if [[ $SHEBANG_ESCAPED == 1 && "${words[0]##*/}" == env ]]; then
            echo env-escape
        else
            echo unknown
        fi
        return 0
    fi
    # A quoted 'bash -e' stays one word; classify by its first field so an
    # ambiguous script is checked rather than skipped.
    word=${words[$i]%%[[:space:]]*}
    word=${word##*/}
    if [[ "$word" == busybox ]]; then
        rest=${words[$((i + 1))]:-}
        printf 'busybox %s\n' "${rest%%[[:space:]]*}"
    else
        printf '%s\n' "$word"
    fi
}

# Print the dialect for $1: sh, bash, unsupported, or nothing.
script_shell() {
    local interpreter
    interpreter=$(shebang_interpreter "$1")
    case "$interpreter" in
        bash) echo bash ;;
        sh|dash|ash|'busybox sh'|'busybox ash') echo sh ;;
        ksh|mksh|pdksh|zsh|yash|posh|csh|tcsh|fish|unknown|env-escape)
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
                if [[ "$(shebang_interpreter "$path")" == env-escape ]]; then
                    echo "FAIL: $path uses a backslash escape in an env shebang; use a plain form such as #!/usr/bin/env -S bash -e" >&2
                else
                    echo "FAIL: $path has a shebang this check does not support: $(shebang_interpreter "$path")" >&2
                fi
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
    # GNU env long split-string and a quoted -S word, both runnable Bash.
    printf '%s\n' '#!/usr/bin/env --split-string=/bin/bash -e' 'echo long' \
        > "$repo/tools/split-long.sh"
    printf '%s\n' '#!/usr/bin/env -S "bash" -e' 'echo quoted' > "$repo/tools/split-quoted"
    printf '%s\n' '#!/bin/busybox sh' 'echo busybox' > "$repo/tools/busybox-tool"
    printf '%s\n' 'PIN=1' 'export PIN' > "$repo/tools/sourced.sh"
    printf '%s\n' '#!/bin/sh /etc/rc.common' 'start_service() {' '    echo start' '}' \
        > "$repo/web/init/clean.init"
    # Not shell, so not counted, even with an escape in the interpreter's
    # own arguments.
    printf '%s\n' '#!/usr/bin/env python3' 'print(1)' > "$repo/tools/python-tool"
    printf '%s\n' "#!/usr/bin/env -S awk -v 'FS=\\t' -f" '{ print $1 }' > "$repo/tools/awk-tool"
    # Excluded fixture with a warning: an unused variable.
    printf '%s\n' '#!/bin/sh' 'unused=1' > "$repo/tools/fixtures/legacy"
    git -C "$repo" add -A
    if ! check_tree "$repo" > "$fixture/out" 2>&1; then
        cat "$fixture/out" >&2
        echo "FAIL: selftest rejected clean scripts" >&2
        exit 1
    fi
    grep -Fq 'PASS: 8 tracked shell scripts' "$fixture/out" || {
        cat "$fixture/out" >&2
        echo "FAIL: selftest did not check exactly the 8 non-fixture shell scripts" >&2
        exit 1
    }

    # The root lookup must not print a CDPATH match into the root path.
    mkdir -p "$repo/tools"
    cp -- "$self" "$repo/tools/check-shell-scripts.sh"
    git -C "$repo" add -A
    if ! (cd "$repo" && CDPATH=. bash tools/check-shell-scripts.sh) > "$fixture/out" 2>&1 ||
        ! grep -Fq 'PASS: 9 tracked shell scripts' "$fixture/out"; then
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
    # Each GNU env form must reach the shell so the warning is reported.
    for shebang in \
        '#!/usr/bin/env -S bash -e' \
        '#!/usr/bin/env -S "bash" -e' \
        '#!/usr/bin/env -Sbash' \
        '#!/usr/bin/env --split-string=/bin/bash -e' \
        "#!/usr/bin/env --split-string='bash -e'" \
        '#!/usr/bin/env -u FOO bash' \
        '#!/usr/bin/env --unset=FOO sh' \
        '#!/usr/bin/env -i bash' \
        '#!/usr/bin/env -S -- DENEB_MODE=1 bash -e'
    do
        add_script tools/env-tool "$shebang" 'unused=1'
        expect_failure "a warning in an extensionless $shebang script" tools/env-tool 'SC2034'
    done
    add_script tools/dash-tool '#!/bin/dash' 'unused=1'
    expect_failure 'a warning in an extensionless dash script' tools/dash-tool 'SC2034'
    # env -S escapes fail closed instead of being emulated.
    add_script tools/escape-tool '#!/usr/bin/env -S\_bash -e' 'echo escape'
    expect_failure 'an env -S \_ escape' tools/escape-tool 'backslash escape in an env shebang'
    add_script tools/escape.sh '#!/usr/bin/env -S bash\_-e' 'echo escape'
    expect_failure 'an escape inside the env interpreter word' tools/escape.sh 'backslash escape in an env shebang'
    # The BusyBox applet word also decides the shell.
    add_script tools/escape-tool '#!/usr/bin/env -S busybox sh\_-e' 'echo escape'
    expect_failure 'an escape inside the BusyBox applet word' tools/escape-tool 'backslash escape in an env shebang'
    add_script tools/escape-tool '#!/usr/bin/env -S busybox\_sh' 'echo escape'
    expect_failure 'an escape between BusyBox and its applet' tools/escape-tool 'backslash escape in an env shebang'
    add_script tools/busybox-env-tool '#!/usr/bin/env -S busybox sh -e' 'unused=1'
    expect_failure 'a warning in an env BusyBox sh script' tools/busybox-env-tool 'SC2034'
    # Escapes after the interpreter word are its own arguments.
    add_script tools/escape-tool "#!/usr/bin/env -S 'bash' \\c junk" 'unused=1'
    expect_failure 'a warning in a bash script with a later \c escape' tools/escape-tool 'SC2034'
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
