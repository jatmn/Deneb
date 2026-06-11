#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Live-device active Section 8 soak runner. This intentionally delegates all
# heat, homing, motion, job, and verification work to deneb-printsvc-smoke so
# the same physical safety gates and bounded fixtures are used for every cycle.

set -u

SUMMARY="${DENEB_ACTIVE_SOAK_SUMMARY:-/tmp/deneb-active-physical-soak.summary}"
LOG="${DENEB_ACTIVE_SOAK_LOG:-/tmp/deneb-active-physical-soak.log}"
FIXTURE="${DENEB_ACTIVE_SOAK_FIXTURE:-/tmp/deneb-active-physical-soak-xyz.gcode}"
DURATION_SECONDS="${DENEB_ACTIVE_SOAK_SECONDS:-7200}"
FIXTURE_CYCLES="${DENEB_ACTIVE_SOAK_FIXTURE_CYCLES:-30}"
COMPLETION_TIMEOUT="${DENEB_ACTIVE_SOAK_COMPLETION_TIMEOUT:-420}"
MAX_RSS_DELTA_KB="${DENEB_ACTIVE_SOAK_MAX_RSS_DELTA_KB:-2048}"
KEEP_ITERATION_ARTIFACTS="${DENEB_ACTIVE_SOAK_KEEP_ITERATION_ARTIFACTS:-0}"

SMOKE="${DENEB_PRINTSVC_SMOKE:-/usr/bin/deneb-printsvc-smoke}"
VERIFY="${DENEB_PRINTSVC_SMOKE_VERIFY:-/usr/bin/deneb-printsvc-smoke-verify}"

usage() {
    cat <<'EOF'
Usage: deneb-active-physical-soak-runner [options]

Options:
  --duration SEC          Time-bounded active soak length, default 7200
  --fixture PATH          Representative XYZ fixture path
  --fixture-cycles N      Representative fixture cycles, default 30
  --completion-timeout S  Max seconds per completed print job, default 420
  --max-rss-delta-kb N    Fail if native driver RSS grows by more than N
  --keep-iteration-artifacts
                           Preserve each per-iteration smoke log/summary
  --summary PATH          Aggregate summary path
  --log PATH              Aggregate log path
  -h, --help              Show this help

Each iteration runs low heat/cooldown, guarded homing, the home macro, and a
bounded representative XYZ complete-job through the installed smoke harness.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --duration)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --duration" >&2; exit 2; }
            DURATION_SECONDS="$1"
            ;;
        --fixture)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --fixture" >&2; exit 2; }
            FIXTURE="$1"
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
        --keep-iteration-artifacts)
            KEEP_ITERATION_ARTIFACTS=1
            ;;
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

for value in "$DURATION_SECONDS" "$FIXTURE_CYCLES" "$COMPLETION_TIMEOUT" "$MAX_RSS_DELTA_KB"; do
    case "$value" in
        ''|*[!0-9]*) echo "invalid numeric option: $value" >&2; exit 2 ;;
    esac
done

mkdir -p "$(dirname "$SUMMARY")" "$(dirname "$LOG")" 2>/dev/null || true
: >"$SUMMARY" || { echo "cannot write summary: $SUMMARY" >&2; exit 1; }
: >"$LOG" || { echo "cannot write log: $LOG" >&2; exit 1; }

say() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" | tee -a "$LOG"
}

summary() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" >>"$SUMMARY"
}

native_proc_pid() {
    ps w | grep -E '(^|[ /])deneb-printsvc([ ]|$)' | grep -v grep |
        while read -r pid rest; do
            case "$pid" in
                ''|*[!0-9]*) continue ;;
            esac
            echo "$pid"
            return
        done
}

native_rss_kb() {
    pid="$(native_proc_pid)"
    case "$pid" in
        ''|*[!0-9]*) echo 0; return ;;
    esac
    awk '/^VmRSS:/ {print $2; found=1} END {if (!found) print 0}' \
        "/proc/$pid/status" 2>/dev/null
}

native_proc_stats() {
    pid="$(native_proc_pid)"
    case "$pid" in
        ''|*[!0-9]*)
            echo "deneb_printsvc_pid=0 deneb_printsvc_vmsize_kb=0 deneb_printsvc_vmdata_kb=0 deneb_printsvc_vmhwm_kb=0 deneb_printsvc_threads=0 deneb_printsvc_fd_count=0 deneb_printsvc_statm_size_pages=0 deneb_printsvc_statm_resident_pages=0 deneb_printsvc_statm_shared_pages=0 deneb_printsvc_private_kb=0"
            return
            ;;
    esac

    set -- $(cat "/proc/$pid/statm" 2>/dev/null || echo "0 0 0 0 0 0 0")
    statm_size="${1:-0}"
    statm_resident="${2:-0}"
    statm_shared="${3:-0}"
    private_pages=$((statm_resident - statm_shared))
    if [ "$private_pages" -lt 0 ]; then
        private_pages=0
    fi
    private_kb=$((private_pages * 4))
    fd_count="$(ls "/proc/$pid/fd" 2>/dev/null | wc -l | awk '{print $1}')"
    status_fields="$(
            awk '/^VmRSS:/ {print $2; found=1} END {if (!found) print 0}' \
                "/proc/$pid/status" 2>/dev/null
        )"
    proc_fields="$(
        awk '
            /^VmSize:/ {vmsize=$2}
            /^VmData:/ {vmdata=$2}
            /^VmHWM:/ {vmhwm=$2}
            /^Threads:/ {threads=$2}
            END {
                if (vmsize == "") vmsize = 0
                if (vmdata == "") vmdata = 0
                if (vmhwm == "") vmhwm = 0
                if (threads == "") threads = 0
                printf "deneb_printsvc_vmsize_kb=%s deneb_printsvc_vmdata_kb=%s deneb_printsvc_vmhwm_kb=%s deneb_printsvc_threads=%s", vmsize, vmdata, vmhwm, threads
            }
        ' "/proc/$pid/status" 2>/dev/null
    )"
    echo "deneb_printsvc_pid=$pid $proc_fields deneb_printsvc_fd_count=${fd_count:-0} deneb_printsvc_statm_size_pages=$statm_size deneb_printsvc_statm_resident_pages=$statm_resident deneb_printsvc_statm_shared_pages=$statm_shared deneb_printsvc_private_kb=$private_kb deneb_printsvc_status_rss_kb=${status_fields:-0}"
}

sample() {
    label="$1"
    status="$(wget -qO- http://127.0.0.1/api/v1/printer/status 2>/dev/null || echo unknown)"
    printer="$(wget -qO- http://127.0.0.1/api/v1/printer 2>/dev/null | tr -d '\n' || echo none)"
    rss="$(native_rss_kb)"
    rss="${rss:-0}"
    mem="$(free | awk '/^Mem:/ {print $3}' 2>/dev/null || echo 0)"
    cpu="$(awk '/^cpu / {sum=0; for (i=2; i<=NF; i++) sum += $i; print sum}' /proc/stat 2>/dev/null || echo 0)"
    uptime="$(awk '{printf("%d", $1)}' /proc/uptime 2>/dev/null || echo 0)"
    tmp_used="$(df -k /tmp 2>/dev/null | awk 'NR==2 {print $3}' || echo 0)"
    proc_stats="$(native_proc_stats)"
    summary "sample=$label uptime_seconds=${uptime:-0} mem_used_kb=${mem:-0} tmp_used_kb=${tmp_used:-0} cpu_total_jiffies=${cpu:-0} deneb_printsvc_rss_kb=$rss $proc_stats status=$status printer=$printer"
}

cooldown() {
    wget -qO- --method=PUT --body-data='{"target":0}' \
        http://127.0.0.1/api/v1/printer/bed/temperature >/dev/null 2>&1 || true
    wget -qO- --method=PUT --body-data='{"target":0}' \
        http://127.0.0.1/api/v1/printer/heads/0/extruders/0/hotend/temperature >/dev/null 2>&1 || true
}

record_iteration_artifacts() {
    iter_summary="$1"
    iter_log="$2"
    rc="$3"

    if [ "$KEEP_ITERATION_ARTIFACTS" = "1" ]; then
        return
    fi

    if [ "$rc" = "0" ]; then
        rm -f "$iter_summary" "$iter_log" 2>/dev/null || true
        return
    fi

    {
        echo "--- failed iteration log tail: $iter_log ---"
        tail -120 "$iter_log" 2>/dev/null || true
        echo "--- failed iteration summary: $iter_summary ---"
        cat "$iter_summary" 2>/dev/null || true
    } >>"$LOG"
}

if [ ! -x "$SMOKE" ] || [ ! -x "$VERIFY" ]; then
    echo "missing installed smoke or verifier tool" >&2
    exit 1
fi

if [ ! -f "$FIXTURE" ]; then
    "$SMOKE" --make-representative-fixture "$FIXTURE" "$FIXTURE_CYCLES" >>"$LOG" 2>&1 || exit $?
fi

start_time="$(date +%s)"
end_time=$((start_time + DURATION_SECONDS))
summary "start mode=active-physical-soak duration_seconds=$DURATION_SECONDS fixture=$FIXTURE fixture_cycles=$FIXTURE_CYCLES max_rss_delta_kb=$MAX_RSS_DELTA_KB keep_iteration_artifacts=$KEEP_ITERATION_ARTIFACTS rc=0"
say "active physical soak started duration=${DURATION_SECONDS}s fixture=$FIXTURE"
sample initial
initial_rss="$(native_rss_kb)"
initial_rss="${initial_rss:-0}"

i=1
while [ "$(date +%s)" -lt "$end_time" ]; do
    iter_summary="/tmp/deneb-active-physical-soak-$i.summary"
    iter_log="/tmp/deneb-active-physical-soak-$i.log"
    sample "iteration-${i}-before"
    say "iteration $i: heat/cool, home, representative XYZ complete-job"
    "$SMOKE" --native-no-restart --boot-sync --heat --motion --macro home \
        --complete-job "$FIXTURE" --completion-timeout "$COMPLETION_TIMEOUT" \
        --prehome-action home --physical-ok --physical-bundle-ok \
        --summary "$iter_summary" --log "$iter_log" >/dev/null 2>>"$LOG"
    rc=$?
    if [ "$rc" = "0" ]; then
        "$VERIFY" --native --idle --boot-sync --complete-job --resources \
            "$iter_summary" >>"$LOG" 2>&1
        rc=$?
    fi
    sample "iteration-${i}-after"
    summary "phase=active-soak-iteration index=$i summary=$iter_summary log=$iter_log rc=$rc"
    record_iteration_artifacts "$iter_summary" "$iter_log" "$rc"
    if [ "$rc" != "0" ]; then
        cooldown
        summary "phase=active-soak-result iterations=$i rc=$rc reason=iteration_failed"
        say "active physical soak failed at iteration $i rc=$rc"
        exit "$rc"
    fi
    i=$((i + 1))
done

sample final
final_rss="$(native_rss_kb)"
final_rss="${final_rss:-0}"
rss_delta=$((final_rss - initial_rss))
elapsed=$(( $(date +%s) - start_time ))
iters=$((i - 1))
if [ "$rss_delta" -le "$MAX_RSS_DELTA_KB" ]; then
    summary "phase=active-soak-result iterations=$iters elapsed_seconds=$elapsed rss_initial_kb=$initial_rss rss_final_kb=$final_rss rss_delta_kb=$rss_delta max_rss_delta_kb=$MAX_RSS_DELTA_KB rc=0"
    say "active physical soak complete iterations=$iters elapsed=${elapsed}s"
    exit 0
fi

cooldown
summary "phase=active-soak-result iterations=$iters elapsed_seconds=$elapsed rss_initial_kb=$initial_rss rss_final_kb=$final_rss rss_delta_kb=$rss_delta max_rss_delta_kb=$MAX_RSS_DELTA_KB rc=1 reason=rss_growth"
say "active physical soak failed: RSS delta ${rss_delta} KiB exceeds ${MAX_RSS_DELTA_KB} KiB"
exit 1
