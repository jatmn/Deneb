#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Shell-only static checks for native print-service init handoff scripts.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="${DENEB_REPO_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
INSTALLED_INIT_DIR="${DENEB_INSTALLED_INIT_DIR:-/etc/init.d}"
DEFAULT_PRINTSVC_INIT="${REPO_ROOT}/printsvc/init/deneb-printsvc.init"
DEFAULT_INSTALLER="${REPO_ROOT}/ui/installer/update.sh"
INSTALLED_PRINTSVC_INIT="${INSTALLED_INIT_DIR}/deneb-printsvc"
INSTALLED_PRINTSERVER_INIT="${INSTALLED_INIT_DIR}/printserver"

if [ -n "${DENEB_PRINTSVC_INIT:-}" ]; then
    PRINTSVC_INIT="$DENEB_PRINTSVC_INIT"
elif [ -f "$DEFAULT_PRINTSVC_INIT" ]; then
    PRINTSVC_INIT="$DEFAULT_PRINTSVC_INIT"
else
    PRINTSVC_INIT="$INSTALLED_PRINTSVC_INIT"
fi

if [ -n "${DENEB_INSTALLER:-}" ]; then
    INSTALLER="$DENEB_INSTALLER"
    INSTALLER_REQUIRED=1
elif [ -f "$DEFAULT_INSTALLER" ]; then
    INSTALLER="$DEFAULT_INSTALLER"
    INSTALLER_REQUIRED=1
else
    INSTALLER=""
    INSTALLER_REQUIRED=0
fi

if [ -n "${DENEB_PRINTSERVER_INIT:-}" ]; then
    PRINTSERVER_INIT="$DENEB_PRINTSERVER_INIT"
elif [ -f "$INSTALLED_PRINTSERVER_INIT" ]; then
    PRINTSERVER_INIT="$INSTALLED_PRINTSERVER_INIT"
else
    PRINTSERVER_INIT=""
fi
TMP_DIR="${TMPDIR:-/tmp}/deneb-printsvc-init-selftest.$$"

failures=0

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

cleanup() {
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$TMP_DIR"

pass() {
    echo "PASS: $*"
}

require_file() {
    file="$1"
    label="$2"
    if [ -f "$file" ]; then
        pass "$label"
    else
        fail "$label"
    fi
}

require_pattern() {
    file="$1"
    pattern="$2"
    label="$3"

    if grep -Eq "$pattern" "$file"; then
        pass "$label"
    else
        fail "$label"
    fi
}

reject_pattern() {
    file="$1"
    pattern="$2"
    label="$3"

    if grep -Eq "$pattern" "$file"; then
        fail "$label"
    else
        pass "$label"
    fi
}

require_order() {
    file="$1"
    first="$2"
    second="$3"
    label="$4"

    if awk -v first="$first" -v second="$second" '
        $0 ~ first && first_line == 0 { first_line = NR }
        $0 ~ second && first_line > 0 && NR > first_line { found = 1; exit }
        END { exit found ? 0 : 1 }
    ' "$file"; then
        pass "$label"
    else
        fail "$label"
    fi
}

require_pattern_count() {
    file="$1"
    pattern="$2"
    min_count="$3"
    label="$4"
    count="$(grep -Ec "$pattern" "$file" 2>/dev/null || true)"

    if [ "$count" -ge "$min_count" ]; then
        pass "$label"
    else
        fail "$label"
    fi
}

extract_printserver_shim() {
    installer="$1"
    out="$2"

    awk '
        /cat > "\$\{printserver_init\}" <<'\''EOF'\''/ {
            in_shim = 1;
            next;
        }
        in_shim && /^EOF$/ {
            found = 1;
            exit;
        }
        in_shim {
            print;
        }
        END {
            exit found ? 0 : 1;
        }
    ' "$installer" > "$out"
}

check_printserver_shim() {
    file="$1"
    prefix="$2"

    require_pattern "$file" 'DENEB_PRINTSERVER_NATIVE_GATE' \
        "$prefix has native gate marker"
    require_pattern "$file" 'stop_stock_print_service\(\)' \
        "$prefix has cleanup helper"
    require_pattern "$file" '/home/cygnus/marlindriver/print_service\.py' \
        "$prefix targets exact stock print_service.py path"
    require_pattern "$file" 'rm -f /var/run/printserver\.pid' \
        "$prefix clears stale printserver pid"
    require_pattern_count "$file" 'stop_stock_print_service' 3 \
        "$prefix has cleanup helper plus start/stop calls"
    require_order "$file" 'logger -t deneb-printserver "native deneb-printsvc owns the print backend"' '^[[:space:]]*stop_stock_print_service$' \
        "$prefix cleans stock service during start"
    require_order "$file" 'rm -f /var/run/printserver\.pid' '^[[:space:]]*stop_stock_print_service$' \
        "$prefix cleans stock service during stop"
    reject_pattern "$file" '/usr/bin/python3|python3 ' \
        "$prefix does not launch Python"
}

require_file "$PRINTSVC_INIT" "native printsvc init exists"
if [ "$INSTALLER_REQUIRED" -eq 1 ]; then
    require_file "$INSTALLER" "installer script exists"
else
    pass "installer script skipped for installed runtime selftest"
fi

if [ "$failures" -eq 0 ]; then
    require_pattern "$PRINTSVC_INIT" 'stop_stock_print_service\(\)' \
        "native init has explicit stock service cleanup helper"
    require_pattern "$PRINTSVC_INIT" '/etc/init\.d/printserver stop' \
        "native init stops stock printserver"
    require_pattern "$PRINTSVC_INIT" '/home/cygnus/marlindriver/print_service\.py' \
        "native init targets exact stock print_service.py path"
    require_pattern "$PRINTSVC_INIT" 'rm -f /var/run/printserver\.pid' \
        "native init clears stale printserver pid"
    require_pattern "$PRINTSVC_INIT" '^[[:space:]]*stop_stock_print_service$' \
        "native init invokes stock cleanup"
    require_pattern_count "$PRINTSVC_INIT" 'stop_stock_print_service' 2 \
        "native init has cleanup helper plus startup call"
    require_order "$PRINTSVC_INIT" '/etc/init\.d/printserver stop' '^[[:space:]]*stop_stock_print_service$' \
        "native init runs stock cleanup after stopping printserver"
    require_order "$PRINTSVC_INIT" '^[[:space:]]*stop_stock_print_service$' 'procd_open_instance' \
        "native init runs stock cleanup before native procd start"
    reject_pattern "$PRINTSVC_INIT" '/usr/bin/python3|python3 ' \
        "native init does not launch Python"

    if [ "$INSTALLER_REQUIRED" -eq 1 ]; then
        require_pattern "$INSTALLER" 'DENEB_PRINTSERVER_NATIVE_GATE' \
            "installer writes native printserver gate shim"
        require_pattern "$INSTALLER" 'stop_stock_print_service\(\)' \
            "installer shim has explicit stock service cleanup helper"
        require_pattern "$INSTALLER" '/home/cygnus/marlindriver/print_service\.py' \
            "installer shim targets exact stock print_service.py path"
        require_pattern "$INSTALLER" 'rm -f /var/run/printserver\.pid' \
            "installer shim clears stale printserver pid"
        require_pattern "$INSTALLER" 'native deneb-printsvc owns the print backend' \
            "installer shim declares native ownership"
        require_pattern_count "$INSTALLER" 'stop_stock_print_service' 3 \
            "installer shim has cleanup helper plus start/stop calls"
        require_order "$INSTALLER" 'logger -t deneb-printserver "native deneb-printsvc owns the print backend"' '^[[:space:]]*stop_stock_print_service$' \
            "installer shim cleans stock service during printserver start"
        require_order "$INSTALLER" 'rm -f /var/run/printserver\.pid' '^[[:space:]]*stop_stock_print_service$' \
            "installer shim cleans stock service during printserver stop"
        reject_pattern "$INSTALLER" '/usr/bin/python3 /home/cygnus/marlindriver/print_service\.py' \
            "installer does not launch stock print_service.py"
        require_pattern "$INSTALLER" 'manifest\.txt' \
            "installer requires package manifest"
        require_pattern "$INSTALLER" 'native_printsvc: experimental' \
            "installer checks native printsvc manifest status"
        require_pattern "$INSTALLER" 'native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction' \
            "installer checks native printsvc release gate"
        require_pattern "$INSTALLER" 'cp /tmp/update/manifest\.txt /etc/deneb/manifest\.txt' \
            "installer preserves package manifest"
        require_pattern "$INSTALLER" 'deneb-printsvc-native-audit --package-dir /tmp/update' \
            "installer runs packaged de-Python audit"
        require_pattern "$INSTALLER" 'cp /tmp/update/deneb-printsvc-native-audit /usr/bin/deneb-printsvc-native-audit' \
            "installer preserves de-Python audit tool"
        require_pattern "$INSTALLER" 'cp /tmp/update/deneb-printsvc-release-gate-selftest /usr/bin/deneb-printsvc-release-gate-selftest' \
            "installer preserves release gate selftest"
        require_pattern "$INSTALLER" 'deneb-printsvc-native-audit-selftest' \
            "installer runs native audit selftest"

        generated_shim="${TMP_DIR}/printserver.generated"
        if extract_printserver_shim "$INSTALLER" "$generated_shim"; then
            pass "installer printserver shim heredoc extracted"
            check_printserver_shim "$generated_shim" "installer-generated printserver shim"
        else
            fail "installer printserver shim heredoc extracted"
        fi
    fi

    if [ -n "$PRINTSERVER_INIT" ]; then
        require_file "$PRINTSERVER_INIT" "generated printserver shim exists"
        if [ -f "$PRINTSERVER_INIT" ]; then
            check_printserver_shim "$PRINTSERVER_INIT" "generated printserver shim"
        fi
    fi
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures init selftest failure(s)" >&2
    exit 1
fi

echo "deneb-printsvc init selftest passed"
