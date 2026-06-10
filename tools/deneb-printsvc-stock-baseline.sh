#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Supervised stock-baseline collector for the native print-service migration.
# This script temporarily starts the stock printserver init path, runs the
# packaged smoke harness in --stock mode, and restores native deneb-printsvc.

set -u

SCRIPT_NAME=$(basename "$0")
SMOKE="${DENEB_PRINTSVC_SMOKE_BIN:-/usr/bin/deneb-printsvc-smoke}"
SUMMARY="${DENEB_PRINTSVC_STOCK_BASELINE_SUMMARY:-/tmp/deneb-printsvc-stock-baseline.summary}"
LOG="${DENEB_PRINTSVC_STOCK_BASELINE_LOG:-/tmp/deneb-printsvc-stock-baseline.log}"
ALLOW_STOCK_SWITCH="${DENEB_PRINTSVC_STOCK_BASELINE_OK:-0}"
RESTORE_NATIVE=1
WAIT_TIMEOUT="${DENEB_PRINTSVC_STOCK_BASELINE_WAIT:-90}"
NATIVE_WAS_ENABLED=0
NATIVE_GUARD_PID=""

usage() {
    cat >&2 <<EOF
Usage: ${SCRIPT_NAME} --allow-stock-switch [options] [-- smoke-options]

Options:
  --allow-stock-switch   Required acknowledgement. Stock startup runs the
                         stock motion-controller verification path.
  --summary PATH         Stock smoke summary output, default ${SUMMARY}
  --log PATH             Stock smoke log output, default ${LOG}
  --smoke PATH           Smoke harness path, default ${SMOKE}
  --wait-timeout SEC     Max seconds for service ownership changes, default 90
  --no-restore-native    Leave stock printserver active after the run
  -h, --help             Show this help

Extra smoke-options are passed to deneb-printsvc-smoke after --stock,
--summary, and --log. The helper rejects --native. Physical phases still
require the smoke harness --physical-ok and --physical-bundle-ok gates.
EOF
    exit 2
}

log() {
    printf '%s\n' "$*"
}

fail() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

is_stock_arg_safe() {
    case "$1" in
        --native)
            return 1
            ;;
    esac
    return 0
}

count_process() {
    kind=$1
    case "$kind" in
        native)
            ps w 2>/dev/null |
                awk '$5 == "/usr/bin/deneb-printsvc" { count++ } END { print count + 0 }'
            ;;
        stock)
            ps w 2>/dev/null |
                awk '$5 == "/usr/bin/python3" && $6 == "/home/cygnus/marlindriver/print_service.py" { count++ } END { print count + 0 }'
            ;;
        *)
            printf '0\n'
            ;;
    esac
}

kill_exact_stock_driver() {
    ps w 2>/dev/null | grep '/home/cygnus/marlindriver/print_service.py' |
        grep -v grep | while read -r pid rest; do
            case "$pid" in
                ''|*[!0-9]*) continue ;;
            esac
            kill "$pid" 2>/dev/null || true
            sleep 1
            kill -9 "$pid" 2>/dev/null || true
        done
    rm -f /var/run/printserver.pid
}

kill_exact_native_driver() {
    ps w 2>/dev/null |
        awk '$5 == "/usr/bin/deneb-printsvc" { print $1 }' |
        while read -r pid; do
            case "$pid" in
                ''|*[!0-9]*) continue ;;
            esac
            kill "$pid" 2>/dev/null || true
            kill -9 "$pid" 2>/dev/null || true
        done
}

stop_native_driver() {
    /etc/init.d/deneb-printsvc disable >/dev/null 2>&1 || true
    ubus call service delete '{"name":"deneb-printsvc"}' >/dev/null 2>&1 || true
    kill_exact_native_driver
    ubus call service delete '{"name":"deneb-printsvc"}' >/dev/null 2>&1 || true
}

start_native_guard() {
    (
        while :; do
            ubus call service delete '{"name":"deneb-printsvc"}' >/dev/null 2>&1 || true
            kill_exact_native_driver
            sleep 1
        done
    ) &
    NATIVE_GUARD_PID=$!
}

stop_native_guard() {
    if [ -n "$NATIVE_GUARD_PID" ]; then
        kill "$NATIVE_GUARD_PID" >/dev/null 2>&1 || true
        wait "$NATIVE_GUARD_PID" 2>/dev/null || true
        NATIVE_GUARD_PID=""
    fi
}

find_stock_init() {
    for candidate in \
        /home/deneb/backups/deneb-ui/init/printserver.orig \
        /rom/etc/init.d/printserver
    do
        if [ -x "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

wait_for_process_state() {
    label=$1
    stock_want=$2
    native_want=$3
    deadline=$(( $(date +%s) + WAIT_TIMEOUT ))

    while [ "$(date +%s)" -le "$deadline" ]; do
        stock_count=$(count_process stock)
        native_count=$(count_process native)
        if [ "$stock_count" = "$stock_want" ] && [ "$native_count" = "$native_want" ]; then
            log "PASS: ${label} stock=${stock_count} native=${native_count}"
            return 0
        fi
        sleep 2
    done

    stock_count=$(count_process stock)
    native_count=$(count_process native)
    fail "${label} timed out: stock=${stock_count} native=${native_count}"
}

restart_api_quietly() {
    /etc/init.d/deneb-api restart >/dev/null 2>&1 || true
}

wait_for_stock_api_ready() {
    label=$1
    deadline=$(( $(date +%s) + WAIT_TIMEOUT ))

    while [ "$(date +%s)" -le "$deadline" ]; do
        status="$(wget -qO- http://127.0.0.1/api/v1/printer/status 2>/dev/null || true)"
        printer="$(wget -qO- http://127.0.0.1/api/v1/printer 2>/dev/null || true)"
        case "$status:$printer" in
            *idle*'"bed":{"temperature":{"current":0.0'*|*idle*'"hotend":{"id":"0.4 mm","serial":"","temperature":{"current":0.0'*)
                ;;
            *idle*'"bed":{"temperature":{"current":'*'"hotend":{"id":"0.4 mm","serial":"","temperature":{"current":'*)
                log "PASS: ${label} stock API reports idle with nonzero temperature telemetry"
                return 0
                ;;
        esac
        sleep 2
    done

    fail "${label} timed out waiting for stock API idle/nonzero temperature telemetry"
}

restore_native() {
    [ "$RESTORE_NATIVE" = "1" ] || return 0
    stop_native_guard
    log "restoring native deneb-printsvc ownership"
    /etc/init.d/printserver stop >/dev/null 2>&1 || true
    if [ -n "${STOCK_INIT:-}" ]; then
        "$STOCK_INIT" stop >/dev/null 2>&1 || true
    fi
    kill_exact_stock_driver
    if [ "$NATIVE_WAS_ENABLED" = "1" ]; then
        /etc/init.d/deneb-printsvc enable >/dev/null 2>&1 || true
    fi
    /etc/init.d/deneb-printsvc restart >/dev/null 2>&1 || /etc/init.d/deneb-printsvc start >/dev/null 2>&1 || true
    restart_api_quietly
    wait_for_process_state "native-restored" 0 1
}

SMOKE_ARGS=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --allow-stock-switch)
            ALLOW_STOCK_SWITCH=1
            ;;
        --summary)
            shift
            [ "$#" -gt 0 ] || usage
            SUMMARY="$1"
            ;;
        --log)
            shift
            [ "$#" -gt 0 ] || usage
            LOG="$1"
            ;;
        --smoke)
            shift
            [ "$#" -gt 0 ] || usage
            SMOKE="$1"
            ;;
        --wait-timeout)
            shift
            [ "$#" -gt 0 ] || usage
            case "$1" in
                ''|*[!0-9]*) fail "invalid wait timeout: $1" ;;
            esac
            WAIT_TIMEOUT="$1"
            ;;
        --no-restore-native)
            RESTORE_NATIVE=0
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                is_stock_arg_safe "$1" || fail "--native is not valid for stock baseline collection"
                SMOKE_ARGS="${SMOKE_ARGS} $(printf '%s\n' "$1" | sed "s/'/'\\\\''/g; s/.*/'&'/")"
                shift
            done
            break
            ;;
        -h|--help)
            usage
            ;;
        *)
            is_stock_arg_safe "$1" || fail "--native is not valid for stock baseline collection"
            SMOKE_ARGS="${SMOKE_ARGS} $(printf '%s\n' "$1" | sed "s/'/'\\\\''/g; s/.*/'&'/")"
            ;;
    esac
    shift
done

[ "$ALLOW_STOCK_SWITCH" = "1" ] ||
    fail "refusing stock service switch without --allow-stock-switch or DENEB_PRINTSVC_STOCK_BASELINE_OK=1"
[ -x "$SMOKE" ] || fail "smoke harness not executable: $SMOKE"

STOCK_INIT=$(find_stock_init) ||
    fail "stock printserver init not found; expected backup or /rom/etc/init.d/printserver"

trap restore_native EXIT HUP INT TERM

log "switching to stock printserver using ${STOCK_INIT}"
if /etc/init.d/deneb-printsvc enabled >/dev/null 2>&1; then
    NATIVE_WAS_ENABLED=1
fi
stop_native_driver
wait_for_process_state "native-stopped" 0 0
kill_exact_stock_driver
"$STOCK_INIT" start >/tmp/deneb-printsvc-stock-baseline-start.log 2>&1 ||
    fail "stock printserver start failed: ${STOCK_INIT}"
restart_api_quietly
stop_native_driver
wait_for_process_state "stock-active" 1 0
wait_for_stock_api_ready "stock-api-ready"

start_native_guard
cmd="'$SMOKE' --stock --summary '$SUMMARY' --log '$LOG'${SMOKE_ARGS}"
log "running stock smoke baseline"
sh -c "$cmd"
rc=$?
stop_native_guard
if [ "$rc" -ne 0 ]; then
    fail "stock smoke baseline failed with rc=${rc}"
fi

log "stock baseline summary: ${SUMMARY}"
exit 0
