#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Static de-Python audit for the native print-service migration.

set -eu

SCRIPT_NAME=$(basename "$0")

usage() {
    cat >&2 <<EOF
Usage:
  ${SCRIPT_NAME} --source <repo-root>
  ${SCRIPT_NAME} --package-dir <unpacked-update-dir>
  ${SCRIPT_NAME} --archive <Deneb_Update_*.deneb>
EOF
    exit 2
}

pass() {
    printf 'PASS: %s\n' "$1"
}

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

require_file() {
    if [ ! -f "$1" ]; then
        fail "$2 missing: $1"
    fi
    pass "$2"
}

require_dir() {
    if [ ! -d "$1" ]; then
        fail "$2 missing: $1"
    fi
    pass "$2"
}

require_pattern() {
    file=$1
    pattern=$2
    label=$3
    if ! grep -Eq "$pattern" "$file"; then
        fail "$label"
    fi
    pass "$label"
}

reject_pattern() {
    file=$1
    pattern=$2
    label=$3
    if grep -Eq "$pattern" "$file"; then
        fail "$label"
    fi
    pass "$label"
}

reject_name_artifacts() {
    root=$1
    label=$2
    if find "$root" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
        -print | grep . >/dev/null 2>&1; then
        find "$root" \( -name '*.py' -o -name '*python*' -o -name 'print_service.py' \) \
            -print >&2
        fail "$label"
    fi
    pass "$label"
}

audit_source() {
    repo=$1

    require_file "${repo}/common/print/print_backend_route.c" "native route source exists"
    require_file "${repo}/common/print/print_backend_route.h" "native route header exists"
    require_file "${repo}/ui/build-package.sh" "package builder exists"
    require_file "${repo}/ui/installer/update.sh" "installer exists"
    require_file "${repo}/printsvc/init/deneb-printsvc.init" "native printsvc init exists"

    require_pattern "${repo}/common/print/print_backend_route.c" \
        'deneb_print_backend_route\(DENEB_PRINT_BACKEND_NATIVE\)' \
        "shared route defaults to native printsvc"
    require_pattern "${repo}/common/print/print_backend_route.h" \
        '#define DENEB_PRINTSVC_STATUS_URL "tcp://127\.0\.0\.1:5555"' \
        "shared route exposes native status endpoint"
    require_pattern "${repo}/common/print/print_backend_route.h" \
        '#define DENEB_PRINTSVC_COMMAND_URL "tcp://127\.0\.0\.1:5556"' \
        "shared route exposes native command endpoint"

    if grep -R "DENEB_PRINT_BACKEND_STOCK\|STOCK_PRINT_BACKEND\|print_service_route" \
        "${repo}/common/print" "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" \
        >/dev/null 2>&1; then
        grep -R "DENEB_PRINT_BACKEND_STOCK\|STOCK_PRINT_BACKEND\|print_service_route" \
            "${repo}/common/print" "${repo}/ui/src" "${repo}/web/src" "${repo}/printsvc/src" >&2 || true
        fail "no stock print backend selector remains in native clients"
    fi
    pass "no stock print backend selector remains in native clients"

    if grep -R '/usr/bin/python3[[:space:]]\+/home/cygnus/marlindriver/print_service\.py' \
        "${repo}/ui/src" "${repo}/ui/installer" "${repo}/ui/init" \
        "${repo}/web/src" "${repo}/web/init" \
        "${repo}/printsvc/src" "${repo}/printsvc/init" "${repo}/common/print" \
        >/dev/null 2>&1; then
        grep -R '/usr/bin/python3[[:space:]]\+/home/cygnus/marlindriver/print_service\.py' \
            "${repo}/ui/src" "${repo}/ui/installer" "${repo}/ui/init" \
            "${repo}/web/src" "${repo}/web/init" \
            "${repo}/printsvc/src" "${repo}/printsvc/init" "${repo}/common/print" >&2 || true
        fail "no Deneb runtime launches stock Python print_service.py"
    fi
    pass "no Deneb runtime launches stock Python print_service.py"

    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-native-audit" \
        "package builder runs de-Python audit"
    require_pattern "${repo}/ui/build-package.sh" \
        "deneb-printsvc-native-audit-selftest" \
        "package builder runs de-Python audit selftest"
    require_pattern "${repo}/ui/build-package.sh" \
        "find \"\\\$STAGING_DIR\" .*print_service\\.py" \
        "package builder rejects Python driver artifacts"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-native-audit" \
        "installer runs de-Python audit"
    require_pattern "${repo}/ui/installer/update.sh" \
        "deneb-printsvc-native-audit-selftest" \
        "installer runs de-Python audit selftest"
    require_pattern "${repo}/ui/installer/update.sh" \
        "native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction" \
        "installer checks native printsvc release gate"
}

audit_package_dir() {
    root=$1

    require_file "${root}/manifest.txt" "package manifest exists"
    require_file "${root}/deneb-printsvc" "package includes native printsvc"
    require_file "${root}/deneb-printsvc-smoke" "package includes live smoke harness"
    require_file "${root}/deneb-printsvc-smoke-verify" "package includes smoke verifier"
    require_file "${root}/deneb-printsvc-smoke-compare" "package includes smoke comparator"
    require_file "${root}/deneb-printsvc-native-audit" "package includes de-Python audit"
    require_file "${root}/deneb-printsvc-native-audit-selftest" "package includes de-Python audit selftest"
    require_dir "${root}/deneb-printsvc-macros" "package includes Deneb macro directory"
    reject_name_artifacts "$root" "package has no Python driver artifact names"

    require_pattern "${root}/manifest.txt" '^native_printsvc: experimental$' \
        "package manifest marks native printsvc experimental"
    require_pattern "${root}/manifest.txt" \
        '^native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction$' \
        "package manifest carries non-experimental evidence gate"
    require_pattern "${root}/manifest.txt" \
        'no Python driver files are packaged; native deneb-printsvc owns marlindriver' \
        "package manifest records no-Python driver ownership"
}

audit_archive() {
    archive=$1
    tmp_dir="${TMPDIR:-/tmp}/deneb-native-audit.$$"
    trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
    mkdir -p "$tmp_dir"
    tar -tf "$archive" > "${tmp_dir}/files.txt"

    if grep -Ei '(^|/).*\.py$|(^|/).*python.*|(^|/)print_service\.py$' "${tmp_dir}/files.txt"; then
        fail "archive has no Python driver artifact names"
    fi
    pass "archive has no Python driver artifact names"

    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc$' \
        "archive includes native printsvc"
    require_pattern "${tmp_dir}/files.txt" '(^|/)deneb-printsvc-native-audit$' \
        "archive includes de-Python audit"
    require_pattern "${tmp_dir}/files.txt" '(^|/)manifest.txt$' \
        "archive includes manifest"
    tar xf "$archive" -C "$tmp_dir"
    audit_package_dir "$tmp_dir"
}

if [ "$#" -ne 2 ]; then
    usage
fi

case "$1" in
    --source)
        audit_source "$2"
        ;;
    --package-dir)
        audit_package_dir "$2"
        ;;
    --archive)
        audit_archive "$2"
        ;;
    *)
        usage
        ;;
esac

printf 'deneb-printsvc de-Python audit passed\n'
