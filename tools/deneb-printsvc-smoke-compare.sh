#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Compare two deneb-printsvc-smoke summaries without Python. Intended for
# stock-vs-native live runs captured by deneb-printsvc-smoke.

set -u

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke-compare STOCK.summary NATIVE.summary

Prints compact before/after deltas for memory, CPU, boot-sync, and throughput.
The command is evidence-oriented: missing required fields make it fail instead
of silently reporting an incomplete comparison.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

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
require_pattern "$STOCK" \
    'sample=initial .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing initial print_service.py process evidence"
require_pattern "$STOCK" \
    'sample=final .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing final print_service.py process evidence"
require_pattern "$NATIVE" \
    ' phase=printer-(job-running|complete-job-running|preheat-abort-active|cura-job-running) .*native_active:true.*native_stop_allowed:true' \
    "native summary missing active native stop-allowed evidence"
require_pattern "$NATIVE" \
    ' phase=printer-(job-aborted|preheat-aborted|cura-job-aborted|job-completed) .*native_active:false.*native_stop_allowed:false' \
    "native summary missing inactive native stop-disabled evidence"

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

if [ "$failures" -ne 0 ]; then
    echo "$failures comparison failure(s)" >&2
    exit 1
fi

echo "summary comparison passed: stock=$STOCK native=$NATIVE"
