#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Device-side remaining-Python runtime inventory helper.
# Emits a Markdown summary of live Python processes, init ownership, and
# de-Python classification for the current Deneb install.
#
# Usage:
#   ./deneb-python-runtime-inventory [--summary PATH] [--checklist] [--help]

set -u

SCRIPT_NAME=$(basename "$0")
SUMMARY="${DENEB_PYTHON_RUNTIME_SUMMARY:-/tmp/deneb-python-runtime-inventory.md}"

usage() {
    cat >&2 <<EOF
Usage: ${SCRIPT_NAME} [options]

Options:
  --summary PATH   Markdown output path, default ${SUMMARY}
  --checklist      Print the capture checklist and exit
  -h, --help       Show this help

The helper is read-only. It samples /proc, init scripts, and Deneb-local API
status to classify remaining live Python runtime dependencies.
EOF
    exit 2
}

checklist() {
    cat <<'EOF'
Remaining Live Python Runtime Inventory Checklist
=================================================

Run on target hardware after a Deneb install and reboot:

[ ] Capture the generated Markdown inventory.
[ ] Confirm native Deneb services are running: deneb-ui, deneb-api,
    deneb-web/lighttpd, deneb-mdns, deneb-printsvc.
[ ] Confirm expected absent stock Python paths:
    - connector.py when Digital Factory is unpaired or native deneb-dfsvc owns
      active cloud use.
    - print_service.py when native deneb-printsvc owns the print backend.
    - stock menu executor.py after deneb-ui smoke and reboot.
    - compile_all/compileall after Deneb disables the stock compile_all init.
[ ] Classify each remaining Python process as:
    - stock dependency to keep until native owner exists,
    - stock startup work to disable/avoid launching,
    - Deneb-owned runtime artifact to remove,
    - Deneb-owned reference/tooling to quarantine,
    - unexpected regression requiring investigation.
[ ] Record RSS, VSZ, VmData, thread count, fd count, and private memory when
    available.
[ ] Do not mark a Python path gone from source/package inspection alone; require
    live process evidence or package/audit evidence.
EOF
    exit 0
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --summary)
            [ "$#" -ge 2 ] || usage
            SUMMARY="$2"
            shift 2
            ;;
        --checklist)
            checklist
            ;;
        -h|--help)
            usage
            ;;
        *)
            usage
            ;;
    esac
done

capture_datetime() {
    date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || printf '%s' "1970-01-01T00:00:00Z"
}

cmdline_for_pid() {
    tr '\0' ' ' < "/proc/$1/cmdline" 2>/dev/null | sed 's/[[:space:]]*$//'
}

status_field() {
    awk -v key="$2:" '$1 == key { print $2; found = 1 } END { if (!found) print "NA" }' \
        "/proc/$1/status" 2>/dev/null
}

fd_count() {
    if [ -d "/proc/$1/fd" ]; then
        ls "/proc/$1/fd" 2>/dev/null | wc -l | tr -d ' '
    else
        printf '%s' "NA"
    fi
}

private_kb() {
    if [ -r "/proc/$1/smaps" ]; then
        awk '/^Private_/ {s += $2} END { if (s == "") print "NA"; else print s }' \
            "/proc/$1/smaps" 2>/dev/null
    else
        printf '%s' "NA"
    fi
}

python_pids() {
    local pd pid cmd
    for pd in /proc/[0-9]*/cmdline; do
        [ -r "$pd" ] || continue
        cmd="$(tr '\0' ' ' < "$pd" 2>/dev/null || true)"
        case "$cmd" in
            *python*|*compileall*)
                pid="${pd#/proc/}"
                pid="${pid%/cmdline}"
                printf '%s\n' "$pid"
                ;;
        esac
    done
}

classification_for_cmd() {
    case "$1" in
        *"/home/cygnus/coordinator/coordinator.py"*)
            printf '%s' "stock coordinator fallback/service-start policy - retire or gate after source/package audit and workflow proof"
            ;;
        *"connector.py"*)
            printf '%s' "unexpected regression in Deneb-native DF path - investigate"
            ;;
        *"print_service.py"*)
            printf '%s' "unexpected regression in Deneb-native print-service path - investigate"
            ;;
        *"/home/cygnus/menu/executor.py"*|*" menu/executor.py"*)
            printf '%s' "stock UI fallback - should be absent after Deneb UI install"
            ;;
        *"compileall"*|*"compile_all"*)
            printf '%s' "stock startup work - should be disabled in Deneb mode"
            ;;
        *)
            printf '%s' "unclassified Python runtime - investigate and classify"
            ;;
    esac
}

service_state() {
    local service="$1"
    if [ ! -x "/etc/init.d/$service" ]; then
        printf '| `%s` | missing | missing |\n' "$service"
        return
    fi

    /etc/init.d/"$service" enabled >/dev/null 2>&1
    enabled_rc=$?
    /etc/init.d/"$service" running >/dev/null 2>&1
    running_rc=$?
    printf '| `%s` | rc %s | rc %s |\n' "$service" "$enabled_rc" "$running_rc"
}

deneb_status() {
    wget -qO- http://127.0.0.1/api/v1/printer/status 2>/dev/null || printf '%s' "unavailable"
}

tmp="${SUMMARY}.$$"
{
    printf '# Remaining Live Python Runtime Inventory\n\n'
    printf '- Captured: `%s`\n' "$(capture_datetime)"
    printf '- Printer status: `%s`\n\n' "$(deneb_status)"

    printf '## Init State Snapshot\n\n'
    printf '| Service | enabled rc | running rc |\n'
    printf '| --- | --- | --- |\n'
    service_state coordinator
    service_state printserver
    service_state menu
    service_state digitalfactory
    service_state compile_all
    printf '\n'

    printf '## Live Python Processes\n\n'
    printf '| PID | Command | Classification | VSZ KB | RSS KB | VmData KB | Threads | FD count | Private KB |\n'
    printf '| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n'

    found=0
    for pid in $(python_pids); do
        [ -d "/proc/$pid" ] || continue
        cmd="$(cmdline_for_pid "$pid")"
        [ -n "$cmd" ] || continue
        found=1
        class="$(classification_for_cmd "$cmd")"
        printf '| `%s` | `%s` | %s | %s | %s | %s | %s | %s | %s |\n' \
            "$pid" "$cmd" "$class" \
            "$(status_field "$pid" VmSize)" \
            "$(status_field "$pid" VmRSS)" \
            "$(status_field "$pid" VmData)" \
            "$(status_field "$pid" Threads)" \
            "$(fd_count "$pid")" \
            "$(private_kb "$pid")"
    done
    if [ "$found" -eq 0 ]; then
        printf '| none | none | no live Python process found | 0 | 0 | 0 | 0 | 0 | 0 |\n'
    fi

    printf '\n## Expected Deneb-Native Absences\n\n'
    printf '- `connector.py`: absent unless investigating a Digital Factory fallback regression.\n'
    printf '- `print_service.py`: absent when `deneb-printsvc` owns the print backend.\n'
    printf '- `/home/cygnus/menu/executor.py`: absent after native `deneb-ui` install and reboot.\n'
    printf '- `compile_all` / `python3 -m compileall`: absent after Deneb disables stock compile work.\n'
} > "$tmp"
mv "$tmp" "$SUMMARY"
cat "$SUMMARY"
