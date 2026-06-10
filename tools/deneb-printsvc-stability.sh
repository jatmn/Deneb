#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Repeated native print-service stability harness. This wraps the existing smoke
# harness so stability evidence reuses the same route, resource, flow-drain, and
# physical safety checks instead of growing a separate motion path.

set -u

SCRIPT_DIR="$(CDPATH= cd "$(dirname "$0")" && pwd)"
SMOKE="${DENEB_PRINTSVC_SMOKE:-${SCRIPT_DIR}/deneb-printsvc-smoke.sh}"
VERIFY="${DENEB_PRINTSVC_SMOKE_VERIFY:-${SCRIPT_DIR}/deneb-printsvc-smoke-verify.sh}"
if [ ! -f "$SMOKE" ] && [ -x /usr/bin/deneb-printsvc-smoke ]; then
    SMOKE=/usr/bin/deneb-printsvc-smoke
fi
if [ ! -f "$VERIFY" ] && [ -x /usr/bin/deneb-printsvc-smoke-verify ]; then
    VERIFY=/usr/bin/deneb-printsvc-smoke-verify
fi

SUMMARY="${DENEB_PRINTSVC_STABILITY_SUMMARY:-/tmp/deneb-printsvc-stability.summary}"
LOG="${DENEB_PRINTSVC_STABILITY_LOG:-/tmp/deneb-printsvc-stability.log}"
ITERATIONS="${DENEB_PRINTSVC_STABILITY_ITERATIONS:-3}"
SLEEP_SECONDS="${DENEB_PRINTSVC_STABILITY_SLEEP_SECONDS:-30}"
MAX_RSS_DELTA_KB="${DENEB_PRINTSVC_STABILITY_MAX_RSS_DELTA_KB:-2048}"
COMPLETION_TIMEOUT="${DENEB_PRINTSVC_STABILITY_COMPLETION_TIMEOUT:-300}"
PREHOME_ACTION="${DENEB_PRINTSVC_STABILITY_PREHOME_ACTION:-z_home}"
COMPLETE_JOB_PATH=""
FIXTURE_CYCLES=80
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
RUN_SELFTEST=0

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-stability [options]

Options:
  --iterations N          Number of stability samples/runs, default 3
  --sleep SEC             Delay between observe-only samples, default 30
  --complete-job PATH     Repeat a bounded completion job each iteration
  --fixture-cycles N      Cycles for generated completion fixture if PATH is
                          missing, default 80
  --completion-timeout S  Max seconds for each completion job, default 300
  --max-rss-delta-kb N    Fail if final native driver RSS grows by more than N,
                          default 2048
  --prehome-action ACTION Prehome action for repeated completion jobs,
                          default z_home
  --physical-ok           Required with --complete-job
  --summary PATH          Write aggregate stability summary
  --log PATH              Write aggregate stability log
  --selftest              Run shell-only fixture tests and exit
  -h, --help              Show this help

Without --complete-job this is observe-only resource sampling. With
--complete-job it repeatedly delegates to deneb-printsvc-smoke using
--native-no-restart, so the same native service stays alive across iterations.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --iterations)
            shift
            [ "$#" -gt 0 ] || { echo "missing count after --iterations" >&2; exit 2; }
            ITERATIONS="$1"
            ;;
        --sleep)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --sleep" >&2; exit 2; }
            SLEEP_SECONDS="$1"
            ;;
        --complete-job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --complete-job" >&2; exit 2; }
            COMPLETE_JOB_PATH="$1"
            ;;
        --fixture-cycles)
            shift
            [ "$#" -gt 0 ] || { echo "missing count after --fixture-cycles" >&2; exit 2; }
            FIXTURE_CYCLES="$1"
            ;;
        --completion-timeout)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --completion-timeout" >&2; exit 2; }
            COMPLETION_TIMEOUT="$1"
            ;;
        --max-rss-delta-kb)
            shift
            [ "$#" -gt 0 ] || { echo "missing KiB after --max-rss-delta-kb" >&2; exit 2; }
            MAX_RSS_DELTA_KB="$1"
            ;;
        --prehome-action)
            shift
            [ "$#" -gt 0 ] || { echo "missing action after --prehome-action" >&2; exit 2; }
            PREHOME_ACTION="$1"
            ;;
        --physical-ok) PHYSICAL_OK=1 ;;
        --summary)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --summary" >&2; exit 2; }
            SUMMARY="$1"
            ;;
        --log)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --log" >&2; exit 2; }
            LOG="$1"
            ;;
        --selftest) RUN_SELFTEST=1 ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

for value in "$ITERATIONS" "$SLEEP_SECONDS" "$MAX_RSS_DELTA_KB" "$COMPLETION_TIMEOUT" "$FIXTURE_CYCLES"; do
    case "$value" in
        ''|*[!0-9]*) echo "invalid numeric option: $value" >&2; exit 2 ;;
    esac
done
if [ "$ITERATIONS" -lt 1 ]; then
    echo "--iterations must be greater than zero" >&2
    exit 2
fi
case "$PREHOME_ACTION" in
    z_home|home) ;;
    *) echo "invalid --prehome-action: $PREHOME_ACTION" >&2; exit 2 ;;
esac

mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
: >"$LOG" || { echo "cannot write log: $LOG" >&2; exit 1; }
mkdir -p "$(dirname "$SUMMARY")" 2>/dev/null || true
: >"$SUMMARY" || { echo "cannot write summary: $SUMMARY" >&2; exit 1; }

say() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" | tee -a "$LOG"
}

summary() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" >>"$SUMMARY"
}

native_rss_kb() {
    ps w | grep -E '(^|[ /])deneb-printsvc([ ]|$)' | grep -v grep |
        while read -r pid rest; do
            case "$pid" in
                ''|*[!0-9]*) continue ;;
            esac
            awk '/^VmRSS:/ {print $2; found=1} END {if (!found) print 0}' \
                "/proc/$pid/status" 2>/dev/null
            return
        done
}

sample_resources() {
    label="$1"
    rss="$(native_rss_kb)"
    rss="${rss:-0}"
    uptime="$(awk '{printf("%d", $1)}' /proc/uptime 2>/dev/null || printf '0')"
    mem_used="$(free | awk '/^Mem:/ {print $3}' 2>/dev/null || printf '0')"
    cpu_total="$(awk '/^cpu / {sum=0; for (i=2; i<=NF; i++) sum += $i; print sum}' /proc/stat 2>/dev/null || printf '0')"
    load1="$(awk '{print $1}' /proc/loadavg 2>/dev/null || printf '0')"
    summary "sample=$label uptime_seconds=${uptime:-0} mem_used_kb=${mem_used:-0} cpu_total_jiffies=${cpu_total:-0} load1=${load1:-0} deneb_printsvc_rss_kb=$rss"
}

run_selftest() {
    tmp_dir="${TMPDIR:-/tmp}/deneb-printsvc-stability-selftest.$$"
    mkdir -p "$tmp_dir" || exit 1
    trap 'rm -rf "$tmp_dir"' EXIT INT TERM

    DENEB_PRINTSVC_STABILITY_ITERATIONS=2 \
    DENEB_PRINTSVC_STABILITY_SLEEP_SECONDS=0 \
    DENEB_PRINTSVC_STABILITY_MAX_RSS_DELTA_KB=4096 \
    "$0" --iterations 2 --sleep 0 --summary "$tmp_dir/observe.summary" --log "$tmp_dir/observe.log" >/dev/null 2>&1 || {
        echo "FAIL: observe-only stability selftest run failed" >&2
        cat "$tmp_dir/observe.log" >&2 2>/dev/null || true
        exit 1
    }
    grep -q 'phase=stability-result .*iterations=2 .*rc=0' "$tmp_dir/observe.summary" || {
        echo "FAIL: observe-only stability result missing" >&2
        cat "$tmp_dir/observe.summary" >&2
        exit 1
    }
    if "$0" --iterations 1 --complete-job "$tmp_dir/missing.gcode" \
        --summary "$tmp_dir/reject.summary" --log "$tmp_dir/reject.log" >/dev/null 2>&1; then
        echo "FAIL: completion stability accepted missing --physical-ok" >&2
        exit 1
    fi
    echo "deneb-printsvc stability selftest passed"
}

if [ "$RUN_SELFTEST" = "1" ]; then
    run_selftest
    exit $?
fi

if [ -n "$COMPLETE_JOB_PATH" ] && [ "$PHYSICAL_OK" != "1" ]; then
    echo "--complete-job requires --physical-ok or DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1" >&2
    exit 2
fi

say "deneb-printsvc stability started"
summary "start iterations=$ITERATIONS sleep_seconds=$SLEEP_SECONDS complete_job=${COMPLETE_JOB_PATH:-none} max_rss_delta_kb=$MAX_RSS_DELTA_KB"

sample_resources "stability-initial"
initial_rss="$(native_rss_kb)"
initial_rss="${initial_rss:-0}"

if [ -n "$COMPLETE_JOB_PATH" ] && [ ! -f "$COMPLETE_JOB_PATH" ]; then
    say "generating completion fixture: $COMPLETE_JOB_PATH cycles=$FIXTURE_CYCLES"
    "$SMOKE" --make-complete-fixture "$COMPLETE_JOB_PATH" "$FIXTURE_CYCLES" >>"$LOG" 2>&1 || exit $?
fi

i=1
while [ "$i" -le "$ITERATIONS" ]; do
    sample_resources "stability-${i}-before"
    if [ -n "$COMPLETE_JOB_PATH" ]; then
        iter_summary="/tmp/deneb-printsvc-stability-$$-$i.summary"
        iter_log="/tmp/deneb-printsvc-stability-$$-$i.log"
        say "iteration $i: repeated completion smoke"
        "$SMOKE" --native-no-restart --boot-sync \
            --complete-job "$COMPLETE_JOB_PATH" \
            --completion-timeout "$COMPLETION_TIMEOUT" \
            --prehome-action "$PREHOME_ACTION" --physical-ok \
            --summary "$iter_summary" --log "$iter_log" >>"$LOG" 2>&1
        rc=$?
        cat "$iter_log" >>"$LOG" 2>/dev/null || true
        if [ "$rc" = "0" ]; then
            "$VERIFY" --native --idle --boot-sync --complete-job --resources "$iter_summary" >>"$LOG" 2>&1
            rc=$?
        fi
        summary "phase=stability-iteration index=$i mode=complete-job summary=$iter_summary rc=$rc"
        [ "$rc" = "0" ] || exit "$rc"
    else
        say "iteration $i: observe-only sample"
        summary "phase=stability-iteration index=$i mode=observe-only rc=0"
        sleep "$SLEEP_SECONDS"
    fi
    sample_resources "stability-${i}-after"
    i=$((i + 1))
done

sample_resources "stability-final"
final_rss="$(native_rss_kb)"
final_rss="${final_rss:-0}"
rss_delta=$((final_rss - initial_rss))
if [ "$rss_delta" -lt 0 ]; then
    rss_delta_abs=$((0 - rss_delta))
else
    rss_delta_abs="$rss_delta"
fi

if [ "$rss_delta" -le "$MAX_RSS_DELTA_KB" ]; then
    summary "phase=stability-result iterations=$ITERATIONS rss_initial_kb=$initial_rss rss_final_kb=$final_rss rss_delta_kb=$rss_delta max_rss_delta_kb=$MAX_RSS_DELTA_KB rc=0"
    say "deneb-printsvc stability complete"
    exit 0
fi

summary "phase=stability-result iterations=$ITERATIONS rss_initial_kb=$initial_rss rss_final_kb=$final_rss rss_delta_kb=$rss_delta max_rss_delta_kb=$MAX_RSS_DELTA_KB rc=1"
say "deneb-printsvc stability failed: RSS delta ${rss_delta_abs} KiB exceeds ${MAX_RSS_DELTA_KB} KiB"
exit 1
