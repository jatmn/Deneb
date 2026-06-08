#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Host/target CLI smoke coverage for the native deneb-printsvc binary.
# This stays shell-only so CTest and target-side package checks do not need
# Python just to prove the native smoke entry points.

set -eu

PRINTSVC="${1:-${DENEB_PRINTSVC_BIN:-deneb-printsvc}}"
TMP_DIR="${TMPDIR:-/tmp}/deneb-printsvc-cli-selftest.$$"
JOB_PATH="$TMP_DIR/local-job.gcode"
OUT="$TMP_DIR/out.txt"

cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$TMP_DIR"

require_pattern() {
    pattern="$1"
    label="$2"

    if grep -Eq "$pattern" "$OUT"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label" >&2
        cat "$OUT" >&2
        exit 1
    fi
}

"$PRINTSVC" --smoke-test >"$OUT" 2>&1
echo "PASS: native smoke-test entry point"

printf '%s\n' 'M104 S0' 'M140 S0' 'G1 X1 Y1 F1200' >"$JOB_PATH"
"$PRINTSVC" --local-job-smoke "$JOB_PATH" >"$OUT" 2>&1

require_pattern \
    'phase=local-job-accepted .*deneb_state=pre_print .*native_active=true .*native_stop_allowed=true .*source=USB .*rc=0' \
    "local-job accepted state is native and stoppable"
require_pattern \
    'phase=local-job-aborted-state .*deneb_state=idle .*native_active=false .*native_stop_allowed=false .*source=USB .*rc=0' \
    "local-job aborted state is idle and not stoppable"

echo "deneb-printsvc CLI selftest passed"
