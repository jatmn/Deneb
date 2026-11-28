#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Install Ubuntu packages on a GitHub-hosted CI runner.
#
# Runner images carry third-party apt sources (for example
# packages.microsoft.com) that Deneb's CI never installs from. When one of
# them is unavailable, a plain `apt-get update` exits 100 and fails the job.
# Refresh and install from the Ubuntu archive sources only, and keep both
# steps strict: an Ubuntu archive failure still fails CI.
#
# usage: tools/ci-apt-install.sh PACKAGE...
#        tools/ci-apt-install.sh --selftest
#
# CI_APT_ROOT (default /etc/apt) and CI_APT_SUDO (default sudo) exist for the
# self-test.

set -eu

apt_root=${CI_APT_ROOT:-/etc/apt}

# Succeed for http(s) URIs on ubuntu.com hosts, and for the runner's mirror
# list when every mirror it names is itself an Ubuntu archive URI.
ubuntu_uri() {
    case "$1" in
        mirror+file:/*)
            mirrors=${1#mirror+file:}
            [ -f "$mirrors" ] || return 1
            listed=$(sed -e 's/[[:space:]].*//' -e '/^#/d' -e '/^$/d' "$mirrors")
            [ -n "$listed" ] || return 1
            for mirror in $listed; do
                ubuntu_uri "$mirror" || return 1
            done
            return 0
            ;;
        http://*|https://*) ;;
        *) return 1 ;;
    esac
    host=${1#*://}
    host=${host%%/*}
    host=${host%%:*}
    case "$host" in
        ubuntu.com|*.ubuntu.com) return 0 ;;
    esac
    return 1
}

# Succeed when sources file $1 names at least one URI and every URI is an
# Ubuntu archive URI. Rather than parse each APT field spelling, collect every
# URI-shaped string (scheme followed by ":/") outside comments. A URI anywhere
# else, in any field, can only make the file be skipped. Comments follow APT:
# a deb822 .sources file has only whole-line comments, "#" in column 0, while
# a one-line file ends each line at the first "#" outside [options].
ubuntu_only_source() {
    [ -f "$1" ] || return 1
    case "$1" in
        *.sources) deb822=1 ;;
        *) deb822=0 ;;
    esac
    uris=$(awk -v deb822="$deb822" '
        # Drop a one-line comment the way APT does: skip each [...] whole, and
        # keep the line unchanged when a "[" is never closed.
        function strip_comment(s,   out, c, end) {
            out = ""
            while (s != "") {
                c = substr(s, 1, 1)
                if (c == "#") return out
                if (c == "[") {
                    end = index(substr(s, 2), "]")
                    if (end == 0) return out s
                    out = out substr(s, 1, end + 1)
                    s = substr(s, end + 2)
                    continue
                }
                out = out c
                s = substr(s, 2)
            }
            return out
        }
        deb822 && /^#/ { next }
        {
            line = deb822 ? $0 : strip_comment($0)
            while (match(line, /[A-Za-z][A-Za-z0-9+.-]*:\/[^] \t]*/)) {
                print substr(line, RSTART, RLENGTH)
                line = substr(line, RSTART + RLENGTH)
            }
        }
    ' "$1")
    [ -n "$uris" ] || return 1
    for uri in $uris; do
        ubuntu_uri "$uri" || return 1
    done
}

apt_run() {
    if [ -n "${CI_APT_SUDO-sudo}" ]; then
        "${CI_APT_SUDO-sudo}" apt-get "$@"
    else
        apt-get "$@"
    fi
}

# apt-get limited to the selected Ubuntu sources, with download retries.
apt_ubuntu() {
    apt_run -o Acquire::Retries=3 -o "Dir::Etc::SourceList=$source_list" \
        -o "Dir::Etc::SourceParts=$source_parts" "$@"
}

install_packages() {
    source_parts=$(mktemp -d)
    trap 'rm -rf "$source_parts"' EXIT
    chmod 755 "$source_parts"
    for file in "$apt_root"/sources.list.d/*.list "$apt_root"/sources.list.d/*.sources; do
        [ -f "$file" ] || continue
        if ubuntu_only_source "$file"; then
            cp -- "$file" "$source_parts/"
            echo "Using apt source $file"
        else
            echo "Skipping apt source $file (not Ubuntu-only)"
        fi
    done
    source_list=/dev/null
    if ubuntu_only_source "$apt_root/sources.list"; then
        source_list=$apt_root/sources.list
        echo "Using apt source $source_list"
    fi
    if [ "$source_list" = /dev/null ] && [ -z "$(ls -A "$source_parts")" ]; then
        echo "::error::No Ubuntu-only apt sources found under $apt_root"
        exit 1
    fi
    # Without --error-on=any, apt-get update can warn about an index it failed
    # to download and still exit 0.
    apt_ubuntu update --error-on=any
    apt_ubuntu install --no-install-recommends "$@"
}

selftest() {
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    mkdir -p "$work/bin" "$work/etc/sources.list.d"
    root=$work/etc
    # Stub apt-get: record each call and the sources it would read, and fail
    # update or install on request.
    cat > "$work/bin/apt-get" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >> "$APT_LOG"
parts=''
for arg do
    case "$arg" in Dir::Etc::SourceParts=*) parts=${arg#*=} ;; esac
done
[ -z "$parts" ] || ls "$parts" >> "$APT_LOG"
case " $* " in
    *" update "*) [ -z "${FAIL_UPDATE:-}" ] || exit 100 ;;
    *" install "*) [ -z "${FAIL_INSTALL:-}" ] || exit 100 ;;
esac
exit 0
STUB
    chmod +x "$work/bin/apt-get"
    export APT_LOG="$work/log"

    # The runner layout: deb822 Ubuntu sources through a mirror list, plus a
    # third-party one-line source and an empty sources.list.
    printf '%s\n' 'http://azure.archive.ubuntu.com/ubuntu/	priority:1' \
        'http://archive.ubuntu.com/ubuntu/	priority:2' > "$root/apt-mirrors.txt"
    printf '%s\n' 'Types: deb' "URIs: mirror+file:$root/apt-mirrors.txt" \
        'Suites: noble noble-updates' 'Components: main universe' '' \
        'Types: deb' 'URIs: http://security.ubuntu.com/ubuntu' 'Suites: noble-security' \
        'Components: main universe' > "$root/sources.list.d/ubuntu.sources"
    printf '%s\n' 'deb [arch=amd64 signed-by=/k.gpg] https://packages.microsoft.com/ubuntu/24.04/prod noble main' \
        > "$root/sources.list.d/microsoft-prod.list"
    printf '%s\n' '# Ubuntu sources moved to ubuntu.sources' > "$root/sources.list"

    run() {
        : > "$APT_LOG"
        PATH="$work/bin:$PATH" CI_APT_ROOT="$root" CI_APT_SUDO='' "$@" > "$work/out" 2>&1
    }
    fail() {
        cat "$work/out" "$APT_LOG" >&2
        echo "FAIL: $1" >&2
        exit 1
    }

    run env sh "$0" lighttpd unzip || fail "a clean update and install failed"
    opts="-o Acquire::Retries=3 -o Dir::Etc::SourceList=/dev/null -o Dir::Etc::SourceParts="
    grep -q "^${opts}[^ ]* update --error-on=any\$" "$APT_LOG" ||
        fail "update did not use retries, strict errors, and the Ubuntu sources"
    grep -q "^${opts}[^ ]* install --no-install-recommends lighttpd unzip\$" "$APT_LOG" ||
        fail "install did not use retries, the Ubuntu sources, and the packages"
    [ "$(grep -cx 'ubuntu.sources' "$APT_LOG")" = 2 ] || fail "ubuntu.sources was not used for both calls"
    ! grep -q 'microsoft' "$APT_LOG" || fail "a third-party source reached apt"
    echo "PASS: update and install use only Ubuntu sources, with retries and a strict update"

    if run env FAIL_UPDATE=1 sh "$0" lighttpd; then
        fail "a failed Ubuntu update was accepted"
    fi
    ! grep -q ' install ' "$APT_LOG" || fail "install ran after a failed update"
    echo "PASS: a failed Ubuntu update fails before installing"

    if run env FAIL_INSTALL=1 sh "$0" lighttpd; then
        fail "a failed install was accepted"
    fi
    echo "PASS: a failed install fails"

    # A one-line sources.list mixing Ubuntu and another host is not used.
    printf '%s\n' 'deb http://archive.ubuntu.com/ubuntu noble main' \
        'deb http://evil.example/ubuntu.com/ubuntu noble main' > "$root/sources.list"
    run env sh "$0" lighttpd || fail "the mixed sources.list case failed"
    grep -q 'Dir::Etc::SourceList=/dev/null' "$APT_LOG" || fail "a mixed sources.list was used"
    echo "PASS: a source with any non-Ubuntu URI is skipped"

    # APT folds indented lines into the URIs field, past comment lines.
    printf '%s\n' 'Types: deb' 'URIs: http://archive.ubuntu.com/ubuntu' '# folded below' \
        ' https://packages.microsoft.com/ubuntu/24.04/prod' 'Suites: noble' 'Components: main' \
        > "$root/sources.list.d/folded.sources"
    printf '%s\n' 'Types: deb' 'uris: http://archive.ubuntu.com/ubuntu' \
        '	http://security.ubuntu.com/ubuntu' 'Suites: noble' 'Components: main' \
        > "$root/sources.list.d/folded-ubuntu.sources"
    run env sh "$0" lighttpd || fail "the folded URIs case failed"
    ! grep -qx 'folded.sources' "$APT_LOG" || fail "a folded third-party URI reached apt"
    grep -qx 'folded-ubuntu.sources' "$APT_LOG" || fail "a folded all-Ubuntu URIs field was skipped"
    rm -f "$root/sources.list.d/folded.sources" "$root/sources.list.d/folded-ubuntu.sources"
    echo "PASS: folded deb822 URIs are checked line by line"

    # APT also accepts a field with no space after the colon.
    printf '%s\n' 'Types: deb' 'URIs: http://archive.ubuntu.com/ubuntu' 'Suites: noble' \
        'Components: main' '' 'Types: deb' 'URIs:https://packages.microsoft.com/ubuntu/24.04/prod' \
        'Suites: noble' 'Components: main' > "$root/sources.list.d/nospace.sources"
    run env sh "$0" lighttpd || fail "the unspaced URIs case failed"
    ! grep -qx 'nospace.sources' "$APT_LOG" || fail "an unspaced third-party URIs field reached apt"
    rm -f "$root/sources.list.d/nospace.sources"
    echo "PASS: any URI in the file must be Ubuntu, however the field is written"

    # A one-line source ends at "#", except inside [options]. In deb822 only a
    # column-0 "#" starts a comment.
    printf '%s\n' '# https://packages.microsoft.com/ubuntu/24.04/prod' \
        'deb http://archive.ubuntu.com/ubuntu noble main # See https://launchpad.net/ubuntu' \
        > "$root/sources.list.d/commented.list"
    printf '%s\n' 'deb http://archive.ubuntu.com/ubuntu noble main' \
        'deb [signed-by=/k#x.gpg] https://packages.microsoft.com/ubuntu/24.04/prod noble main' \
        > "$root/sources.list.d/bracket.list"
    printf '%s\n' 'Types: deb' 'URIs: http://archive.ubuntu.com/ubuntu # https://packages.microsoft.com/ubuntu/24.04/prod' \
        'Suites: noble' 'Components: main' > "$root/sources.list.d/hash.sources"
    run env sh "$0" lighttpd || fail "the comment cases failed"
    grep -qx 'commented.list' "$APT_LOG" || fail "a one-line source with a commented-out URI was skipped"
    ! grep -qx 'bracket.list' "$APT_LOG" || fail "a third-party URI after a \"#\" in [options] reached apt"
    ! grep -qx 'hash.sources' "$APT_LOG" || fail "a third-party URI after a \"#\" in a deb822 field reached apt"
    rm -f "$root/sources.list.d/commented.list" "$root/sources.list.d/bracket.list" \
        "$root/sources.list.d/hash.sources"
    echo "PASS: comments are skipped the way APT reads each format"

    rm -f "$root/sources.list.d/ubuntu.sources"
    if run env sh "$0" lighttpd; then
        fail "no Ubuntu sources was accepted"
    fi
    grep -q 'No Ubuntu-only apt sources' "$work/out" || fail "missing sources were not reported"
    echo "PASS: no Ubuntu sources is an error"

    if run env sh "$0"; then
        fail "no packages was accepted"
    fi
    echo "PASS: no packages is a usage error"
    echo "CI apt install self-test: PASS"
}

case "${1:-}" in
    --selftest) selftest ;;
    '') echo "usage: $0 PACKAGE... | --selftest" >&2; exit 2 ;;
    *) install_packages "$@" ;;
esac
