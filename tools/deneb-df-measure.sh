#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Device-side Digital Factory lifecycle measurement helper.
# Captures per-process resource samples for the native deneb-dfsvc Digital
# Factory connector across all DF states, and separately records any stock
# connector.py fallback process as a de-Python regression signal:
#   disabled, idle-not-running, pairing, connected/reconnecting, disconnect
#
# Usage:
#   ./deneb-df-measure.sh [--state STATE] [--summary PATH] [--log PATH] [--checklist]
#
# Options:
#   --state STATE    Intended DF state to measure (see checklist)
#   --summary PATH   Key=value summary output, default /tmp/deneb-df-measure.summary
#   --log PATH       Detailed log output, default /tmp/deneb-df-measure.log
#   --checklist      Print the measurement checklist and exit
#   --help           Show usage
#
# Environment variables:
#   DENEB_DF_MEASURE_WAIT   Seconds to settle before capture (default 5)

set -u

SCRIPT_NAME=$(basename "$0")
SUMMARY="${DENEB_DF_MEASURE_SUMMARY:-/tmp/deneb-df-measure.summary}"
LOG="${DENEB_DF_MEASURE_LOG:-/tmp/deneb-df-measure.log}"
WAIT="${DENEB_DF_MEASURE_WAIT:-5}"
STATE=""

usage() {
    cat >&2 <<EOF
Usage: ${SCRIPT_NAME} [options]

Options:
  --state STATE    Intended DF state label (disabled|idle-not-running|
                   pairing|connected|reconnecting|disconnect)
  --summary PATH   Key=value summary output, default ${SUMMARY}
  --log PATH       Detailed log output, default ${LOG}
  --checklist      Print the measurement checklist and exit
  -h, --help       Show this help

Measured per process:
  - VSZ (VmSize, kB)    - RSS (VmRSS, kB)
  - FD count             - Thread count
  - PID-owned TCP/UDP sockets
  - CPU jiffies (from /proc/pid/stat)
  - Digital Factory log bytes
  - Service enabled/running state
EOF
    exit 2
}

checklist() {
    cat <<'CHKLST'
==================================================================
Digital Factory Lifecycle Measurement Checklist
==================================================================

Each state below should be measured on target hardware. For each
state, record a full process sample AND a service-state snapshot.

Required States:
----------------

[ ] DISABLED
    - Prerequisite: /etc/init.d/digitalfactory is disabled at boot
      (uci get ultimaker.option.cluster_id is empty)
    - Verify: /etc/init.d/digitalfactory enabled -> rc != 0
    - Verify: no deneb-dfsvc or connector.py process is running
    - Record: system-wide process list, service status

[ ] IDLE-NOT-RUNNING
    - Prerequisite: /etc/init.d/digitalfactory is enabled but not
      started (stopped by installer or never started)
    - Verify: /etc/init.d/digitalfactory enabled -> rc 0
    - Verify: /etc/init.d/digitalfactory running -> rc != 0
    - Record: service status, no deneb-dfsvc or connector.py expected

[ ] PAIRING
    - Prerequisite: User has initiated pairing from DF screen
    - Trigger: Start digitalfactory, then run
      deneb-api digital-factory connect --timeout 120
    - Verify: deneb-dfsvc process is running and connector.py is absent
    - Record: process sample during pairing handshake, then again
      after 30s settle
    - Note: Requires cloud access and a valid pairing code

[ ] CONNECTED
    - Prerequisite: Pairing completed, cluster_id is set
    - Verify: deneb-api digital-factory status shows connected
    - Verify: deneb-dfsvc process is running long-term and connector.py is absent
    - Record: process sample after 60s settle in connected state
    - Record: log growth over 5 minutes in steady state

[ ] RECONNECTING
    - Prerequisite: Previously connected, network interrupted
    - Trigger: Disable WiFi or block cloud endpoint
    - Verify: deneb-dfsvc process remains running but socket
      shows CLOSE-WAIT or SYN-SENT
    - Record: process sample during reconnection backoff

[ ] DISCONNECT
    - Prerequisite: Currently connected or pairing
    - Trigger: Run deneb-api digital-factory disconnect or
      tap "Disconnect" on DF screen
    - Verify: deneb-api digital-factory status shows timeout
    - Record: process sample immediately after disconnect, again
      after 10s settle

Per-Sample Measurements (for each state):
------------------------------------------
For each capture, run the helper and record:

  timestamp, state_label,
  deneb-dfsvc: pid, vmsize_kb, vmrss_kb, fd_count, thread_count,
               cpu_jiffies, pid-owned sockets (tcp/udp),
               Digital Factory log bytes,
  connector.py fallback: same process fields if present; should be absent
                         in Deneb-native packages,
  digitalfactory service: enabled (y/n), running (y/n),
  deneb-api digital-factory status output

Required Output Format:
-----------------------
Each sample produces one line in the summary file:

  ISO8601 state=STATE pid=PID vmsize_kb=N vmrss_kb=N fd_count=N \
    thread_count=N cpu_jiffies=N tcp_sockets=N udp_sockets=N \
    df_enabled=0|1 df_running=0|1

Minimum Runs:
-------------
  - At least one run with deneb-dfsvc NOT running (disabled or
    idle-not-running state)
  - At least one supervised run where deneb-dfsvc IS started by
    the intended flow (pairing or connected state)
  - connector.py must remain absent in Deneb-native package runs; if it appears,
    treat that sample as a regression to investigate rather than proof of
    native Digital Factory lifecycle.

Memory Tool Requirements:
-------------------------
This helper reads /proc and uses standard POSIX tools only.
No Valgrind/sanitizer run is needed unless the helper is extended
with native (compiled) parsing code.
CHKLST
    exit 0
}

log() {
    printf '%s\n' "$*" | tee -a "$LOG"
}

capture_datetime() {
    date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || \
        printf '%s' "1970-01-01T00:00:00Z"
}

find_native_connector_pids() {
    pgrep -f '(^|/| )deneb-dfsvc([[:space:]]|$)' 2>/dev/null || true
}

# Locate stock connector.py process(es). Returns space-separated PIDs.
find_stock_connector_pids() {
    # Match stock Python connector.py only. A broad pgrep can match this helper
    # or the SSH shell command when the measurement command mentions connector.py.
    local pids=""
    local pd cmdline pid
    for pd in /proc/[0-9]*/cmdline; do
        cmdline="$(tr '\0' ' ' < "$pd" 2>/dev/null || true)"
        case "$cmdline" in
            *python*connector.py*|*python*connector.py[[:space:]]*)
                pid="${pd#/proc/}"
                pid="${pid%/cmdline}"
                pids="$pids $pid"
                ;;
        esac
    done
    echo "$pids" | tr -s ' ' | sed 's/^ //'
}

socket_inode_exists() {
    local inode="$1"
    shift
    local table

    for table in "$@"; do
        [ -r "$table" ] || continue
        awk -v inode="$inode" '
            NR > 1 && $10 == inode { found = 1 }
            END { exit found ? 0 : 1 }
        ' "$table" 2>/dev/null && return 0
    done

    return 1
}

count_pid_sockets() {
    local pid="$1"
    local proto="$2"
    local count=0
    local tables=""

    case "$proto" in
        tcp) tables="/proc/net/tcp /proc/net/tcp6" ;;
        udp) tables="/proc/net/udp /proc/net/udp6" ;;
        *) echo 0; return ;;
    esac

    [ -d "/proc/$pid/fd" ] || {
        echo 0
        return
    }

    for f in "/proc/$pid/fd"/*; do
        local link inode
        link="$(readlink "$f" 2>/dev/null || true)"
        case "$link" in
            socket:\[*\])
                inode="${link#socket:[}"
                inode="${inode%]}"
                if socket_inode_exists "$inode" $tables; then
                    count=$((count + 1))
                fi
                ;;
        esac
    done

    echo "$count"
}

# Capture a single process sample. Outputs key=value pairs.
capture_proc_sample() {
    local pid="$1"
    local state_label="$2"
    local role="${3:-process}"
    local ts
    ts="$(capture_datetime)"

    [ -z "$pid" ] && return 1
    [ ! -d "/proc/$pid" ] && {
        log "WARN: pid $pid no longer exists, skipping sample"
        return 1
    }

    # Basic identity
    local cmdline
    cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null | sed 's/ *$//')"
    [ -z "$cmdline" ] && cmdline="[unknown]"

    # Memory from /proc/pid/status
    local vmsize_kb=0 vmrss_kb=0 thread_count=0
    eval "$(awk '
        /^VmSize:/ { vmsize = $2 }
        /^VmRSS:/  { vmrss  = $2 }
        /^Threads:/{ threads = $2 }
        END { printf "vmsize_kb=%d vmrss_kb=%d thread_count=%d", vmsize, vmrss, threads }
    ' "/proc/$pid/status" 2>/dev/null)"

    # statm for shared pages
    local statm_shared_pages=0
    read -r _ _ statm_shared_pages _ _ _ _ < "/proc/$pid/statm" 2>/dev/null || true

    # FD count
    local fd_count=0
    fd_count="$(ls "/proc/$pid/fd" 2>/dev/null | wc -l | awk '{print $1}')"

    # CPU jiffies from /proc/pid/stat (fields 12-13 = utime+stime)
    local cpu_jiffies=0
    cpu_jiffies="$(awk '{print $12+$13}' "/proc/$pid/stat" 2>/dev/null || echo 0)"

    # PID-owned sockets, matched by fd socket inode against /proc/net.
    local tcp_sockets=0 udp_sockets=0
    tcp_sockets="$(count_pid_sockets "$pid" tcp)"
    udp_sockets="$(count_pid_sockets "$pid" udp)"

    # Output sample line
    printf '%s state=%s role=%s pid=%d vmsize_kb=%d vmrss_kb=%d statm_shared_pages=%d fd_count=%d thread_count=%d cpu_jiffies=%d tcp_sockets=%d udp_sockets=%d command="%s"\n' \
        "$ts" "$state_label" "$role" "$pid" "$vmsize_kb" "$vmrss_kb" \
        "$statm_shared_pages" "$fd_count" "$thread_count" \
        "$cpu_jiffies" "$tcp_sockets" "$udp_sockets" "$cmdline"
}

# Capture service state for the digitalfactory init script
capture_service_state() {
    local state_label="$1"
    local ts
    ts="$(capture_datetime)"

    local enabled=0 init_running=0 process_running=0
    /etc/init.d/digitalfactory enabled 2>/dev/null && enabled=1
    /etc/init.d/digitalfactory running 2>/dev/null && init_running=1

    # Some stock init scripts report a successful "running" command even when
    # no connector process exists. Treat process presence as the lifecycle truth
    # and keep the raw init return as a separate diagnostic.
    if [ -n "$(find_native_connector_pids)" ] || [ -n "$(find_stock_connector_pids)" ]; then
        process_running=1
    fi

    local bridge_status=""
    bridge_status="$(deneb-api digital-factory status --timeout 10 2>>"$LOG" || true)"

    printf '%s state=%s df_enabled=%d df_running=%d df_init_running=%d bridge_status="%s"\n' \
        "$ts" "$state_label" "$enabled" "$process_running" "$init_running" "$bridge_status"
}

# Main measurement capture
do_measure() {
    echo "=== Digital Factory Measurement: state=$STATE ===" >> "$LOG"
    log "State: $STATE"
    log "Waiting ${WAIT}s for settle..."
    sleep "$WAIT"

    # Clear summary for this run
    : > "$SUMMARY"

    # 1. Service state
    log "--- Service State ---"
    capture_service_state "$STATE" | tee -a "$SUMMARY" >> "$LOG"

    # 2. Find native deneb-dfsvc process(es)
    local native_pids stock_pids
    native_pids="$(find_native_connector_pids)"
    if [ -z "$native_pids" ]; then
        log "No deneb-dfsvc process found"
        printf '%s state=%s role=deneb-dfsvc pid=0 vmsize_kb=0 vmrss_kb=0 fd_count=0 thread_count=0 cpu_jiffies=0 cmdline="(not running)"\n' \
            "$(capture_datetime)" "$STATE" >> "$SUMMARY"
    else
        local n=0
        for pid in $native_pids; do
            n=$((n + 1))
            log "--- Native Connector Process Sample [$n]: pid=$pid ---"
            capture_proc_sample "$pid" "$STATE" "deneb-dfsvc" | tee -a "$SUMMARY" >> "$LOG"
        done
    fi

    # 3. Record stock connector.py fallback process(es), which should be absent
    # in Deneb-native package runs.
    stock_pids="$(find_stock_connector_pids)"
    if [ -z "$stock_pids" ]; then
        log "No stock connector.py fallback process found"
        printf '%s state=%s role=connector.py pid=0 vmsize_kb=0 vmrss_kb=0 fd_count=0 thread_count=0 cpu_jiffies=0 cmdline="(not running)"\n' \
            "$(capture_datetime)" "$STATE" >> "$SUMMARY"
    else
        local n=0
        for pid in $stock_pids; do
            n=$((n + 1))
            log "--- Stock Connector Fallback Process Sample [$n]: pid=$pid ---"
            capture_proc_sample "$pid" "$STATE" "connector.py" | tee -a "$SUMMARY" >> "$LOG"
        done
    fi

    # 4. System-wide socket summary (for context)
    log "--- Socket Summary ---"
    {
        printf 'Active TCP connections:\n'
        ss -tna 2>/dev/null || cat /proc/net/tcp 2>/dev/null || printf '(unavailable)\n'
        printf 'Active UDP endpoints:\n'
        ss -una 2>/dev/null || cat /proc/net/udp 2>/dev/null || printf '(unavailable)\n'
    } >> "$LOG"

    # 5. DF log size across the stock UltiMaker log and any rotations.
    log "--- Digital Factory Logs ---"
    local total_log_bytes=0 found_log=0
    local logfile size
    for logfile in /var/log/ultimaker/digitalfactory.log* /var/log/digitalfactory.log; do
        [ -f "$logfile" ] || continue
        found_log=1
        size="$(wc -c < "$logfile")"
        total_log_bytes=$((total_log_bytes + size))
        log "$logfile: $size bytes"
    done
    if [ "$found_log" -eq 1 ]; then
        printf '%s state=%s digitalfactory_log_bytes=%d\n' \
            "$(capture_datetime)" "$STATE" "$total_log_bytes" >> "$SUMMARY"
    else
        log "digitalfactory.log*: not found"
    fi

    log "=== Measurement complete for state=$STATE ==="
    echo "Summary written to $SUMMARY"
    echo "Log written to $LOG"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            ;;
        --checklist)
            checklist
            ;;
        --state)
            STATE="$2"
            shift 2
            ;;
        --state=*)
            STATE="${1#*=}"
            shift
            ;;
        --summary)
            SUMMARY="$2"
            shift 2
            ;;
        --log)
            LOG="$2"
            shift 2
            ;;
        *)
            usage
            ;;
    esac
done

[ -n "$STATE" ] || usage

do_measure
