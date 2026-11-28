#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Install Ubuntu packages on a GitHub-hosted CI runner.
#
# Runner images carry third-party apt sources (for example
# packages.microsoft.com) that Deneb's CI does not install from. When one of
# them is unavailable, `apt-get update` exits 100 even though the Ubuntu
# indexes refreshed. Treat a failed update as a warning and let the install
# decide: it still fails when a requested package cannot be resolved or
# downloaded.
#
# usage: tools/ci-apt-install.sh PACKAGE...
#        tools/ci-apt-install.sh --selftest

set -eu

# CI_APT_SUDO selects the privilege wrapper; the self-test sets it empty.
apt_run() {
    if [ -n "${CI_APT_SUDO-sudo}" ]; then
        "${CI_APT_SUDO-sudo}" apt-get -o Acquire::Retries=3 "$@"
    else
        apt-get -o Acquire::Retries=3 "$@"
    fi
}

install_packages() {
    if ! apt_run update; then
        echo "::warning::apt-get update failed; installing from the package indexes that refreshed"
    fi
    apt_run install --no-install-recommends "$@"
}

selftest() {
    work=$(mktemp -d)
    trap 'rm -rf "$work"' EXIT
    mkdir "$work/bin"
    # Stub apt-get: fail update when FAIL_UPDATE is set, fail install when
    # FAIL_INSTALL is set, and log every call.
    cat > "$work/bin/apt-get" <<'STUB'
#!/bin/sh
printf '%s\n' "$*" >> "$APT_LOG"
case " $* " in
    *" update "*) [ -z "${FAIL_UPDATE:-}" ] || exit 100 ;;
    *" install "*) [ -z "${FAIL_INSTALL:-}" ] || exit 100 ;;
esac
exit 0
STUB
    chmod +x "$work/bin/apt-get"
    export APT_LOG="$work/log"

    run() {
        : > "$APT_LOG"
        PATH="$work/bin:$PATH" CI_APT_SUDO='' "$@" > "$work/out" 2>&1
    }

    run env sh "$0" lighttpd unzip ||
        { cat "$work/out" >&2; echo "FAIL: a clean update and install failed" >&2; exit 1; }
    grep -Fqx -- '-o Acquire::Retries=3 install --no-install-recommends lighttpd unzip' "$APT_LOG" ||
        { cat "$APT_LOG" >&2; echo "FAIL: install did not receive the packages" >&2; exit 1; }
    echo "PASS: update and install run with retries"

    run env FAIL_UPDATE=1 sh "$0" lighttpd ||
        { cat "$work/out" >&2; echo "FAIL: a failed update stopped the install" >&2; exit 1; }
    grep -Fq '::warning::apt-get update failed' "$work/out" ||
        { echo "FAIL: a failed update was not reported" >&2; exit 1; }
    grep -Fq 'install --no-install-recommends lighttpd' "$APT_LOG" ||
        { echo "FAIL: install did not run after a failed update" >&2; exit 1; }
    echo "PASS: a failed update warns and still installs"

    if run env FAIL_UPDATE=1 FAIL_INSTALL=1 sh "$0" lighttpd; then
        echo "FAIL: a failed install was accepted" >&2
        exit 1
    fi
    echo "PASS: a failed install still fails"

    if run env sh "$0"; then
        echo "FAIL: no packages was accepted" >&2
        exit 1
    fi
    echo "PASS: no packages is a usage error"
    echo "CI apt install self-test: PASS"
}

case "${1:-}" in
    --selftest) selftest ;;
    '') echo "usage: $0 PACKAGE... | --selftest" >&2; exit 2 ;;
    *) install_packages "$@" ;;
esac
