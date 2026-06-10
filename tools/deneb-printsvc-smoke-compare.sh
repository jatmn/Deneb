#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
#
# Compare deneb-printsvc-smoke summaries without Python. Intended for
# stock-vs-native live runs captured by deneb-printsvc-smoke.

set -u

usage() {
    cat <<'EOF'
Usage: deneb-printsvc-smoke-compare [--require-reduction] STOCK.summary NATIVE_RESOURCE.summary [NATIVE_EVIDENCE.summary...]

Prints compact before/after deltas for firmware metadata, ambient telemetry,
memory, CPU, boot-sync, and throughput.
The command is evidence-oriented: missing required fields make it fail instead
of silently reporting an incomplete comparison.
When multiple native summaries are supplied, the first native summary provides
resource/firmware/throughput measurements and all native summaries are searched
together for workflow/status evidence. This keeps live motion/heat tests safely
split across small runs instead of requiring one giant physical bundle.

Options:
  --require-reduction  Fail if native memory/RSS/CPU evidence is not lower,
                       boot evidence is slower, or print throughput is more
                       than 15% below stock.
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

if [ "$#" -lt 2 ]; then
    usage >&2
    exit 2
fi

STOCK="$1"
shift
NATIVE_RESOURCE="$1"
shift
NATIVE_EVIDENCE="$NATIVE_RESOURCE"
NATIVE_DISPLAY="$NATIVE_RESOURCE"
TMP_NATIVE_EVIDENCE=""

cleanup() {
    if [ -n "$TMP_NATIVE_EVIDENCE" ]; then
        rm -f "$TMP_NATIVE_EVIDENCE"
    fi
}
trap cleanup EXIT HUP INT TERM

if [ "$#" -gt 0 ]; then
    TMP_NATIVE_EVIDENCE="${TMPDIR:-/tmp}/deneb-printsvc-native-evidence.$$"
    : > "$TMP_NATIVE_EVIDENCE" || exit 1
    for file in "$NATIVE_RESOURCE" "$@"; do
        if [ ! -s "$file" ]; then
            echo "FAIL: native evidence summary missing or empty: $file" >&2
            exit 1
        fi
        printf '# summary_file=%s\n' "$file" >> "$TMP_NATIVE_EVIDENCE"
        cat "$file" >> "$TMP_NATIVE_EVIDENCE"
        printf '\n' >> "$TMP_NATIVE_EVIDENCE"
    done
    NATIVE_EVIDENCE="$TMP_NATIVE_EVIDENCE"
    NATIVE_DISPLAY="$NATIVE_RESOURCE +$# evidence"
fi
NATIVE="$NATIVE_EVIDENCE"

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

last_driver_rss_total() {
    file="$1"
    label="$2"
    pattern="$3"
    awk -v label="$label" -v pattern="$pattern" '
        index($0, "sample=" label " ") && index($0, " pid=") && $0 ~ pattern {
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

require_positive_number() {
    value="$1"
    label="$2"

    if ! require_number "$value" "$label"; then
        return 1
    fi
    awk -v value="$value" -v label="$label" '
        BEGIN {
            if ((value + 0) <= 0) {
                printf("FAIL: %s must be positive\n", label) > "/dev/stderr";
                exit 1;
            }
        }
    '
    if [ "$?" -ne 0 ]; then
        failures=$((failures + 1))
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

require_within_percent_floor() {
    before="$1"
    after="$2"
    percent="$3"
    label="$4"

    if ! require_number "$before" "$label stock"; then
        return 1
    fi
    if ! require_number "$after" "$label native"; then
        return 1
    fi
    if [ $((after * 100)) -lt $((before * percent)) ]; then
        fail "$label regressed beyond tolerance: stock=$before native=$after minimum_percent=$percent"
        return 1
    fi
    return 0
}

require_at_most() {
    before="$1"
    after="$2"
    label="$3"

    if ! require_number "$before" "$label stock"; then
        return 1
    fi
    if ! require_number "$after" "$label native"; then
        return 1
    fi
    if [ "$after" -gt "$before" ]; then
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
        " phase=status-${phase} .*rc=0 .*status=${status}" \
        "native summary missing scalar status evidence for ${phase}"
}

require_printer_stop_phase() {
    phase="$1"
    active="$2"
    stop_allowed="$3"

    require_pattern "$NATIVE" \
        " phase=printer-${phase} .*rc=0 .*body=.*native_active:${active}.*native_stop_allowed:${stop_allowed}" \
        "native summary missing printer native active/stop evidence for ${phase}"
}

require_abort_requested_phase() {
    phase="$1"

    require_status_phase "$phase" aborting
    require_printer_stop_phase "$phase" true false
}

require_abort_draining_phase() {
    phase="$1"

    if grep -Eq " phase=status-${phase} .*rc=0 .*status=aborting" "$NATIVE"; then
        require_printer_stop_phase "$phase" true false
        return
    fi

    require_status_phase "$phase" idle
    require_printer_stop_phase "$phase" false false
}

require_completion_runtime_evidence() {
    require_pattern "$NATIVE" \
        ' phase=printer-job-completed .*rc=0 .*body=.*flow_inflight:0.*flow_resend:0' \
        "native summary missing completed drained flow evidence"

    if grep -Eq ' phase=status-complete-job-running .*rc=0 .*status=printing' "$NATIVE" &&
       grep -Eq ' phase=printer-complete-job-running .*rc=0 .*body=.*native_active:true.*native_stop_allowed:true' "$NATIVE"; then
        return 0
    fi

    if grep -Eq ' phase=status-complete-job-running .*rc=0 .*status=idle' "$NATIVE" &&
       grep -Eq ' phase=printer-complete-job-running .*rc=0 .*body=.*native_active:false.*native_stop_allowed:false' "$NATIVE" &&
       grep -Eq ' phase=job-completion-wait .*elapsed=0 .*rc=0' "$NATIVE"; then
        return 0
    fi

    fail "native summary missing completion active-running or fast-completed evidence"
    return 1
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

require_physical_safety_evidence() {
    require_pattern "$NATIVE" \
        ' phase=heat-safety .*kind=physical .*axes=none .*required_home=none .*travel=bed_to_40C_nozzle_to_50C_then_cooldown .*rc=0' \
        "native summary missing heat physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=motion-safety .*kind=physical .*axes=Z .*required_home=Z .*travel=z_home_to_max_only .*rc=0' \
        "native summary missing motion physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=macro-safety .*kind=physical .*axes=(XYZ|Z) .*required_home=(XYZ|Z) .*rc=0' \
        "native summary missing macro physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=local-job-safety .*kind=physical .*axes=job_defined .*required_home=(z_home|home) .*rc=0' \
        "native summary missing local/USB job physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=job-safety .*kind=physical .*axes=XYZ .*required_home=home .*rc=0' \
        "native summary missing all-axis pause/resume job physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=cura-job-safety .*kind=physical .*axes=(job_defined|XYZ) .*required_home=(z_home|home) .*rc=0' \
        "native summary missing Cura job physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=preheat-abort-safety .*kind=physical .*axes=(job_defined|XYZ) .*required_home=(z_home|home) .*rc=0' \
        "native summary missing preheat-abort physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=active-abort-safety .*kind=physical .*axes=(job_defined|XYZ) .*required_home=(z_home|home) .*rc=0' \
        "native summary missing active-abort physical safety evidence"
    require_pattern "$NATIVE" \
        ' phase=complete-job-safety .*kind=physical .*((axes=Z .*required_home=(z_home|home) .*travel=bounded_relative_Z_negative_max_96mm)|(axes=XYZ .*required_home=home .*travel=bounded_cura_style_xyz_no_heat_no_extrusion_to_completion)) .*rc=0' \
        "native summary missing completion physical safety evidence"
}

require_firmware_proof() {
    file="$1"
    label="$2"

    require_pattern "$file" \
        ' phase=firmware-proof .*rc=0 .*firmware=[A-Za-z0-9_.:-]+' \
        "$label summary missing firmware-proof metadata"
    require_pattern "$file" \
        ' phase=firmware-proof .*machine_type=[A-Za-z0-9_.:-]+' \
        "$label summary missing firmware-proof machine type"
    require_pattern "$file" \
        ' phase=firmware-proof .*bed_current=([1-9][0-9]*([.][0-9]+)?|0[.][1-9][0-9]*) .*nozzle_current=([1-9][0-9]*([.][0-9]+)?|0[.][1-9][0-9]*)' \
        "$label summary missing positive ambient bed/nozzle temperatures"
}

compare_firmware_proof() {
    stock_firmware="$(phase_value "$STOCK" firmware-proof firmware)"
    native_firmware="$(phase_value "$NATIVE_RESOURCE" firmware-proof firmware)"
    stock_machine="$(phase_value "$STOCK" firmware-proof machine_type)"
    native_machine="$(phase_value "$NATIVE_RESOURCE" firmware-proof machine_type)"
    stock_pcb="$(phase_value "$STOCK" firmware-proof pcb_id)"
    native_pcb="$(phase_value "$NATIVE_RESOURCE" firmware-proof pcb_id)"
    stock_pcb_valid="$(phase_value "$STOCK" firmware-proof pcb_id_valid)"
    native_pcb_valid="$(phase_value "$NATIVE_RESOURCE" firmware-proof pcb_id_valid)"
    stock_bed="$(phase_value "$STOCK" firmware-proof bed_current)"
    native_bed="$(phase_value "$NATIVE_RESOURCE" firmware-proof bed_current)"
    stock_nozzle="$(phase_value "$STOCK" firmware-proof nozzle_current)"
    native_nozzle="$(phase_value "$NATIVE_RESOURCE" firmware-proof nozzle_current)"

    require_firmware_proof "$STOCK" stock
    require_firmware_proof "$NATIVE_RESOURCE" native
    require_positive_number "$stock_bed" "stock bed current temperature"
    require_positive_number "$native_bed" "native bed current temperature"
    require_positive_number "$stock_nozzle" "stock nozzle current temperature"
    require_positive_number "$native_nozzle" "native nozzle current temperature"

    if [ "$stock_firmware" != "none" ] &&
       [ "$stock_firmware" != "$native_firmware" ]; then
        fail "native firmware metadata does not match stock: stock=$stock_firmware native=$native_firmware"
    fi
    if [ "$native_firmware" = "none" ] &&
       [ "$stock_firmware" != "none" ]; then
        fail "native firmware metadata fell back to none while stock reported $stock_firmware"
    fi
    if [ "$stock_machine" != "none" ] &&
       [ "$stock_machine" != "unknown" ] &&
       [ "$stock_machine" != "$native_machine" ]; then
        fail "native machine type does not match stock: stock=$stock_machine native=$native_machine"
    fi
    if [ "$stock_pcb_valid" = "true" ] &&
       { [ "$native_pcb_valid" != "true" ] || [ "$stock_pcb" != "$native_pcb" ]; }; then
        fail "native PCB metadata does not match valid stock PCB metadata: stock=$stock_pcb/$stock_pcb_valid native=$native_pcb/$native_pcb_valid"
    fi

    printf 'compare=firmware_proof stock_firmware=%s native_firmware=%s stock_machine_type=%s native_machine_type=%s stock_pcb_id=%s native_pcb_id=%s stock_bed_current=%s native_bed_current=%s stock_nozzle_current=%s native_nozzle_current=%s\n' \
        "$stock_firmware" "$native_firmware" "$stock_machine" "$native_machine" \
        "$stock_pcb" "$native_pcb" "$stock_bed" "$native_bed" \
        "$stock_nozzle" "$native_nozzle"
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
if [ ! -s "$NATIVE_RESOURCE" ]; then
    fail "native resource summary missing or empty: $NATIVE_RESOURCE"
fi
if [ ! -s "$NATIVE_EVIDENCE" ]; then
    fail "native evidence summary missing or empty: $NATIVE_DISPLAY"
fi
[ "$failures" -eq 0 ] || exit 1

require_pattern "$NATIVE_EVIDENCE" \
    ' phase=native-driver-process .*deneb_printsvc=1 .*print_service_py=0 .*rc=0' \
    "native summary missing deneb-printsvc process ownership evidence"
reject_pattern "$NATIVE_EVIDENCE" \
    'sample=[^ ]+ .*command="?.*print_service.py' \
    "native summary contains stock print_service.py process sample"
require_pattern "$NATIVE_EVIDENCE" \
    ' phase=route-native-enabled .*rc=0 .*body=.*print_backend:native.*native_only_route:true' \
    "native summary missing native-only route evidence"
require_pattern "$NATIVE_EVIDENCE" \
    ' phase=boot-sync-ready .*rc=0' \
    "native summary missing successful boot-sync evidence"
require_pattern "$NATIVE_EVIDENCE" \
    ' phase=boot-sync-ready .*route_body=[^ ]*native_only_route:true' \
    "native summary missing native-only boot-sync route evidence"
require_pattern "$NATIVE_EVIDENCE" \
    ' phase=boot-sync-ready .*status=(idle|printing|paused|error|offline|finished) ' \
    "native summary missing scalar boot-sync status evidence"
require_physical_safety_evidence
require_local_job_evidence
for snapshot in job-abort-requested job-abort-draining \
    cura-job-abort-requested cura-job-abort-draining; do
    require_pattern "$NATIVE" \
        " snapshot=${snapshot}" \
        "native summary missing ${snapshot} evidence"
done
for phase in initial native-enabled cooldown motion macro service-restarted \
    job-aborted cura-job-aborted preheat-aborted active-aborted \
    job-completed; do
    require_status_phase "$phase" idle
done
for phase in heating job-running job-resumed cura-job-running \
    preheat-abort-active active-abort-printing; do
    require_status_phase "$phase" printing
done
for phase in job-abort-requested cura-job-abort-requested \
    preheat-abort-requested active-abort-requested; do
    require_abort_requested_phase "$phase"
done
for phase in job-abort-draining cura-job-abort-draining \
    preheat-abort-draining active-abort-draining; do
    require_abort_draining_phase "$phase"
done
require_status_phase job-paused paused
require_pattern "$NATIVE" \
    ' phase=complete-job-fixture-check .*rc=0 .*reason=progress_command' \
    "native summary missing non-dwell completion fixture evidence"
require_completion_runtime_evidence
require_pattern "$STOCK" \
    ' phase=complete-job-position .*running_z=[0-9][0-9.]* .*final_z=[0-9][0-9.]* .*delta_z=[1-9][0-9.]* .*rc=0' \
    "stock summary missing completion Z travel evidence"
require_pattern "$NATIVE_RESOURCE" \
    ' phase=complete-job-position .*running_z=[0-9][0-9.]* .*final_z=[0-9][0-9.]* .*delta_z=[1-9][0-9.]* .*rc=0' \
    "native summary missing completion Z travel evidence"
require_pattern "$STOCK" \
    ' phase=stock-driver-process .*deneb_printsvc=0 .*print_service_py=1 .*rc=0' \
    "stock summary missing stock print_service.py process ownership evidence"
require_pattern "$STOCK" \
    'sample=initial .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing initial print_service.py process evidence"
require_pattern "$STOCK" \
    'sample=final .*pid=[0-9]+ .*command="?.*print_service.py' \
    "stock summary missing final print_service.py process evidence"
reject_pattern "$STOCK" \
    'sample=[^ ]+ .*command="?.*/usr/bin/deneb-printsvc' \
    "stock summary contains native deneb-printsvc process sample"
for phase in heating job-running job-paused job-resumed cura-job-running \
    preheat-abort-active active-abort-printing; do
    require_printer_stop_phase "$phase" true true
done
for phase in initial native-enabled cooldown motion macro service-restarted \
    job-aborted cura-job-aborted preheat-aborted active-aborted \
    job-completed; do
    require_printer_stop_phase "$phase" false false
done

compare_firmware_proof

stock_initial_mem="$(summary_value "$STOCK" initial mem_used_kb)"
native_initial_mem="$(summary_value "$NATIVE_RESOURCE" initial mem_used_kb)"
stock_final_mem="$(summary_value "$STOCK" final mem_used_kb)"
native_final_mem="$(summary_value "$NATIVE_RESOURCE" final mem_used_kb)"
stock_initial_rss="$(last_driver_rss_total "$STOCK" initial 'print_service[.]py')"
native_initial_rss="$(last_driver_rss_total "$NATIVE_RESOURCE" initial 'deneb-printsvc')"
stock_final_rss="$(last_driver_rss_total "$STOCK" final 'print_service[.]py')"
native_final_rss="$(last_driver_rss_total "$NATIVE_RESOURCE" final 'deneb-printsvc')"
stock_initial_cpu="$(summary_value "$STOCK" initial cpu_total_jiffies)"
native_initial_cpu="$(summary_value "$NATIVE_RESOURCE" initial cpu_total_jiffies)"
stock_final_cpu="$(summary_value "$STOCK" final cpu_total_jiffies)"
native_final_cpu="$(summary_value "$NATIVE_RESOURCE" final cpu_total_jiffies)"
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
native_boot="$(phase_value "$NATIVE_RESOURCE" boot-sync-ready elapsed_seconds)"
stock_bps="$(phase_value "$STOCK" job-throughput bytes_per_second)"
native_bps="$(phase_value "$NATIVE_RESOURCE" job-throughput bytes_per_second)"
require_positive_integer "$stock_bps" "stock print throughput"
require_positive_integer "$native_bps" "native print throughput"

delta_line mem_used_initial "$stock_initial_mem" "$native_initial_mem" kb
delta_line mem_used_final "$stock_final_mem" "$native_final_mem" kb
delta_line driver_rss_initial "$stock_initial_rss" "$native_initial_rss" kb
delta_line driver_rss_final "$stock_final_rss" "$native_final_rss" kb
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
    require_at_most "$stock_boot" "$native_boot" "boot-sync elapsed time"
    require_within_percent_floor "$stock_bps" "$native_bps" 85 \
        "print throughput"
fi

if [ "$failures" -ne 0 ]; then
    echo "$failures comparison failure(s)" >&2
    exit 1
fi

echo "summary comparison passed: stock=$STOCK native=$NATIVE_DISPLAY"
