#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Device-side smoke/measurement harness for the lab-gated native print service.
# Default mode is observe-only. Hardware-moving/heating phases require explicit
# flags so the script can be shipped safely before live validation is allowed.

set -u

API_BASE="${DENEB_API_BASE:-http://127.0.0.1/api/v1}"
LOG="${DENEB_PRINTSVC_SMOKE_LOG:-/tmp/deneb-printsvc-smoke.log}"
SUMMARY="${DENEB_PRINTSVC_SMOKE_SUMMARY:-/tmp/deneb-printsvc-smoke.summary}"
ENABLE_NATIVE=0
RESTORE_ROUTE=1
RUN_HEAT=0
RUN_MOTION=0
RUN_JOB=0
JOB_PATH=""
OLD_NATIVE=""
ROUTE_RESTORED=0

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke [options]

Observe-only by default:
  --native              Temporarily route Deneb clients to deneb-printsvc
  --no-restore          Leave deneb.printsvc.enabled at its final value
  --heat                Exercise low bed/nozzle heat targets, then cool down
  --motion              Exercise a Z-home request through the REST API
  --job PATH            Start PATH through the REST API and abort it after a sample
  --log PATH            Write the smoke log to PATH
  --summary PATH        Write compact evidence summary to PATH
  --api URL             Override API base, default http://127.0.0.1/api/v1
  -h, --help            Show this help

The heat, motion, and job phases move or heat hardware. Run them only with the
printer supervised, clear of obstructions, and prepared for immediate power-off.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --native) ENABLE_NATIVE=1 ;;
        --no-restore) RESTORE_ROUTE=0 ;;
        --heat) RUN_HEAT=1 ;;
        --motion) RUN_MOTION=1 ;;
        --job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --job" >&2; exit 2; }
            RUN_JOB=1
            JOB_PATH="$1"
            ;;
        --log)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --log" >&2; exit 2; }
            LOG="$1"
            ;;
        --summary)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --summary" >&2; exit 2; }
            SUMMARY="$1"
            ;;
        --api)
            shift
            [ "$#" -gt 0 ] || { echo "missing URL after --api" >&2; exit 2; }
            API_BASE="$1"
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

mkdir -p "$(dirname "$LOG")" 2>/dev/null || true
: >"$LOG" || {
    echo "cannot write log: $LOG" >&2
    exit 1
}
mkdir -p "$(dirname "$SUMMARY")" 2>/dev/null || true
: >"$SUMMARY" || {
    echo "cannot write summary: $SUMMARY" >&2
    exit 1
}

say() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" | tee -a "$LOG"
}

summary() {
    printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || date)" "$*" >>"$SUMMARY"
}

append_cmd() {
    say "$ $*"
    "$@" >>"$LOG" 2>&1
    rc=$?
    say "rc=$rc"
    return "$rc"
}

http_request() {
    method="$1"
    path="$2"
    body="${3:-}"
    url="${API_BASE}${path}"

    if command -v curl >/dev/null 2>&1; then
        if [ -n "$body" ]; then
            curl -fsS -X "$method" -H 'Content-Type: application/json' \
                --data "$body" "$url" >>"$LOG" 2>&1
        else
            curl -fsS -X "$method" "$url" >>"$LOG" 2>&1
        fi
        return $?
    fi

    if [ "$method" = "GET" ] && command -v wget >/dev/null 2>&1; then
        wget -q -O - "$url" >>"$LOG" 2>&1
        return $?
    fi

    say "SKIP: no curl available for $method $url"
    return 0
}

api_step() {
    label="$1"
    method="$2"
    path="$3"
    body="${4:-}"
    say "$label: $method $path"
    http_request "$method" "$path" "$body"
    rc=$?
    say "$label rc=$rc"
    summary "phase=$label kind=api method=$method path=$path rc=$rc"
    return "$rc"
}

sample_processes() {
    label="$1"
    ps w | grep -E 'deneb-printsvc|print_service.py|coordinator.py|deneb-api|deneb-ui' |
        grep -v grep | while read -r pid rest; do
            case "$pid" in
                ''|*[!0-9]*) continue ;;
            esac
            status="/proc/$pid/status"
            cmdline="/proc/$pid/cmdline"
            vmsize="$(awk '/^VmSize:/ {print $2}' "$status" 2>/dev/null || true)"
            vmrss="$(awk '/^VmRSS:/ {print $2}' "$status" 2>/dev/null || true)"
            command="$(tr '\0' ' ' <"$cmdline" 2>/dev/null || printf '%s' "$rest")"
            summary "sample=$label pid=$pid vmsize_kb=${vmsize:-0} vmrss_kb=${vmrss:-0} command=\"$command\""
        done
}

snapshot() {
    label="$1"
    say "===== snapshot: $label ====="
    summary "snapshot=$label"
    append_cmd cat /proc/uptime || true
    awk -v label="$label" '{printf("sample=%s uptime_seconds=%s idle_seconds=%s\n", label, $1, $2)}' \
        /proc/uptime >>"$SUMMARY" 2>/dev/null || true
    append_cmd free || true
    free | awk -v label="$label" '
        /^Mem:/ {printf("sample=%s mem_total_kb=%s mem_used_kb=%s mem_free_kb=%s\n", label, $2, $3, $4)}
        /^Swap:/ {printf("sample=%s swap_total_kb=%s swap_used_kb=%s swap_free_kb=%s\n", label, $2, $3, $4)}
    ' >>"$SUMMARY" 2>/dev/null || true
    append_cmd df -h || true
    append_cmd sh -c "ps w | grep -E 'deneb-printsvc|print_service.py|coordinator.py|deneb-api|deneb-ui' | grep -v grep" || true
    sample_processes "$label"
    api_step "route-$label" GET /deneb/print_backend || true
    api_step "status-$label" GET /printer/status || true
}

restore_route() {
    if [ "$ROUTE_RESTORED" = "1" ]; then
        return
    fi
    if [ "$ENABLE_NATIVE" = "1" ] && [ "$RESTORE_ROUTE" = "1" ] && [ -n "$OLD_NATIVE" ]; then
        say "restoring deneb.printsvc.enabled=$OLD_NATIVE"
        uci -q set deneb.printsvc.enabled="$OLD_NATIVE" 2>/dev/null || true
        uci -q commit deneb 2>/dev/null || true
        /etc/init.d/deneb-printsvc stop >>"$LOG" 2>&1 || true
        /etc/init.d/printserver restart >>"$LOG" 2>&1 || true
        /etc/init.d/deneb-api restart >>"$LOG" 2>&1 || true
        ROUTE_RESTORED=1
        summary "phase=route-restored value=$OLD_NATIVE"
    fi
}

trap restore_route EXIT INT TERM

say "deneb-printsvc smoke started"
say "log=$LOG summary=$SUMMARY api=$API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION job=$RUN_JOB"
summary "start api=$API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION job=$RUN_JOB"

append_cmd /usr/bin/deneb-printsvc --smoke-test
summary "phase=printsvc-self-test rc=$?"
snapshot "initial"

if [ "$ENABLE_NATIVE" = "1" ]; then
    OLD_NATIVE="$(uci -q get deneb.printsvc.enabled 2>/dev/null || echo 0)"
    say "enabling native route, previous deneb.printsvc.enabled=$OLD_NATIVE"
    append_cmd uci -q set deneb.printsvc.enabled=1 || true
    append_cmd uci -q commit deneb || true
    append_cmd /etc/init.d/printserver stop || true
    append_cmd /etc/init.d/deneb-printsvc restart || true
    append_cmd /etc/init.d/deneb-api restart || true
    sleep 3
    summary "phase=native-route-enabled previous=$OLD_NATIVE"
    snapshot "native-enabled"
fi

if [ "$RUN_HEAT" = "1" ]; then
    api_step "bed-low-heat" PUT /printer/bed/temperature '{"target":40}'
    api_step "nozzle-low-heat" PUT /printer/heads/0/extruders/0/hotend/temperature '{"target":50}'
    sleep 10
    snapshot "heating"
    api_step "bed-cooldown" PUT /printer/bed/temperature '{"target":0}' || true
    api_step "nozzle-cooldown" PUT /printer/heads/0/extruders/0/hotend/temperature '{"target":0}' || true
    sleep 5
    snapshot "cooldown"
fi

if [ "$RUN_MOTION" = "1" ]; then
    api_step "z-home" POST /printer/heads/0/position '{"action":"z_home"}'
    sleep 5
    snapshot "motion"
fi

if [ "$RUN_JOB" = "1" ]; then
    if [ ! -f "$JOB_PATH" ]; then
        say "ERROR: job path does not exist: $JOB_PATH"
        exit 1
    fi
    if ! command -v curl >/dev/null 2>&1; then
        say "SKIP: job upload requires curl for multipart/form-data"
        RUN_JOB=0
    fi
fi

if [ "$RUN_JOB" = "1" ]; then
    say "job-start: multipart upload $JOB_PATH"
    curl -fsS -F "file=@${JOB_PATH}" -F "jobname=$(basename "$JOB_PATH")" \
        -F "owner=Deneb smoke" "${API_BASE}/print_job" >>"$LOG" 2>&1
    rc=$?
    say "job-start rc=$rc"
    summary "phase=job-start kind=multipart path=$JOB_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 20
    snapshot "job-running"
    api_step "job-abort" PUT /print_job/state '{"action":"abort"}' || \
        api_step "job-stop" PUT /print_job/state '{"action":"stop"}' || true
    sleep 10
    snapshot "job-aborted"
fi

snapshot "final"
if [ "$ENABLE_NATIVE" = "1" ] && [ "$RESTORE_ROUTE" = "1" ]; then
    restore_route
    sleep 3
    snapshot "restored"
fi
summary "complete log=$LOG summary=$SUMMARY"
say "deneb-printsvc smoke complete"
