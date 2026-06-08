#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Device-side smoke/measurement harness for the native print service.
# Default mode is observe-only. Hardware-moving/heating phases require explicit
# flags so the script can be shipped safely before live validation is allowed.

set -u

API_BASE="${DENEB_API_BASE:-http://127.0.0.1/api/v1}"
CLUSTER_API_BASE="${DENEB_CLUSTER_API_BASE:-http://127.0.0.1/cluster-api/v1}"
LOG="${DENEB_PRINTSVC_SMOKE_LOG:-/tmp/deneb-printsvc-smoke.log}"
SUMMARY="${DENEB_PRINTSVC_SMOKE_SUMMARY:-/tmp/deneb-printsvc-smoke.summary}"
ENABLE_NATIVE=0
RUN_HEAT=0
RUN_MOTION=0
RUN_LOCAL_JOB=0
RUN_JOB=0
RUN_CURA_JOB=0
RUN_PREHEAT_ABORT=0
RUN_COMPLETE_JOB=0
RUN_MACRO=0
RUN_PAUSE_RESUME=0
RUN_RESTART=0
RUN_BOOT_SYNC=0
RUN_PARSER_SELFTEST=0
JOB_PATH=""
LOCAL_JOB_PATH=""
CURA_JOB_PATH=""
PREHEAT_ABORT_PATH=""
COMPLETE_JOB_PATH=""
MACRO_ACTION=""
COMPLETION_TIMEOUT="${DENEB_PRINTSVC_SMOKE_COMPLETION_TIMEOUT:-3600}"
READY_TIMEOUT="${DENEB_PRINTSVC_SMOKE_READY_TIMEOUT:-180}"

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke [options]

Observe-only by default:
  --native              Restart and assert the native deneb-printsvc route
  --no-restore          Accepted for older commands; native routing is not toggled
  --heat                Exercise low bed/nozzle heat targets, then cool down
  --motion              Exercise a Z-home request through the REST API
  --macro ACTION        Exercise a macro-backed manual action: home, bed_up, or bed_down
  --local-job PATH      Exercise native USB/local JOB acceptance for existing PATH
  --job PATH            Start PATH through the REST API and abort it after a sample
  --cura-job PATH       Start PATH through the Cura cluster API and abort it
  --preheat-abort PATH  Start PATH and abort quickly during preparation/preheat
  --pause-resume        During --job, pause and resume before aborting
  --complete-job PATH   Start PATH and wait for it to leave the active job API
  --completion-timeout SEC
                        Max seconds to wait for --complete-job, default 3600
  --restart             Restart deneb-printsvc and deneb-api, then sample recovery
  --boot-sync           Wait for print backend route/status readiness evidence
  --summary-parser-selftest
                        Exercise summary status/body parsing without hardware
  --ready-timeout SEC   Max seconds for --boot-sync, default 180
  --log PATH            Write the smoke log to PATH
  --summary PATH        Write compact evidence summary to PATH
  --api URL             Override API base, default http://127.0.0.1/api/v1
  --cluster-api URL     Override cluster API base, default http://127.0.0.1/cluster-api/v1
  -h, --help            Show this help

The heat, motion, macro, job, and complete-job phases move or heat hardware. Run
them only with the printer supervised, clear of obstructions, and prepared for
immediate power-off.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --native) ENABLE_NATIVE=1 ;;
        --no-restore) ;;
        --heat) RUN_HEAT=1 ;;
        --motion) RUN_MOTION=1 ;;
        --macro)
            shift
            [ "$#" -gt 0 ] || { echo "missing action after --macro" >&2; exit 2; }
            case "$1" in
                home|bed_up|bed_down) ;;
                *)
                    echo "unsupported macro action: $1" >&2
                    echo "supported macro actions: home, bed_up, bed_down" >&2
                    exit 2
                    ;;
            esac
            RUN_MACRO=1
            MACRO_ACTION="$1"
            ;;
        --local-job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --local-job" >&2; exit 2; }
            RUN_LOCAL_JOB=1
            LOCAL_JOB_PATH="$1"
            ;;
        --job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --job" >&2; exit 2; }
            RUN_JOB=1
            JOB_PATH="$1"
            ;;
        --cura-job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --cura-job" >&2; exit 2; }
            RUN_CURA_JOB=1
            CURA_JOB_PATH="$1"
            ;;
        --preheat-abort)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --preheat-abort" >&2; exit 2; }
            RUN_PREHEAT_ABORT=1
            PREHEAT_ABORT_PATH="$1"
            ;;
        --pause-resume) RUN_PAUSE_RESUME=1 ;;
        --complete-job)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --complete-job" >&2; exit 2; }
            RUN_COMPLETE_JOB=1
            COMPLETE_JOB_PATH="$1"
            ;;
        --completion-timeout)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --completion-timeout" >&2; exit 2; }
            case "$1" in
                ''|*[!0-9]*) echo "invalid completion timeout: $1" >&2; exit 2 ;;
            esac
            COMPLETION_TIMEOUT="$1"
            ;;
        --restart) RUN_RESTART=1 ;;
        --boot-sync) RUN_BOOT_SYNC=1 ;;
        --summary-parser-selftest) RUN_PARSER_SELFTEST=1 ;;
        --ready-timeout)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --ready-timeout" >&2; exit 2; }
            case "$1" in
                ''|*[!0-9]*) echo "invalid ready timeout: $1" >&2; exit 2 ;;
            esac
            READY_TIMEOUT="$1"
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
        --cluster-api)
            shift
            [ "$#" -gt 0 ] || { echo "missing URL after --cluster-api" >&2; exit 2; }
            CLUSTER_API_BASE="$1"
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

http_status_code() {
    method="$1"
    path="$2"
    url="${API_BASE}${path}"
    body_file="/tmp/deneb-printsvc-smoke-http.$$"

    if ! command -v curl >/dev/null 2>&1; then
        say "SKIP: no curl available for status-code check $method $url"
        return 2
    fi

    code="$(curl -sS -o "$body_file" -w '%{http_code}' -X "$method" "$url" 2>>"$LOG")"
    rc=$?
    if [ -s "$body_file" ]; then
        cat "$body_file" >>"$LOG"
        printf '\n' >>"$LOG"
    fi
    rm -f "$body_file"
    [ "$rc" = "0" ] || return "$rc"
    printf '%s\n' "$code"
    return 0
}

sanitize_summary_value() {
    printf '%s' "$1" | tr -d '\r\n"' | sed 's/[^A-Za-z0-9_.:-]/_/g'
}

extract_status_value() {
    raw="$1"
    value="$(printf '%s' "$raw" |
        sed -n 's/.*"status"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' |
        head -n 1)"
    if [ -z "$value" ]; then
        value="$(printf '%s' "$raw" |
            sed -n 's/.*status[[:space:]]*:[[:space:]]*\([A-Za-z_][A-Za-z0-9_-]*\).*/\1/p' |
            head -n 1)"
    fi
    if [ -z "$value" ]; then
        value="$(printf '%s' "$raw" | tr -d '\r\n" ' |
            sed -n '/^\(idle\|printing\|paused\|error\|offline\|finished\)$/p' |
            head -n 1)"
    fi
    sanitize_summary_value "$value"
}

http_get_capture() {
    path="$1"
    out="$2"
    url="${API_BASE}${path}"

    : >"$out" || return 1
    if command -v curl >/dev/null 2>&1; then
        curl -fsS -X GET "$url" >"$out" 2>>"$LOG"
        return $?
    fi
    if command -v wget >/dev/null 2>&1; then
        wget -q -O "$out" "$url" 2>>"$LOG"
        return $?
    fi
    say "SKIP: no curl/wget available for GET $url"
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

status_step() {
    label="$1"
    body_file="/tmp/deneb-printsvc-smoke-status.$$"

    say "$label: GET /printer/status"
    http_get_capture /printer/status "$body_file"
    rc=$?
    if [ -s "$body_file" ]; then
        cat "$body_file" >>"$LOG"
        printf '\n' >>"$LOG"
    fi
    raw_body="$(cat "$body_file" 2>/dev/null || true)"
    status_value="$(extract_status_value "$raw_body")"
    body_value="$(sanitize_summary_value "$raw_body")"
    rm -f "$body_file"
    say "$label rc=$rc status=${status_value:-unknown}"
    summary "phase=$label kind=api method=GET path=/printer/status rc=$rc status=${status_value:-unknown} body=${body_value:-unknown}"
    return "$rc"
}

printer_root_step() {
    label="$1"
    body_file="/tmp/deneb-printsvc-smoke-printer.$$"

    say "$label: GET /printer"
    http_get_capture /printer "$body_file"
    rc=$?
    if [ -s "$body_file" ]; then
        cat "$body_file" >>"$LOG"
        printf '\n' >>"$LOG"
    fi
    body_value="$(sanitize_summary_value "$(cat "$body_file" 2>/dev/null || true)")"
    rm -f "$body_file"
    say "$label rc=$rc body=${body_value:-unknown}"
    summary "phase=$label kind=api method=GET path=/printer rc=$rc body=${body_value:-unknown}"
    return "$rc"
}

route_step() {
    label="$1"
    body_file="/tmp/deneb-printsvc-smoke-route.$$"

    say "$label: GET /deneb/print_backend"
    http_get_capture /deneb/print_backend "$body_file"
    rc=$?
    if [ -s "$body_file" ]; then
        cat "$body_file" >>"$LOG"
        printf '\n' >>"$LOG"
    fi
    body_value="$(sanitize_summary_value "$(cat "$body_file" 2>/dev/null || true)")"
    rm -f "$body_file"
    say "$label rc=$rc body=${body_value:-unknown}"
    summary "phase=$label kind=api method=GET path=/deneb/print_backend rc=$rc body=${body_value:-unknown}"
    return "$rc"
}

monotonic_seconds() {
    awk '{printf("%d", $1)}' /proc/uptime 2>/dev/null || printf '0'
}

sample_cpu() {
    label="$1"
    awk -v label="$label" '
        /^cpu / {
            total=0;
            for (i=2; i<=NF; i++) total += $i;
            printf("sample=%s cpu_user_jiffies=%s cpu_nice_jiffies=%s cpu_system_jiffies=%s cpu_idle_jiffies=%s cpu_iowait_jiffies=%s cpu_irq_jiffies=%s cpu_softirq_jiffies=%s cpu_total_jiffies=%s\n",
                   label, $2, $3, $4, $5, $6, $7, $8, total);
            exit;
        }
    ' /proc/stat >>"$SUMMARY" 2>/dev/null || true
    awk -v label="$label" '
        {printf("sample=%s load1=%s load5=%s load15=%s runnable=%s last_pid=%s\n",
                label, $1, $2, $3, $4, $5)}
    ' /proc/loadavg >>"$SUMMARY" 2>/dev/null || true
}

restart_step() {
    label="$1"
    say "$label: restarting native print service route"
    rc=0
    /etc/init.d/deneb-printsvc restart >>"$LOG" 2>&1 || rc=$?
    if [ "$rc" = "0" ]; then
        /etc/init.d/deneb-api restart >>"$LOG" 2>&1 || rc=$?
    fi
    say "$label rc=$rc"
    summary "phase=$label kind=service-restart rc=$rc"
    return "$rc"
}

wait_for_job_inactive() {
    timeout="$1"
    elapsed=0
    interval=10

    while [ "$elapsed" -le "$timeout" ]; do
        code="$(http_status_code GET /print_job 2>/dev/null || true)"
        case "$code" in
            404)
                summary "phase=job-completion-wait elapsed=$elapsed rc=0"
                return 0
                ;;
            200|201|202)
                say "job still active after ${elapsed}s"
                ;;
            *)
                say "unexpected /print_job status while waiting: ${code:-none}"
                summary "phase=job-completion-wait elapsed=$elapsed rc=1"
                return 1
                ;;
        esac
        sleep "$interval"
        elapsed=$((elapsed + interval))
    done

    summary "phase=job-completion-wait elapsed=$elapsed rc=1"
    return 1
}

wait_for_boot_sync() {
    timeout="$1"
    elapsed=0
    interval=2
    start_uptime="$(monotonic_seconds)"

    while [ "$elapsed" -le "$timeout" ]; do
        route_file="/tmp/deneb-printsvc-smoke-route.$$"
        status_file="/tmp/deneb-printsvc-smoke-ready-status.$$"
        http_get_capture /deneb/print_backend "$route_file"
        route_rc=$?
        http_get_capture /printer/status "$status_file"
        status_rc=$?
        route_value="$(sanitize_summary_value "$(cat "$route_file" 2>/dev/null || true)")"
        raw_status="$(cat "$status_file" 2>/dev/null || true)"
        status_value="$(extract_status_value "$raw_status")"
        status_body_value="$(sanitize_summary_value "$raw_status")"

        if [ -s "$route_file" ]; then
            cat "$route_file" >>"$LOG"
            printf '\n' >>"$LOG"
        fi
        if [ -s "$status_file" ]; then
            cat "$status_file" >>"$LOG"
            printf '\n' >>"$LOG"
        fi
        rm -f "$route_file" "$status_file"

        if [ "$route_rc" = "0" ] && [ "$status_rc" = "0" ] && \
           printf '%s' "$route_value" | grep -q 'native_only_route:true' && \
           printf '%s' "$status_body_value" | grep -q 'native_only_route:true' && \
           [ -n "$status_value" ] && [ "$status_value" != "unknown" ]; then
            uptime_now="$(monotonic_seconds)"
            boot_elapsed=$((uptime_now - start_uptime))
            [ "$boot_elapsed" -ge 0 ] || boot_elapsed=0
            summary "phase=boot-sync-ready elapsed_seconds=$elapsed uptime_delta_seconds=$boot_elapsed route_body=${route_value:-unknown} status=$status_value status_body=${status_body_value:-unknown} rc=0"
            return 0
        fi

        sleep "$interval"
        elapsed=$((elapsed + interval))
    done

    summary "phase=boot-sync-ready elapsed_seconds=$elapsed uptime_delta_seconds=0 route_body=${route_value:-unknown} status=${status_value:-unknown} status_body=${status_body_value:-unknown} rc=1"
    return 1
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

assert_native_driver_process() {
    label="$1"
    deneb_running=0
    stock_running=0

    if ps w | grep -E '(^|[ /])deneb-printsvc([ ]|$)' |
       grep -v grep >/dev/null 2>&1; then
        deneb_running=1
    fi
    if ps w | grep 'print_service.py' | grep -v grep >/dev/null 2>&1; then
        stock_running=1
    fi

    if [ "$deneb_running" = "1" ] && [ "$stock_running" = "0" ]; then
        rc=0
    else
        rc=1
    fi

    say "$label: native process check deneb_printsvc=$deneb_running print_service_py=$stock_running rc=$rc"
    summary "phase=$label kind=process deneb_printsvc=$deneb_running print_service_py=$stock_running rc=$rc"
    return "$rc"
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
    append_cmd head -n 1 /proc/stat || true
    sample_cpu "$label"
    append_cmd cat /proc/loadavg || true
    append_cmd df -h || true
    append_cmd sh -c "ps w | grep -E 'deneb-printsvc|print_service.py|coordinator.py|deneb-api|deneb-ui' | grep -v grep" || true
    sample_processes "$label"
    route_step "route-$label" || true
    status_step "status-$label" || true
    printer_root_step "printer-$label" || true
}

parser_selftest_case() {
    label="$1"
    raw="$2"
    expected="$3"

    actual="$(extract_status_value "$raw")"
    body="$(sanitize_summary_value "$raw")"
    if [ "$actual" = "$expected" ]; then
        summary "phase=summary-parser-${label} status=$actual body=${body:-unknown} rc=0"
        return 0
    fi
    summary "phase=summary-parser-${label} expected=$expected actual=${actual:-unknown} body=${body:-unknown} rc=1"
    return 1
}

parser_selftest() {
    rc=0

    parser_selftest_case json '{"status":"idle","native_only_route":true}' idle || rc=1
    parser_selftest_case flat '{status:printing,native_only_route:true}' printing || rc=1
    parser_selftest_case scalar '"paused"' paused || rc=1
    parser_selftest_case spaced '{ "status" : "finished", "native_only_route" : true }' finished || rc=1
    if [ "$rc" = "0" ]; then
        say "summary parser selftest passed"
    else
        say "summary parser selftest failed"
    fi
    return "$rc"
}

if [ "$RUN_PARSER_SELFTEST" = "1" ]; then
    parser_selftest
    exit $?
fi

say "deneb-printsvc smoke started"
say "log=$LOG summary=$SUMMARY api=$API_BASE cluster_api=$CLUSTER_API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION macro=$RUN_MACRO local_job=$RUN_LOCAL_JOB job=$RUN_JOB cura_job=$RUN_CURA_JOB preheat_abort=$RUN_PREHEAT_ABORT complete_job=$RUN_COMPLETE_JOB pause_resume=$RUN_PAUSE_RESUME restart=$RUN_RESTART boot_sync=$RUN_BOOT_SYNC"
summary "start api=$API_BASE cluster_api=$CLUSTER_API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION macro=$RUN_MACRO local_job=$RUN_LOCAL_JOB job=$RUN_JOB cura_job=$RUN_CURA_JOB preheat_abort=$RUN_PREHEAT_ABORT complete_job=$RUN_COMPLETE_JOB pause_resume=$RUN_PAUSE_RESUME restart=$RUN_RESTART boot_sync=$RUN_BOOT_SYNC"

append_cmd /usr/bin/deneb-printsvc --smoke-test
summary "phase=printsvc-self-test rc=$?"
if [ "$RUN_BOOT_SYNC" = "1" ]; then
    wait_for_boot_sync "$READY_TIMEOUT"
    rc=$?
    [ "$rc" = "0" ] || exit "$rc"
fi
snapshot "initial"

if [ "$ENABLE_NATIVE" = "1" ]; then
    say "asserting native deneb-printsvc route"
    append_cmd /etc/init.d/printserver stop || true
    append_cmd /etc/init.d/deneb-printsvc restart || true
    append_cmd /etc/init.d/deneb-api restart || true
    sleep 3
    summary "phase=native-route-enabled previous=native-only"
    assert_native_driver_process "native-driver-process" || exit $?
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

if [ "$RUN_MACRO" = "1" ]; then
    api_step "macro-${MACRO_ACTION}" POST /printer/heads/0/position "{\"action\":\"$MACRO_ACTION\"}"
    sleep 5
    snapshot "macro"
fi

if [ "$RUN_RESTART" = "1" ]; then
    if [ "$ENABLE_NATIVE" != "1" ]; then
        say "ERROR: --restart requires --native so deneb-printsvc recovery is meaningful"
        exit 2
    fi
    restart_step "service-restart"
    sleep 5
    snapshot "service-restarted"
fi

if [ "$RUN_LOCAL_JOB" = "1" ]; then
    if [ "$ENABLE_NATIVE" != "1" ]; then
        say "ERROR: --local-job requires --native so local/USB acceptance proves native driver ownership"
        exit 2
    fi
    if [ ! -f "$LOCAL_JOB_PATH" ]; then
        say "ERROR: local-job path does not exist: $LOCAL_JOB_PATH"
        exit 1
    fi
    local_job_evidence="/tmp/deneb-printsvc-smoke-local-job.$$"
    say "$ /usr/bin/deneb-printsvc --local-job-smoke $LOCAL_JOB_PATH"
    /usr/bin/deneb-printsvc --local-job-smoke "$LOCAL_JOB_PATH" \
        >"$local_job_evidence" 2>>"$LOG"
    rc=$?
    if [ -s "$local_job_evidence" ]; then
        cat "$local_job_evidence" >>"$LOG"
        while IFS= read -r line; do
            [ -n "$line" ] && summary "$line"
        done <"$local_job_evidence"
    fi
    rm -f "$local_job_evidence"
    say "local-job-native rc=$rc"
    summary "phase=local-job-native kind=printsvc-cli path=$LOCAL_JOB_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    summary "phase=local-job-start path=$LOCAL_JOB_PATH source=USB rc=0"
    summary "phase=local-job-abort rc=0"
fi

if [ "$RUN_JOB" = "1" ] || [ "$RUN_CURA_JOB" = "1" ] || [ "$RUN_PREHEAT_ABORT" = "1" ] || [ "$RUN_COMPLETE_JOB" = "1" ]; then
    if ! command -v curl >/dev/null 2>&1; then
        say "ERROR: job upload requires curl for multipart/form-data"
        exit 1
    fi
fi

if [ "$RUN_CURA_JOB" = "1" ]; then
    if [ ! -f "$CURA_JOB_PATH" ]; then
        say "ERROR: cura-job path does not exist: $CURA_JOB_PATH"
        exit 1
    fi
fi

if [ "$RUN_PREHEAT_ABORT" = "1" ]; then
    if [ ! -f "$PREHEAT_ABORT_PATH" ]; then
        say "ERROR: preheat-abort path does not exist: $PREHEAT_ABORT_PATH"
        exit 1
    fi
fi

if [ "$RUN_JOB" = "1" ]; then
    if [ ! -f "$JOB_PATH" ]; then
        say "ERROR: job path does not exist: $JOB_PATH"
        exit 1
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
    if [ "$RUN_PAUSE_RESUME" = "1" ]; then
        api_step "job-pause" PUT /print_job/state '{"action":"pause"}'
        sleep 10
        snapshot "job-paused"
        api_step "job-resume" PUT /print_job/state '{"action":"resume"}'
        sleep 10
        snapshot "job-resumed"
    fi
    api_step "job-abort" PUT /print_job/state '{"action":"abort"}' || \
        api_step "job-stop" PUT /print_job/state '{"action":"stop"}' || true
    sleep 10
    snapshot "job-aborted"
fi

if [ "$RUN_PREHEAT_ABORT" = "1" ]; then
    say "preheat-abort-start: multipart upload $PREHEAT_ABORT_PATH"
    curl -fsS -F "file=@${PREHEAT_ABORT_PATH}" \
        -F "jobname=$(basename "$PREHEAT_ABORT_PATH")" \
        -F "owner=Deneb smoke preheat abort" "${API_BASE}/print_job" >>"$LOG" 2>&1
    rc=$?
    say "preheat-abort-start rc=$rc"
    summary "phase=preheat-abort-start kind=multipart path=$PREHEAT_ABORT_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 5
    snapshot "preheat-abort-active"
    api_step "preheat-abort" PUT /print_job/state '{"action":"abort"}' || \
        api_step "preheat-stop" PUT /print_job/state '{"action":"stop"}' || true
    sleep 10
    snapshot "preheat-aborted"
fi

if [ "$RUN_CURA_JOB" = "1" ]; then
    say "cura-job-start: cluster multipart upload $CURA_JOB_PATH"
    curl -fsS -F "file=@${CURA_JOB_PATH}" \
        -F "jobname=$(basename "$CURA_JOB_PATH")" \
        -F "owner=Cura" "${CLUSTER_API_BASE}/print_jobs" >>"$LOG" 2>&1
    rc=$?
    say "cura-job-start rc=$rc"
    summary "phase=cura-job-start kind=cluster-multipart path=$CURA_JOB_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 20
    snapshot "cura-job-running"
    say "cura-job-abort: cluster DELETE /print_jobs/current"
    curl -fsS -X DELETE "${CLUSTER_API_BASE}/print_jobs/current" >>"$LOG" 2>&1
    rc=$?
    say "cura-job-abort rc=$rc"
    summary "phase=cura-job-abort kind=cluster-api method=DELETE path=/print_jobs/current rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 10
    snapshot "cura-job-aborted"
fi

if [ "$RUN_COMPLETE_JOB" = "1" ]; then
    if [ ! -f "$COMPLETE_JOB_PATH" ]; then
        say "ERROR: complete-job path does not exist: $COMPLETE_JOB_PATH"
        exit 1
    fi
    complete_job_bytes="$(wc -c <"$COMPLETE_JOB_PATH" 2>/dev/null | awk '{print $1}')"
    complete_job_bytes="${complete_job_bytes:-0}"
    complete_job_start="$(monotonic_seconds)"
    say "complete-job-start: multipart upload $COMPLETE_JOB_PATH"
    curl -fsS -F "file=@${COMPLETE_JOB_PATH}" \
        -F "jobname=$(basename "$COMPLETE_JOB_PATH")" \
        -F "owner=Deneb smoke completion" "${API_BASE}/print_job" >>"$LOG" 2>&1
    rc=$?
    say "complete-job-start rc=$rc"
    summary "phase=complete-job-start kind=multipart path=$COMPLETE_JOB_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 20
    snapshot "complete-job-running"
    wait_for_job_inactive "$COMPLETION_TIMEOUT"
    rc=$?
    [ "$rc" = "0" ] || exit "$rc"
    complete_job_end="$(monotonic_seconds)"
    complete_job_elapsed=$((complete_job_end - complete_job_start))
    [ "$complete_job_elapsed" -gt 0 ] || complete_job_elapsed=1
    complete_job_bps=$((complete_job_bytes / complete_job_elapsed))
    summary "phase=job-throughput path=$COMPLETE_JOB_PATH bytes=$complete_job_bytes elapsed_seconds=$complete_job_elapsed bytes_per_second=$complete_job_bps rc=0"
    sleep 5
    snapshot "job-completed"
fi

snapshot "final"
summary "complete log=$LOG summary=$SUMMARY"
say "deneb-printsvc smoke complete"
