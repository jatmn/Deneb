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
INIT_DIR="${DENEB_RUNTIME_INVENTORY_INIT_DIR:-/etc/init.d}"
PROC_DIR="${DENEB_RUNTIME_INVENTORY_PROC_DIR:-/proc}"

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

status_field() {
    awk -v key="$2:" '$1 == key { print $2; found = 1 } END { if (!found) print "NA" }' \
        "$PROC_DIR/$1/status" 2>/dev/null
}

fd_count() {
    if [ -d "$PROC_DIR/$1/fd" ]; then
        ls "$PROC_DIR/$1/fd" 2>/dev/null | wc -l | tr -d ' '
    else
        printf '%s' "NA"
    fi
}

private_kb() {
    if [ -r "$PROC_DIR/$1/smaps" ]; then
        awk '/^Private_/ {s += $2} END { if (s == "") print "NA"; else print s }' \
            "$PROC_DIR/$1/smaps" 2>/dev/null
    else
        printf '%s' "NA"
    fi
}

process_start_time() (
    local stat
    stat="$(cat "$PROC_DIR/$1/stat" 2>/dev/null)" || return 1
    # Strip PID and the parenthesized command, which may itself contain ')'.
    set -f
    # shellcheck disable=SC2086
    set -- ${stat##*) }
    [ "$#" -ge 20 ] || return 1
    case "$7:${20}" in *[!0-9:]*) return 1 ;; esac
    # Zombies, dead tasks and kernel threads have no live userspace executable.
    case "$1" in Z|X|x) return 2 ;; esac
    [ "$(( $7 & 2097152 ))" -eq 0 ] || return 2  # Linux PF_KTHREAD
    printf '%s' "${20}"
)

process_may_be_live() {
    # An unreadable stat file is inconclusive. Only a known dead/kernel task
    # or a PID that has vanished may be omitted from the incomplete count.
    process_start_time "$1" >/dev/null || [ "$?" -ne 2 ] || return 1
    [ -d "$PROC_DIR/$1" ]
}

argv_for_pid() {
    # Map NUL separators to records. Embedded line breaks stay inside their arg.
    tr '\000\n' '\n\r' 2>/dev/null < "$PROC_DIR/$1/cmdline"
}

python_entrypoint() {
    printf '%s\n' "$1" | awk '
        NR == 1 { next }
        skip { skip = 0; next }
        mode == "module" { if ($0 == "compileall") print; exit }
        mode == "script" { print; exit }
        $0 == "--" { mode = "script"; next }
        $0 == "--check-hash-based-pycs" { skip = 1; next }
        /^--/ { next }
        $0 == "-" { exit }
        /^-/ {
            options = substr($0, 2)
            while (length(options)) {
                flag = substr(options, 1, 1)
                options = substr(options, 2)
                if (flag == "c") exit
                if (flag == "m") {
                    if (options == "") { mode = "module"; break }
                    if (options == "compileall") print options
                    exit
                }
                if (flag ~ /^[QWX]$/) { skip = (options == ""); break }
            }
            next
        }
        { print; exit }
    '
}

classification_for_entrypoint() {
    case "$1" in
        /home/cygnus/coordinator/coordinator.py)
            printf '%s' "stock coordinator fallback/service-start policy - retire or gate after source/package audit and workflow proof"
            ;;
        connector.py|*/connector.py)
            printf '%s' "unexpected regression in Deneb-native DF path - investigate"
            ;;
        print_service.py|*/print_service.py)
            printf '%s' "unexpected regression in Deneb-native print-service path - investigate"
            ;;
        /home/cygnus/menu/executor.py|menu/executor.py)
            printf '%s' "stock UI fallback - should be absent after Deneb UI install"
            ;;
        compileall|*/compileall|compileall.py|*/compileall.py|compile_all|*/compile_all|compile_all.py|*/compile_all.py)
            printf '%s' "stock startup work - should be disabled in Deneb mode"
            ;;
        *)
            printf '%s' "unclassified Python runtime - investigate and classify"
            ;;
    esac
}

report_python_process() {
    local pid="$1" pd="$PROC_DIR/$1" exe name start argv cmd class vsz rss data threads fds private
    exe="$(readlink "$pd/exe" 2>/dev/null)" || return 2
    name="${exe% (deleted)}"
    name="${name##*/}"
    case "$name" in
        python) ;;
        python[0-9]*)
            printf '%s\n' "$name" | grep -Eq '^python[0-9]+(\.[0-9]+)*[dmt]*$' || return 1
            ;;
        *) return 1 ;;
    esac

    start="$(process_start_time "$pid")" || return 2
    argv="$(argv_for_pid "$pid")" || return 2
    [ -n "$argv" ] || return 2
    class="$(classification_for_entrypoint "$(python_entrypoint "$argv")")"
    cmd="$(printf '%s' "$argv" | tr '\n\r' '  ' | sed 's/[[:space:]]*$//; s/|/\\|/g')"
    vsz="$(status_field "$pid" VmSize)"
    rss="$(status_field "$pid" VmRSS)"
    data="$(status_field "$pid" VmData)"
    threads="$(status_field "$pid" Threads)"
    fds="$(fd_count "$pid")"
    private="$(private_kb "$pid")"
    [ "$argv" = "$(argv_for_pid "$pid")" ] &&
        [ "$start" = "$(process_start_time "$pid")" ] &&
        [ "$exe" = "$(readlink "$pd/exe" 2>/dev/null)" ] || return 2

    printf '| `%s` | `%s` | %s | %s | %s | %s | %s | %s | %s |\n' \
        "$pid" "$cmd" "$class" "$vsz" "$rss" "$data" "$threads" "$fds" "$private"
}

service_state() {
    local service="$1"
    local init_script="${INIT_DIR}/$service"
    if [ ! -x "$init_script" ]; then
        printf '| `%s` | missing | missing |\n' "$service"
        return
    fi

    if [ "$service" = "coordinator" ] &&
       grep -q 'DENEB_COORDINATOR_DISABLED' "$init_script" 2>/dev/null; then
        printf '| `%s` | disabled shim | disabled shim |\n' "$service"
        return
    fi

    "$init_script" enabled >/dev/null 2>&1
    enabled_rc=$?
    "$init_script" running >/dev/null 2>&1
    running_rc=$?
    printf '| `%s` | rc %s | rc %s |\n' "$service" "$enabled_rc" "$running_rc"
}

deneb_status() {
    wget -qO- http://127.0.0.1/api/v1/printer/status 2>/dev/null || printf '%s' "unavailable"
}

tmp="${SUMMARY}.$$"
{
    printf '# Remaining Live Python Runtime Inventory\n\n'
    printf '%s `%s`\n' '- Captured:' "$(capture_datetime)"
    printf '%s `%s`\n\n' '- Printer status:' "$(deneb_status)"

    printf '## Init State Snapshot\n\n'
    printf '| Service | enabled state | running state |\n'
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
    incomplete=0
    for pd in "$PROC_DIR"/[0-9]*; do
        [ -d "$pd" ] || continue
        pid="${pd##*/}"
        if report_python_process "$pid"; then
            found=1
        elif [ "$?" -eq 2 ] && process_may_be_live "$pid"; then
            incomplete=$((incomplete + 1))
        fi
    done
    if [ "$incomplete" -gt 0 ]; then
        printf '\nInventory incomplete. Could not inspect all live processes (%s unresolved).\n' "$incomplete"
    elif [ "$found" -eq 0 ]; then
        printf '| none | none | no live Python process found | 0 | 0 | 0 | 0 | 0 | 0 |\n'
    fi

    printf '\n## Expected Deneb-Native Absences\n\n'
    printf '%s\n' '- `connector.py`: absent unless investigating a Digital Factory fallback regression.'
    printf '%s\n' '- `print_service.py`: absent when `deneb-printsvc` owns the print backend.'
    printf '%s\n' '- `/home/cygnus/menu/executor.py`: absent after native `deneb-ui` install and reboot.'
    printf '%s\n' '- `compile_all` / `python3 -m compileall`: absent after Deneb disables stock compile work.'
} > "$tmp"
mv "$tmp" "$SUMMARY"
cat "$SUMMARY"
