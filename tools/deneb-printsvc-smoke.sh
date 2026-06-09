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
RUN_ACTIVE_ABORT=0
RUN_COMPLETE_JOB=0
RUN_MACRO=0
RUN_PAUSE_RESUME=0
RUN_RESTART=0
RUN_BOOT_SYNC=0
RUN_PARSER_SELFTEST=0
MAKE_COMPLETE_FIXTURE=0
PHYSICAL_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_OK:-0}"
PHYSICAL_BUNDLE_OK="${DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK:-0}"
JOB_PATH=""
LOCAL_JOB_PATH=""
CURA_JOB_PATH=""
PREHEAT_ABORT_PATH=""
ACTIVE_ABORT_PATH=""
COMPLETE_JOB_PATH=""
MAKE_COMPLETE_FIXTURE_PATH=""
MAKE_COMPLETE_FIXTURE_CYCLES=80
MACRO_ACTION=""
COMPLETION_TIMEOUT="${DENEB_PRINTSVC_SMOKE_COMPLETION_TIMEOUT:-300}"
READY_TIMEOUT="${DENEB_PRINTSVC_SMOKE_READY_TIMEOUT:-180}"
ACTIVE_ABORT_DELAY="${DENEB_PRINTSVC_SMOKE_ACTIVE_ABORT_DELAY:-60}"
PREHOME_ACTION="${DENEB_PRINTSVC_SMOKE_PREHOME_ACTION:-z_home}"

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
  --active-abort PATH   Start PATH, wait for active printing evidence, then abort
  --active-abort-delay SEC
                        Delay before --active-abort snapshot, default 60
  --pause-resume        During --job, pause and resume before aborting
  --complete-job PATH   Start PATH and wait for it to leave the active job API
  --completion-timeout SEC
                        Max seconds to wait for --complete-job, default 300
  --make-complete-fixture PATH [CYCLES]
                        Write a bounded old-Marlin Z-move completion fixture and exit
  --restart             Restart deneb-printsvc and deneb-api, then sample recovery
  --boot-sync           Wait for print backend route/status readiness evidence
  --summary-parser-selftest
                        Exercise summary status/body parsing without hardware
  --physical-ok        Required for any phase that heats, homes, moves axes,
                        starts a print job, or sends a macro action
  --physical-bundle-ok Required in addition to --physical-ok when more than
                        one physical phase is requested in a single run
  --prehome-action ACTION
                        Motion guard before macro/job phases, default z_home
  --ready-timeout SEC   Max seconds for --boot-sync, default 180
  --log PATH            Write the smoke log to PATH
  --summary PATH        Write compact evidence summary to PATH
  --api URL             Override API base, default http://127.0.0.1/api/v1
  --cluster-api URL     Override cluster API base, default http://127.0.0.1/cluster-api/v1
  -h, --help            Show this help

The heat, motion, macro, job, and complete-job phases move or heat hardware. Run
them only with the printer supervised, clear of obstructions, and prepared for
immediate power-off. These phases fail closed unless --physical-ok or
DENEB_PRINTSVC_SMOKE_PHYSICAL_OK=1 is set. More than one physical phase in a
single invocation also requires --physical-bundle-ok or
DENEB_PRINTSVC_SMOKE_PHYSICAL_BUNDLE_OK=1; prefer separate narrow checks.
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
        --active-abort)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --active-abort" >&2; exit 2; }
            RUN_ACTIVE_ABORT=1
            ACTIVE_ABORT_PATH="$1"
            ;;
        --active-abort-delay)
            shift
            [ "$#" -gt 0 ] || { echo "missing seconds after --active-abort-delay" >&2; exit 2; }
            case "$1" in
                ''|*[!0-9]*) echo "invalid active abort delay: $1" >&2; exit 2 ;;
            esac
            ACTIVE_ABORT_DELAY="$1"
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
        --make-complete-fixture)
            shift
            [ "$#" -gt 0 ] || { echo "missing path after --make-complete-fixture" >&2; exit 2; }
            MAKE_COMPLETE_FIXTURE=1
            MAKE_COMPLETE_FIXTURE_PATH="$1"
            if [ "$#" -gt 1 ] && printf '%s' "$2" | grep -Eq '^[0-9]+$'; then
                shift
                MAKE_COMPLETE_FIXTURE_CYCLES="$1"
            fi
            ;;
        --restart) RUN_RESTART=1 ;;
        --boot-sync) RUN_BOOT_SYNC=1 ;;
        --summary-parser-selftest) RUN_PARSER_SELFTEST=1 ;;
        --physical-ok) PHYSICAL_OK=1 ;;
        --physical-bundle-ok) PHYSICAL_BUNDLE_OK=1 ;;
        --prehome-action)
            shift
            [ "$#" -gt 0 ] || { echo "missing action after --prehome-action" >&2; exit 2; }
            PREHOME_ACTION="$1"
            case "$PREHOME_ACTION" in
                z_home|home) ;;
                *) echo "invalid --prehome-action: $PREHOME_ACTION" >&2; exit 2 ;;
            esac
            ;;
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

write_complete_fixture() {
    path="$1"
    cycle_count="$2"

    case "$cycle_count" in
        ''|*[!0-9]*) echo "invalid fixture cycle count: $cycle_count" >&2; return 2 ;;
    esac
    if [ "$cycle_count" -lt 1 ]; then
        echo "fixture cycle count must be greater than zero" >&2
        return 2
    fi
    if [ "$cycle_count" -gt 480 ]; then
        echo "fixture cycle count must be 480 or lower to keep Z travel bounded" >&2
        return 2
    fi

    mkdir -p "$(dirname "$path")" 2>/dev/null || true
    {
        printf '; Deneb native print-service completion smoke fixture\n'
        printf '; Old-Marlin-safe, no heat, no extrusion, no dwell.\n'
        printf '; Homes Z once to max, then performs bounded relative moves away from max.\n'
        printf 'G28 Z\n'
        printf 'G91\n'
        i=1
        while [ "$i" -le "$cycle_count" ]; do
            printf 'G1 Z-0.20 F30\n'
            i=$((i + 1))
        done
        printf 'G90\n'
        printf 'M114\n'
    } >"$path" || return 1
    return 0
}

if [ "$MAKE_COMPLETE_FIXTURE" = "1" ]; then
    write_complete_fixture "$MAKE_COMPLETE_FIXTURE_PATH" "$MAKE_COMPLETE_FIXTURE_CYCLES"
    rc=$?
    if [ "$rc" != "0" ]; then
        summary "phase=make-complete-fixture path=$MAKE_COMPLETE_FIXTURE_PATH cycles=$MAKE_COMPLETE_FIXTURE_CYCLES rc=$rc"
        exit "$rc"
    fi
    summary "phase=make-complete-fixture path=$MAKE_COMPLETE_FIXTURE_PATH cycles=$MAKE_COMPLETE_FIXTURE_CYCLES rc=0 command=G1_Z"
    say "wrote complete-job fixture: $MAKE_COMPLETE_FIXTURE_PATH cycles=$MAKE_COMPLETE_FIXTURE_CYCLES"
    exit 0
fi

physical_phase_requested() {
    [ "$RUN_HEAT" = "1" ] ||
    [ "$RUN_MOTION" = "1" ] ||
    [ "$RUN_MACRO" = "1" ] ||
    [ "$RUN_LOCAL_JOB" = "1" ] ||
    [ "$RUN_JOB" = "1" ] ||
    [ "$RUN_CURA_JOB" = "1" ] ||
    [ "$RUN_PREHEAT_ABORT" = "1" ] ||
    [ "$RUN_ACTIVE_ABORT" = "1" ] ||
    [ "$RUN_COMPLETE_JOB" = "1" ]
}

physical_phase_count() {
    count=0
    [ "$RUN_HEAT" = "1" ] && count=$((count + 1))
    [ "$RUN_MOTION" = "1" ] && count=$((count + 1))
    [ "$RUN_MACRO" = "1" ] && count=$((count + 1))
    [ "$RUN_LOCAL_JOB" = "1" ] && count=$((count + 1))
    [ "$RUN_JOB" = "1" ] && count=$((count + 1))
    [ "$RUN_CURA_JOB" = "1" ] && count=$((count + 1))
    [ "$RUN_PREHEAT_ABORT" = "1" ] && count=$((count + 1))
    [ "$RUN_ACTIVE_ABORT" = "1" ] && count=$((count + 1))
    [ "$RUN_COMPLETE_JOB" = "1" ] && count=$((count + 1))
    printf '%s\n' "$count"
}

if physical_phase_requested && [ "$PHYSICAL_OK" != "1" ]; then
    say "ERROR: physical printer phase requested without --physical-ok"
    say "Refusing to heat, home, move axes, start jobs, or run macros without an explicit supervised-machine acknowledgement."
    summary "phase=physical-safety-gate rc=2 reason=missing_physical_ok"
    exit 2
fi

PHYSICAL_PHASE_COUNT="$(physical_phase_count)"
if [ "$PHYSICAL_PHASE_COUNT" -gt 1 ] && [ "$PHYSICAL_BUNDLE_OK" != "1" ]; then
    say "ERROR: multiple physical printer phases requested without --physical-bundle-ok"
    say "Refusing bundled physical validation by default; split the run into supervised single-purpose checks."
    summary "phase=physical-bundle-safety-gate rc=2 reason=missing_physical_bundle_ok count=$PHYSICAL_PHASE_COUNT"
    exit 2
fi

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

    if command -v wget >/dev/null 2>&1; then
        if [ "$method" = "GET" ]; then
            wget -q -O - "$url" >>"$LOG" 2>&1
        elif [ -n "$body" ]; then
            wget -q -O - --method="$method" \
                --header='Content-Type: application/json' \
                --body-data="$body" "$url" >>"$LOG" 2>&1
        else
            wget -q -O - --method="$method" "$url" >>"$LOG" 2>&1
        fi
        return $?
    fi

    say "ERROR: no curl/wget available for $method $url"
    return 1
}

http_status_code() {
    method="$1"
    path="$2"
    url="${API_BASE}${path}"
    body_file="/tmp/deneb-printsvc-smoke-http.$$"

    if command -v curl >/dev/null 2>&1; then
        code="$(curl -sS -o "$body_file" -w '%{http_code}' -X "$method" "$url" 2>>"$LOG")"
        rc=$?
    elif command -v wget >/dev/null 2>&1; then
        header_file="/tmp/deneb-printsvc-smoke-http-headers.$$"
        wget -S -q -O "$body_file" --method="$method" "$url" \
            2>"$header_file"
        rc=$?
        code="$(awk '/^  HTTP\// { code=$2 } END { print code }' "$header_file" 2>/dev/null)"
        cat "$header_file" >>"$LOG" 2>/dev/null || true
        rm -f "$header_file"
        [ -n "$code" ] && rc=0
    else
        say "ERROR: no curl/wget available for status-code check $method $url"
        return 2
    fi
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

multipart_post() {
    url="$1"
    file_path="$2"
    job_name="$3"
    owner="$4"

    if command -v curl >/dev/null 2>&1; then
        curl -fsS -F "file=@${file_path}" -F "jobname=${job_name}" \
            -F "owner=${owner}" "$url" >>"$LOG" 2>&1
        return $?
    fi

    if command -v wget >/dev/null 2>&1; then
        boundary="----deneb-smoke-$$-$(date +%s 2>/dev/null || echo 0)"
        body_file="/tmp/deneb-printsvc-smoke-multipart.$$"
        {
            printf -- '--%s\r\n' "$boundary"
            printf 'Content-Disposition: form-data; name="file"; filename="%s"\r\n' "$(basename "$file_path")"
            printf 'Content-Type: application/octet-stream\r\n\r\n'
            cat "$file_path"
            printf '\r\n--%s\r\n' "$boundary"
            printf 'Content-Disposition: form-data; name="jobname"\r\n\r\n%s\r\n' "$job_name"
            printf -- '--%s\r\n' "$boundary"
            printf 'Content-Disposition: form-data; name="owner"\r\n\r\n%s\r\n' "$owner"
            printf -- '--%s--\r\n' "$boundary"
        } >"$body_file" || return 1
        wget -q -O - \
            --header="Content-Type: multipart/form-data; boundary=${boundary}" \
            --post-file="$body_file" "$url" >>"$LOG" 2>&1
        rc=$?
        rm -f "$body_file"
        return "$rc"
    fi

    say "ERROR: no curl/wget available for multipart upload $url"
    return 1
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

physical_safety_plan() {
    label="$1"
    axes="$2"
    required_home="$3"
    travel="$4"
    stop_conditions="$5"

    say "$label: physical safety axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions"
    summary "phase=${label}-safety kind=physical axes=$axes required_home=$required_home travel=$travel stop_conditions=$stop_conditions rc=0"
}

macro_safety_plan() {
    case "$MACRO_ACTION" in
        home)
            physical_safety_plan "macro" "XYZ" "XYZ" "stock_home_macro" "endstop_or_unexpected_motion"
            ;;
        bed_up)
            physical_safety_plan "macro" "Z" "Z" "stock_bed_up_macro" "endstop_or_unexpected_motion"
            ;;
        bed_down)
            physical_safety_plan "macro" "Z" "Z" "stock_bed_down_macro" "endstop_or_unexpected_motion"
            ;;
    esac
}

guarded_prehome() {
    label="$1"

    case "$PREHOME_ACTION" in
        z_home|home) ;;
        *)
            say "ERROR: invalid prehome action: $PREHOME_ACTION"
            summary "phase=${label} action=${PREHOME_ACTION} rc=2 reason=invalid_prehome_action"
            return 2
            ;;
    esac

    say "$label: guarded $PREHOME_ACTION before physical phase"
    summary "phase=${label} action=${PREHOME_ACTION} rc=pending reason=pre_physical_home"
    api_step "$label" POST /printer/heads/0/position "{\"action\":\"$PREHOME_ACTION\"}"
    rc=$?
    summary "phase=${label} action=${PREHOME_ACTION} rc=$rc reason=pre_physical_home"
    [ "$rc" = "0" ] || return "$rc"
    sleep 5
    snapshot "$label"
    return 0
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
    status_file="/tmp/deneb-printsvc-smoke-complete-status.$$"
    printer_file="/tmp/deneb-printsvc-smoke-complete-printer.$$"

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

    http_get_capture /printer/status "$status_file"
    status_rc=$?
    status_raw="$(cat "$status_file" 2>/dev/null || true)"
    status_value="$(extract_status_value "$status_raw")"
    status_body="$(sanitize_summary_value "$status_raw")"
    http_get_capture /printer "$printer_file"
    printer_rc=$?
    printer_body="$(sanitize_summary_value "$(cat "$printer_file" 2>/dev/null || true)")"
    rm -f "$status_file" "$printer_file"

    say "job completion timeout after ${elapsed}s status=${status_value:-unknown}"
    summary "phase=job-completion-wait elapsed=$elapsed rc=1 status=${status_value:-unknown} status_rc=$status_rc status_body=${status_body:-unknown} printer_rc=$printer_rc printer_body=${printer_body:-unknown}"
    api_step "completion-timeout-abort" PUT /print_job/state '{"action":"abort"}' || true
    return 1
}

completion_fixture_has_progress_command() {
    path="$1"

    awk '
        {
            sub(/\r$/, "");
            sub(/[;].*$/, "");
            gsub(/^[[:space:]]+|[[:space:]]+$/, "");
            if ($0 == "")
                next;
            split($0, fields, /[[:space:]]+/);
            cmd = toupper(fields[1]);
            if (cmd == "G4" || cmd == "M400")
                next;
            found = 1;
            exit;
        }
        END { exit found ? 0 : 1; }
    ' "$path"
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
            command_exe="${command%% *}"
            case "$command_exe" in
                /usr/bin/deneb-printsvc|/usr/bin/deneb-api|/usr/bin/deneb-ui|/usr/bin/deneb-mdns)
                    ;;
                /usr/bin/python*|python*)
                    case "$command" in
                        *'/home/cygnus/marlindriver/print_service.py'*|*'/home/cygnus/marlindriver/coordinator.py'*)
                            ;;
                        *)
                            continue
                            ;;
                    esac
                    ;;
                *)
                    continue
                    ;;
            esac
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
    snapshot_label="$1"
    say "===== snapshot: $snapshot_label ====="
    summary "snapshot=$snapshot_label"
    append_cmd cat /proc/uptime || true
    awk -v label="$snapshot_label" '{printf("sample=%s uptime_seconds=%s idle_seconds=%s\n", label, $1, $2)}' \
        /proc/uptime >>"$SUMMARY" 2>/dev/null || true
    append_cmd free || true
    free | awk -v label="$snapshot_label" '
        /^Mem:/ {printf("sample=%s mem_total_kb=%s mem_used_kb=%s mem_free_kb=%s\n", label, $2, $3, $4)}
        /^Swap:/ {printf("sample=%s swap_total_kb=%s swap_used_kb=%s swap_free_kb=%s\n", label, $2, $3, $4)}
    ' >>"$SUMMARY" 2>/dev/null || true
    append_cmd head -n 1 /proc/stat || true
    sample_cpu "$snapshot_label"
    append_cmd cat /proc/loadavg || true
    append_cmd df -h || true
    append_cmd sh -c "ps w | grep -E 'deneb-printsvc|print_service.py|coordinator.py|deneb-api|deneb-ui' | grep -v grep" || true
    sample_processes "$snapshot_label"
    route_step "route-$snapshot_label" || true
    status_step "status-$snapshot_label" || true
    printer_root_step "printer-$snapshot_label" || true
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
say "log=$LOG summary=$SUMMARY api=$API_BASE cluster_api=$CLUSTER_API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION macro=$RUN_MACRO local_job=$RUN_LOCAL_JOB job=$RUN_JOB cura_job=$RUN_CURA_JOB preheat_abort=$RUN_PREHEAT_ABORT active_abort=$RUN_ACTIVE_ABORT active_abort_delay=$ACTIVE_ABORT_DELAY complete_job=$RUN_COMPLETE_JOB pause_resume=$RUN_PAUSE_RESUME restart=$RUN_RESTART boot_sync=$RUN_BOOT_SYNC"
summary "start api=$API_BASE cluster_api=$CLUSTER_API_BASE native=$ENABLE_NATIVE heat=$RUN_HEAT motion=$RUN_MOTION macro=$RUN_MACRO local_job=$RUN_LOCAL_JOB job=$RUN_JOB cura_job=$RUN_CURA_JOB preheat_abort=$RUN_PREHEAT_ABORT active_abort=$RUN_ACTIVE_ABORT active_abort_delay=$ACTIVE_ABORT_DELAY complete_job=$RUN_COMPLETE_JOB pause_resume=$RUN_PAUSE_RESUME restart=$RUN_RESTART boot_sync=$RUN_BOOT_SYNC"

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
    physical_safety_plan "heat" "none" "none" "bed_to_40C_nozzle_to_50C_then_cooldown" "temperature_sensor_fault_or_runaway"
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
    physical_safety_plan "motion" "Z" "Z" "z_home_to_max_only" "z_endstop_or_unexpected_direction"
    api_step "z-home" POST /printer/heads/0/position '{"action":"z_home"}'
    sleep 5
    snapshot "motion"
fi

if [ "$RUN_MACRO" = "1" ]; then
    macro_safety_plan
    guarded_prehome "macro-prehome" || exit $?
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
    physical_safety_plan "local-job" "job_defined" "$PREHOME_ACTION" "user_supplied_gcode" "endstop_temperature_or_unexpected_motion"
    guarded_prehome "local-job-prehome" || exit $?
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

if [ "$RUN_JOB" = "1" ] || [ "$RUN_CURA_JOB" = "1" ] || [ "$RUN_PREHEAT_ABORT" = "1" ] || [ "$RUN_ACTIVE_ABORT" = "1" ] || [ "$RUN_COMPLETE_JOB" = "1" ]; then
    if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
        say "ERROR: job upload requires curl or wget for multipart/form-data"
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

if [ "$RUN_ACTIVE_ABORT" = "1" ]; then
    if [ ! -f "$ACTIVE_ABORT_PATH" ]; then
        say "ERROR: active-abort path does not exist: $ACTIVE_ABORT_PATH"
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
    physical_safety_plan "job" "job_defined" "$PREHOME_ACTION" "user_supplied_gcode_until_abort" "endstop_temperature_or_unexpected_motion"
    guarded_prehome "job-prehome" || exit $?
    say "job-start: multipart upload $JOB_PATH"
    multipart_post "${API_BASE}/print_job" "$JOB_PATH" \
        "$(basename "$JOB_PATH")" "Deneb smoke"
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
    physical_safety_plan "preheat-abort" "job_defined" "$PREHOME_ACTION" "preheat_until_abort" "temperature_sensor_fault_or_unexpected_motion"
    guarded_prehome "preheat-abort-prehome" || exit $?
    say "preheat-abort-start: multipart upload $PREHEAT_ABORT_PATH"
    multipart_post "${API_BASE}/print_job" "$PREHEAT_ABORT_PATH" \
        "$(basename "$PREHEAT_ABORT_PATH")" "Deneb smoke preheat abort"
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

if [ "$RUN_ACTIVE_ABORT" = "1" ]; then
    physical_safety_plan "active-abort" "job_defined" "$PREHOME_ACTION" "user_supplied_gcode_until_active_abort" "endstop_temperature_or_unexpected_motion"
    guarded_prehome "active-abort-prehome" || exit $?
    say "active-abort-start: multipart upload $ACTIVE_ABORT_PATH"
    multipart_post "${API_BASE}/print_job" "$ACTIVE_ABORT_PATH" \
        "$(basename "$ACTIVE_ABORT_PATH")" "Deneb smoke active abort"
    rc=$?
    say "active-abort-start rc=$rc"
    summary "phase=active-abort-start kind=multipart path=$ACTIVE_ABORT_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep "$ACTIVE_ABORT_DELAY"
    snapshot "active-abort-printing"
    api_step "active-abort" PUT /print_job/state '{"action":"abort"}' || \
        api_step "active-stop" PUT /print_job/state '{"action":"stop"}' || true
    sleep 10
    snapshot "active-aborted"
fi

if [ "$RUN_CURA_JOB" = "1" ]; then
    physical_safety_plan "cura-job" "job_defined" "$PREHOME_ACTION" "cura_uploaded_gcode_until_abort" "endstop_temperature_or_unexpected_motion"
    guarded_prehome "cura-job-prehome" || exit $?
    say "cura-job-start: cluster multipart upload $CURA_JOB_PATH"
    multipart_post "${CLUSTER_API_BASE}/print_jobs" "$CURA_JOB_PATH" \
        "$(basename "$CURA_JOB_PATH")" "Cura"
    rc=$?
    say "cura-job-start rc=$rc"
    summary "phase=cura-job-start kind=cluster-multipart path=$CURA_JOB_PATH rc=$rc"
    [ "$rc" = "0" ] || exit "$rc"
    sleep 20
    snapshot "cura-job-running"
    say "cura-job-abort: cluster DELETE /print_jobs/current"
    if command -v curl >/dev/null 2>&1; then
        curl -fsS -X DELETE "${CLUSTER_API_BASE}/print_jobs/current" >>"$LOG" 2>&1
    else
        wget -q -O - --method=DELETE "${CLUSTER_API_BASE}/print_jobs/current" >>"$LOG" 2>&1
    fi
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
    if ! completion_fixture_has_progress_command "$COMPLETE_JOB_PATH"; then
        say "ERROR: complete-job fixture must not be dwell/M400-only: $COMPLETE_JOB_PATH"
        summary "phase=complete-job-fixture-check path=$COMPLETE_JOB_PATH rc=1 reason=dwell_only"
        exit 1
    fi
    summary "phase=complete-job-fixture-check path=$COMPLETE_JOB_PATH rc=0 reason=progress_command"
    physical_safety_plan "complete-job" "Z" "$PREHOME_ACTION" "bounded_relative_Z_negative_max_96mm" "z_endstop_or_unexpected_direction"
    guarded_prehome "complete-job-prehome" || exit $?
    complete_job_bytes="$(wc -c <"$COMPLETE_JOB_PATH" 2>/dev/null | awk '{print $1}')"
    complete_job_bytes="${complete_job_bytes:-0}"
    complete_job_start="$(monotonic_seconds)"
    say "complete-job-start: multipart upload $COMPLETE_JOB_PATH"
    multipart_post "${API_BASE}/print_job" "$COMPLETE_JOB_PATH" \
        "$(basename "$COMPLETE_JOB_PATH")" "Deneb smoke completion"
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
