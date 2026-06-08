#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Verify compact evidence produced by deneb-printsvc-smoke. This intentionally
# stays shell/awk/grep-only so it can run on the target without Python.

set -u

SUMMARY="${DENEB_PRINTSVC_SMOKE_SUMMARY:-/tmp/deneb-printsvc-smoke.summary}"
REQUIRE_NATIVE=0
REQUIRE_HEAT=0
REQUIRE_MOTION=0
REQUIRE_JOB=0

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke-verify [options] [summary-file]

Checks a deneb-printsvc-smoke summary for required evidence.

Options:
  --native      Require native route enable evidence
  --heat        Require heat/cool evidence
  --motion      Require Z-home motion evidence
  --job         Require multipart job start and abort/stop evidence
  --full        Require native, heat, motion, and job evidence
  -h, --help    Show this help

The verifier returns nonzero if required evidence is missing or if any recorded
API phase has a nonzero rc value.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --native) REQUIRE_NATIVE=1 ;;
        --heat) REQUIRE_HEAT=1 ;;
        --motion) REQUIRE_MOTION=1 ;;
        --job) REQUIRE_JOB=1 ;;
        --full)
            REQUIRE_NATIVE=1
            REQUIRE_HEAT=1
            REQUIRE_MOTION=1
            REQUIRE_JOB=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --*)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            SUMMARY="$1"
            ;;
    esac
    shift
done

failures=0

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

pass() {
    echo "PASS: $*"
}

require_pattern() {
    pattern="$1"
    label="$2"
    if grep -Eq "$pattern" "$SUMMARY"; then
        pass "$label"
    else
        fail "$label"
    fi
}

if [ ! -s "$SUMMARY" ]; then
    echo "summary file missing or empty: $SUMMARY" >&2
    exit 1
fi

require_pattern ' start .*native=' "start record present"
require_pattern ' phase=printsvc-self-test rc=0' "printsvc self-test passed"
require_pattern ' snapshot=initial' "initial snapshot present"
require_pattern ' snapshot=final' "final snapshot present"
require_pattern ' phase=route-initial .*rc=0' "initial route query passed"
require_pattern ' phase=status-initial .*rc=0' "initial status query passed"
require_pattern 'sample=initial .*mem_total_kb=' "initial memory sample present"

if grep -Eq ' phase=[^ ]+ .*rc=[1-9][0-9]*' "$SUMMARY"; then
    fail "one or more recorded phases failed"
else
    pass "no recorded phase failures"
fi

if [ "$REQUIRE_NATIVE" = "1" ]; then
    require_pattern ' phase=native-route-enabled ' "native route enabled"
    require_pattern ' snapshot=native-enabled' "native route snapshot present"
    require_pattern ' phase=route-native-enabled .*rc=0' "native route query passed"
    require_pattern ' phase=route-restored ' "native route restored"
    require_pattern ' snapshot=restored' "restored snapshot present"
    require_pattern ' phase=route-restored .*rc=0' "restored route query passed"
fi

if [ "$REQUIRE_HEAT" = "1" ]; then
    require_pattern ' phase=bed-low-heat .*rc=0' "bed heat command passed"
    require_pattern ' phase=nozzle-low-heat .*rc=0' "nozzle heat command passed"
    require_pattern ' snapshot=heating' "heating snapshot present"
    require_pattern ' phase=bed-cooldown .*rc=0' "bed cooldown command passed"
    require_pattern ' phase=nozzle-cooldown .*rc=0' "nozzle cooldown command passed"
    require_pattern ' snapshot=cooldown' "cooldown snapshot present"
fi

if [ "$REQUIRE_MOTION" = "1" ]; then
    require_pattern ' phase=z-home .*rc=0' "Z-home command passed"
    require_pattern ' snapshot=motion' "motion snapshot present"
fi

if [ "$REQUIRE_JOB" = "1" ]; then
    require_pattern ' phase=job-start .*rc=0' "job start passed"
    require_pattern ' snapshot=job-running' "job-running snapshot present"
    if grep -Eq ' phase=job-abort .*rc=0| phase=job-stop .*rc=0' "$SUMMARY"; then
        pass "job abort/stop passed"
    else
        fail "job abort/stop passed"
    fi
    require_pattern ' snapshot=job-aborted' "job-aborted snapshot present"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures verification failure(s)" >&2
    exit 1
fi

echo "summary verification passed: $SUMMARY"
