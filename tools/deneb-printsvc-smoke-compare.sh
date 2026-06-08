#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Compare two deneb-printsvc-smoke summaries without Python. Intended for
# stock-vs-native live runs captured by deneb-printsvc-smoke.

set -u

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke-compare [--require-reduction] STOCK.summary NATIVE.summary

Prints compact before/after deltas for memory, CPU, boot-sync, and throughput.
The command is evidence-oriented: missing required fields make it fail instead
of silently reporting an incomplete comparison.

Options:
  --require-reduction  Fail if native memory/RSS/CPU/boot evidence is not lower
                       than stock, or if throughput is lower than stock.
EOF
}

REQUIRE_REDUCTION=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --require-reduction)
            REQUIRE_REDUCTION=1
            shift
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
            break
            ;;
    esac
done

if [ "$#" -ne 2 ]; then
    usage >&2
    exit 2
fi

STOCK="$1"
NATIVE="$2"

failures=0

fail() {
    echo "FAIL: $*" >&2
    failures=$((failures + 1))
}

summary_value() {
    file="$1"
    label="$2"
    key="$3"
    awk -v label="$label" -v key="$key" '
        index($0, "sample=" label " ") {
            n = split($0, fields, " ");
            for (i = 1; i <= n; i++) {
                if (index(fields[i], key "=") == 1) {
                    sub("^[^=]*=", "", fields[i]);
                    gsub("\r", "", fields[i]);
                    print fields[i];
                    exit;
                }
            }
        }
    ' "$file"
}

phase_value() {
    file="$1"
    phase="$2"
    key="$3"
    awk -v phase="$phase" -v key="$key" '
        index($0, "phase=" phase " ") {
            n = split($0, fields, " ");
            for (i = 1; i <= n; i++) {
                if (index(fields[i], key "=") == 1) {
                    sub("^[^=]*=", "", fields[i]);
                    gsub("\r", "", fields[i]);
                    print fields[i];
                    exit;
                }
            }
        }
    ' "$file"
}

last_process_rss_total() {
    file="$1"
    label="$2"
    awk -v label="$label" '
        index($0, "sample=" label " ") && index($0, " pid=") {
            n = split($0, fields, " ");
            for (i = 1; i <= n; i++) {
                if (index(fields[i], "vmrss_kb=") == 1) {
                    sub("^[^=]*=", "", fields[i]);
                    total += fields[i] + 0;
                }
            }
        }
        END { if (total != "") print total; }
    ' "$file"
}

require_number() {
    value="$1"
    label="$2"
    case "$value" in
        ''|*[!0-9.-]*) fail "$label missing or non-numeric"; return 1 ;;
    esac
    return 0
}

require_positive_integer() {
    value="$1"
    label="$2"

    if ! require_number "$value" "$label"; then
        return 1
    fi
    if [ "$value" -le 0 ]; then
        fail "$label must be positive"
        return 1
    fi
    return 0
}

require_less_than() {
    before="$1"
    after="$2"
    label="$3"

    if ! require_number "$before" "$label stock"; then
        return 1
    fi
    if ! require_number "$after" "$label native"; then
        return 1
    fi
    if [ "$after" -ge "$before" ]; then
        fail "$label did not improve: stock=$before native=$after"
        return 1
    fi
    return 0
}

require_at_least() {
    before="$1"
    after="$2"
    label="$3"

    if ! require_number "$before" "$label stock"; then
        return 1
    fi
    if ! require_number "$after" "$label native"; then
        return 1
    fi
    if [ "$after" -lt "$before" ]; then
        fail "$label regressed: stock=$before native=$after"
        return 1
    fi
    return 0
}

require_pattern() {
    file="$1"
    pattern="$2"
    label="$3"

    if grep -Eq "$pattern" "$file"; then
        return 0
    fi
    fail "$label"
    return 1
}

reject_pattern() {
    file="$1"
    pattern="$2"
    label="$3"

    if grep -Eq "$pattern" "$file"; then
        fail "$label"
        return 1
    fi
    return 0
}

require_status_phase() {
    phase="$1"
    status="$2"

    require_pattern "$NATIVE" \
        " phase=status-${phase} .*rc=0 .*status=${status} .*body=.*native_only_route:true" \
        "native summary missing status/native-route evidence for ${phase}"
}

require_printer_stop_phase() {
    phase="$1"
    active="$2"
    stop_allowed="$3"

    require_pattern "$NATIVE" \
        " phase=printer-${phase} .*rc=0 .*body=.*native_active:${active}.*native_stop_allowed:${stop_allowed}" \
        "native summary missing printer native active/stop evidence for ${phase}"
}

require_local_job_evidence() {
    require_pattern "$NATIVE" \
        ' phase=local-job-native .*kind=printsvc-cli .*rc=0' \
        "native summary missing native local/USB job IPC smoke evidence"
    require_pattern "$NATIVE" \
        ' phase=local-job-start .*source=USB .*rc=0' \
        "native summary missing native local/USB job source evidence"
    require_pattern "$NATIVE" \
        ' phase=local-job-accepted .*deneb_state=pre_print .*native_active=true .*native_stop_allowed=true .*source=USB .*rc=0' \
        "native summary missing native local/USB accepted stop-state evidence"
    require_pattern "$NATIVE" \
        ' phase=local-job-abort .*rc=0' \
        "native summary missing native local/USB abort evidence"
    require_pattern "$NATIVE" \
        ' phase=local-job-aborted-state .*deneb_state=idle .*native_active=false .*native_stop_allowed=false .*rc=0' \
        "native summary missing native local/USB aborted idle-state evidence"
}

delta_line() {
    name="$1"
    before="$2"
    after="$3"
    unit="$4"

    if ! require_number "$before" "$name before"; then
        return
    fi
    if ! require_number "$after" "$name after"; then
        return
    fi
    delta=$((after - before))
    printf 'compare=%s before=%s after=%s delta=%s unit=%s\n' \
        "$name" "$before" "$after" "$delta" "$unit"
}

if [ ! -s "$STOCK" ]; then
    fail "stock summary missing or empty: $STOCK"
fi
if [ ! -s "$NATIVE" ]; then
    fail "native summary missing or empty: $NATIVE"
fi
[ "$failures" -eq 0 ] || exit 1

require_pattern "$NATIVE" \
    ' phase=native-driver-process .*deneb_printsvc=1 .*print_service_py=0 .*rc=0' \
    "native summary missing deneb-printsvc process ownership evidence"
reject_pattern "$NATIVE" \
    'sample=[^ ]+ .*command="?.*print_service.py' \
    "native summary contains stock print_service.py process sample"
require_pattern "$NATIVE" \
    ' phase=route-native-enabled .*rc=0 .*body=.*print_backend:native.*native_only_route:true' \
    "native summary missing native-only route evidence"
require_pattern "$NATIVE" \
    ' phase=boot-sync-ready .*rc=0' \
    "native summary missing successful boot-sync evidence"
require_pattern "$NATIVE" \
    ' phase=boot-sync-ready .*route_body=[^ ]*native_only_route:true' \
    "native summary missing native-only boot-sync route evidence"
require_pattern "$NATIVE" \
    ' phase=boot-sync-ready .*status=(idle|printing|paused|error|offline|finished) ' \
    "native summary missing scalar boot-sync status evidence"
require_pattern "$NATIVE" \
    ' phase=boot-sync-ready .*status_body=[^ ]*native_only_route:true' \
    "native summary missing native-only boot-sync status body evidence"
require_local_job_evidence
for phase in initial native-enabled cooldown motion macro service-restarted \
    job-aborted cura-job-aborted preheat-aborted active-aborted \
    job-completed; do
    require_status_phase "$phase" idle
done
for phase in heating job-running job-resumed cura-job-running \
    preheat-abort-active active-abort-printing complete-job-running; do
    require_status_phase "$phase" printing
done
require_status_phase job-paused paused
require_pattern "$STOCK" \
    'sample=initial .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing initial print_service.py process evidence"
require_pattern "$STOCK" \
    'sample=final .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing final print_service.py process evidence"
for phase in heating job-running job-paused job-resumed cura-job-running \
    preheat-abort-active active-abort-printing complete-job-running; do
    require_printer_stop_phase "$phase" true true
done
for phase in initial native-enabled cooldown motion macro service-restarted \
    job-aborted cura-job-aborted preheat-aborted active-aborted \
    job-completed; do
    require_printer_stop_phase "$phase" false false
done

stock_initial_mem="$(summary_value "$STOCK" initial mem_used_kb)"
native_initial_mem="$(summary_value "$NATIVE" initial mem_used_kb)"
stock_final_mem="$(summary_value "$STOCK" final mem_used_kb)"
native_final_mem="$(summary_value "$NATIVE" final mem_used_kb)"
stock_initial_rss="$(last_process_rss_total "$STOCK" initial)"
native_initial_rss="$(last_process_rss_total "$NATIVE" initial)"
stock_final_rss="$(last_process_rss_total "$STOCK" final)"
native_final_rss="$(last_process_rss_total "$NATIVE" final)"
stock_initial_cpu="$(summary_value "$STOCK" initial cpu_total_jiffies)"
native_initial_cpu="$(summary_value "$NATIVE" initial cpu_total_jiffies)"
stock_final_cpu="$(summary_value "$STOCK" final cpu_total_jiffies)"
native_final_cpu="$(summary_value "$NATIVE" final cpu_total_jiffies)"
stock_cpu_interval=""
native_cpu_interval=""
if require_number "$stock_initial_cpu" "stock CPU initial" &&
   require_number "$stock_final_cpu" "stock CPU final"; then
    stock_cpu_interval=$((stock_final_cpu - stock_initial_cpu))
    require_positive_integer "$stock_cpu_interval" "stock CPU interval"
fi
if require_number "$native_initial_cpu" "native CPU initial" &&
   require_number "$native_final_cpu" "native CPU final"; then
    native_cpu_interval=$((native_final_cpu - native_initial_cpu))
    require_positive_integer "$native_cpu_interval" "native CPU interval"
fi
stock_boot="$(phase_value "$STOCK" boot-sync-ready elapsed_seconds)"
native_boot="$(phase_value "$NATIVE" boot-sync-ready elapsed_seconds)"
stock_bps="$(phase_value "$STOCK" job-throughput bytes_per_second)"
native_bps="$(phase_value "$NATIVE" job-throughput bytes_per_second)"
require_positive_integer "$stock_bps" "stock print throughput"
require_positive_integer "$native_bps" "native print throughput"

delta_line mem_used_initial "$stock_initial_mem" "$native_initial_mem" kb
delta_line mem_used_final "$stock_final_mem" "$native_final_mem" kb
delta_line process_rss_initial "$stock_initial_rss" "$native_initial_rss" kb
delta_line process_rss_final "$stock_final_rss" "$native_final_rss" kb
delta_line cpu_total_initial "$stock_initial_cpu" "$native_initial_cpu" jiffies
delta_line cpu_total_final "$stock_final_cpu" "$native_final_cpu" jiffies
delta_line cpu_total_interval "$stock_cpu_interval" "$native_cpu_interval" jiffies
delta_line boot_sync_elapsed "$stock_boot" "$native_boot" seconds
delta_line print_throughput "$stock_bps" "$native_bps" bytes_per_second

if [ "$REQUIRE_REDUCTION" = "1" ]; then
    require_less_than "$stock_initial_mem" "$native_initial_mem" "initial memory use"
    require_less_than "$stock_final_mem" "$native_final_mem" "final memory use"
    require_less_than "$stock_initial_rss" "$native_initial_rss" "initial print-service RSS"
    require_less_than "$stock_final_rss" "$native_final_rss" "final print-service RSS"
    require_less_than "$stock_cpu_interval" "$native_cpu_interval" "CPU interval"
    require_less_than "$stock_boot" "$native_boot" "boot-sync elapsed time"
    require_at_least "$stock_bps" "$native_bps" "print throughput"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures comparison failure(s)" >&2
    exit 1
fi

echo "summary comparison passed: stock=$STOCK native=$NATIVE"
