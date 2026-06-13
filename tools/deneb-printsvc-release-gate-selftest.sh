#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Shell-only checks for native print-service release-channel gates.
# These run build-package fail-fast paths with dummy binaries; they do not
# create an accepted release archive or touch live hardware.

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="${DENEB_REPO_ROOT:-$(cd "${SCRIPT_DIR}/.." && pwd)}"
TMP_DIR="${TMPDIR:-/tmp}/deneb-printsvc-release-gate-selftest.$$"
BUILD_PACKAGE="${REPO_ROOT}/ui/build-package.sh"
PACKAGE_VERSION="release-gate-selftest.$$"
PACKAGE_STAGING="${REPO_ROOT}/build/ui-package/Deneb_Update_${PACKAGE_VERSION}"
PACKAGE_ARCHIVE="${REPO_ROOT}/dist/Deneb_Update_${PACKAGE_VERSION}.deneb"

installed_runtime_selftest() {
    manifest="${DENEB_INSTALLED_MANIFEST:-/etc/deneb/manifest.txt}"
    failures=0

    pass() {
        echo "PASS: $*"
    }

    fail() {
        echo "FAIL: $*" >&2
        failures=$((failures + 1))
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

    require_file "$manifest" "installed package manifest exists"
    require_file "${SCRIPT_DIR}/deneb-printsvc-smoke-verify" "installed smoke verifier exists"
    require_file "${SCRIPT_DIR}/deneb-printsvc-smoke-compare" "installed smoke comparator exists"
    require_file "${SCRIPT_DIR}/deneb-printsvc-native-audit" "installed de-Python audit exists"
    require_file "${SCRIPT_DIR}/deneb-printsvc-native-audit-selftest" "installed de-Python audit selftest exists"

    if [ -f "$manifest" ]; then
        require_pattern "$manifest" '^native_printsvc: experimental$' \
            "installed manifest records experimental native printsvc"
        require_pattern "$manifest" 'native_printsvc_release_gate: non-experimental packages require verified stock/native smoke summaries with strict resource reduction' \
            "installed manifest records non-experimental release gate"
        require_pattern "$manifest" 'no Python driver files are packaged; native deneb-printsvc owns marlindriver' \
            "installed manifest records native marlindriver ownership"
    fi

    if [ "$failures" -ne 0 ]; then
        echo "$failures installed release gate selftest failure(s)" >&2
        exit 1
    fi

    echo "deneb-printsvc installed release gate selftest passed"
}

if [ ! -f "$BUILD_PACKAGE" ]; then
    installed_runtime_selftest
    exit 0
fi

cleanup() {
    rm -rf "$PACKAGE_STAGING"
    rm -f "$PACKAGE_ARCHIVE"
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT HUP INT TERM
mkdir -p "$TMP_DIR/bin"

touch "$TMP_DIR/bin/deneb-ui" \
    "$TMP_DIR/bin/deneb-api" \
    "$TMP_DIR/bin/lighttpd" \
    "$TMP_DIR/bin/deneb-mdns" \
    "$TMP_DIR/bin/deneb-printsvc" \
    "$TMP_DIR/bin/deneb-dfsvc"
chmod 0755 "$TMP_DIR/bin/"*

expect_failure() {
    label="$1"
    pattern="$2"
    shift 2
    if "$@" > "$TMP_DIR/$label.out" 2>&1; then
        cat "$TMP_DIR/$label.out"
        echo "FAIL: expected release-gate failure: $label" >&2
        exit 1
    fi
    if ! grep -Eq "$pattern" "$TMP_DIR/$label.out"; then
        cat "$TMP_DIR/$label.out"
        echo "FAIL: release-gate failure did not match expectation: $label" >&2
        exit 1
    fi
    echo "PASS: expected release-gate failure: $label"
}

write_valid_stock_summary() {
    path="$1"
    cat > "$path" <<'EOF'
2026-06-08T00:00:00Z start api=http://127.0.0.1/api/v1 cluster_api=http://127.0.0.1 native=0 stock=1 heat=0 motion=0 macro=0 local_job=0 job=0 cura_job=0 preheat_abort=0 active_abort=0 active_abort_delay=60 complete_job=1 pause_resume=0 restart=0 boot_sync=1 client_proof=0 firmware_proof=1
2026-06-08T00:00:00Z phase=printsvc-self-test rc=0
2026-06-08T00:00:00Z phase=stock-driver-process kind=process deneb_printsvc=0 print_service_py=1 rc=0
2026-06-08T00:00:00Z snapshot=initial
2026-06-08T00:00:00Z phase=status-initial kind=api method=GET path=/printer/status rc=0 status=idle body=idle
2026-06-08T00:00:00Z phase=printer-initial kind=api method=GET path=/printer rc=0 body={status:idle}
2026-06-08T00:00:00Z sample=initial mem_total_kb=250000 mem_used_kb=120000
2026-06-08T00:00:00Z sample=initial uptime_seconds=100
2026-06-08T00:00:00Z sample=initial cpu_total_jiffies=1000
2026-06-08T00:00:00Z sample=initial load1=0.20
2026-06-08T00:00:00Z sample=initial pid=111 vmsize_kb=33000 vmrss_kb=12000 command="/usr/bin/python3 /home/cygnus/marlindriver/print_service.py"
2026-06-08T00:01:00Z phase=job-throughput path=/tmp/stock.gcode bytes=10000 elapsed_seconds=20 bytes_per_second=500 rc=0
2026-06-08T00:02:00Z sample=final mem_total_kb=250000 mem_used_kb=122000
2026-06-08T00:02:00Z sample=final uptime_seconds=220
2026-06-08T00:02:00Z sample=final cpu_total_jiffies=1500
2026-06-08T00:02:00Z sample=final load1=0.30
2026-06-08T00:02:00Z sample=final pid=111 vmsize_kb=33000 vmrss_kb=12100 command="/usr/bin/python3 /home/cygnus/marlindriver/print_service.py"
2026-06-08T00:02:00Z snapshot=final
EOF
}

expect_failure invalid_channel \
    'DENEB_RELEASE_CHANNEL must be experimental, nightly, or stable' \
    env DENEB_RELEASE_CHANNEL=beta \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

expect_failure stable_missing_summaries \
    'non-experimental native printsvc builds require DENEB_PRINTSVC_STOCK_SUMMARY and DENEB_PRINTSVC_NATIVE_SUMMARY' \
    env DENEB_RELEASE_CHANNEL=stable \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

expect_failure nightly_missing_stock_summary \
    'stock printsvc smoke summary not found' \
    env DENEB_RELEASE_CHANNEL=nightly \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
        DENEB_PRINTSVC_STOCK_SUMMARY="$TMP_DIR/missing-stock.summary" \
        DENEB_PRINTSVC_NATIVE_SUMMARY="$TMP_DIR/missing-native.summary" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

touch "$TMP_DIR/stock.summary"
expect_failure nightly_missing_native_summary \
    'native printsvc smoke summary not found' \
    env DENEB_RELEASE_CHANNEL=nightly \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
        DENEB_PRINTSVC_STOCK_SUMMARY="$TMP_DIR/stock.summary" \
        DENEB_PRINTSVC_NATIVE_SUMMARY="$TMP_DIR/missing-native.summary" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

touch "$TMP_DIR/native.summary"
expect_failure nightly_invalid_stock_summary \
    'summary file missing or empty' \
    env DENEB_RELEASE_CHANNEL=nightly \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
        DENEB_PRINTSVC_STOCK_SUMMARY="$TMP_DIR/stock.summary" \
        DENEB_PRINTSVC_NATIVE_SUMMARY="$TMP_DIR/native.summary" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

write_valid_stock_summary "$TMP_DIR/stock.summary"
expect_failure nightly_invalid_native_summary \
    'summary file missing or empty' \
    env DENEB_RELEASE_CHANNEL=nightly \
        DENEB_PACKAGE_VERSION_OVERRIDE="$PACKAGE_VERSION" \
        DENEB_PRINTSVC_STOCK_SUMMARY="$TMP_DIR/stock.summary" \
        DENEB_PRINTSVC_NATIVE_SUMMARY="$TMP_DIR/native.summary" \
    sh "$BUILD_PACKAGE" \
        "$TMP_DIR/bin/deneb-ui" \
        "$TMP_DIR/bin/deneb-api" \
        "$TMP_DIR/bin/lighttpd" \
        "$TMP_DIR/bin/deneb-mdns" \
        "$TMP_DIR/bin/deneb-printsvc" \
        "$TMP_DIR/bin/deneb-dfsvc"

echo "deneb-printsvc release gate selftest passed"
